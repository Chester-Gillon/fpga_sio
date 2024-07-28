/*
 * @file vfio_access.c
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Implement a library to allow access to device using VFIO
 * @details
 *  Supports:
 *  a. Access to memory mapped BARs in the device
 *  b. IOVA access to host memory by a DMA controller in the device
 */

#include "vfio_access.h"
#include "vfio_access_private.h"
#include "pci_sysfs_access.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/un.h>


/* Optional filters which may be set to only open by VFIO specific PCI device(s) by location.
 * This can be used to limit which VFIO devices one process may open. */
static vfio_pci_device_location_filter_t vfio_pci_device_location_filters[MAX_VFIO_DEVICES];
static uint32_t num_pci_device_location_filters;


/* Controls how containers are allocated when multiple IOMMU groups are opened by the same process:
 * - When false all IOMMU groups share the same container, and therefore can use the same IOVA allocations.
 * - When true each IOMMU group is allocated a different container, and therefore can't share IOVA allocations.
 */
static bool vfio_isolate_iommu_groups;


/**
 * @brief Add an optional PCI device location filter
 * @detail This may be called before open_vfio_devices_matching_filter() to only open using VFIO specific PCI devices
 *         in the event that there is one than one PCI device which matches the identity filters.
 * @param[in] device_name The PCI device location, as <domain>:<bus>:<dev>.<func> hex values
 */
void vfio_add_pci_device_location_filter (const char *const device_name)
{
    char junk;

    if (num_pci_device_location_filters < MAX_VFIO_DEVICES)
    {
        vfio_pci_device_location_filter_t *const filter = &vfio_pci_device_location_filters[num_pci_device_location_filters];
        const int num_values = sscanf (device_name, "%d:%" SCNx8 ":%" SCNx8 ".%" SCNx8 "%c",
                &filter->domain, &filter->bus, &filter->dev, &filter->func, &junk);

        if (num_values == 4)
        {
            num_pci_device_location_filters++;
        }
        else
        {
            printf ("Error: Invalid PCI device location filter %s\n", device_name);
            exit (EXIT_FAILURE);
        }
    }
}


/**
 * @brief Create a memory buffer to be used for VFIO
 * @param[out] buffer The created memory buffer, which has been mapped into the virtual address space
 * @param[in] size The size in bytes of the buffer to create
 * @param[in] buffer_allocation How to allocate the buffer
 * @param[in] name_suffix For VFIO_BUFFER_ALLOCATION_SHARED_MEMORY a suffix used to create a unique name
 */
void create_vfio_buffer (vfio_buffer_t *const buffer,
                         const size_t size, const vfio_buffer_allocation_type_t buffer_allocation,
                         const char *const name_suffix)
{
    int rc;
    const size_t page_size = (size_t) getpagesize ();

    buffer->allocation_type = buffer_allocation;
    buffer->size = size;

    switch (buffer->allocation_type)
    {
    case VFIO_BUFFER_ALLOCATION_HEAP:
        rc = posix_memalign (&buffer->vaddr, page_size, buffer->size);
        if (rc != 0)
        {
            buffer->vaddr = NULL;
            printf ("Failed to allocate %zu bytes for VFIO DMA mapping\n", buffer->size);
        }
        break;

    case VFIO_BUFFER_ALLOCATION_SHARED_MEMORY:
        buffer->vaddr = NULL;

        /* Create the shared memory pathname, with a fixed prefix and a caller supplied suffix */
        snprintf (buffer->pathname, sizeof (buffer->pathname), "/vfio_buffer_%s", name_suffix);

        /* Create a POSIX shared memory file */
        buffer->fd = shm_open (buffer->pathname, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO);
        if (buffer->fd < 0)
        {
            printf ("shm_open(%s,O_CREAT) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }

        rc = posix_fallocate (buffer->fd, 0, (off_t) buffer->size);
        if (rc != 0)
        {
            printf ("posix_fallocate(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }

        rc = fsync (buffer->fd);
        if (rc != 0)
        {
            printf ("fsync(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }

        rc = close (buffer->fd);
        if (rc != 0)
        {
            printf ("close(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }

        /* Map the POSIX shared memory file into the virtual address space */
        buffer->fd = shm_open (buffer->pathname, O_RDWR, 0);
        if (buffer->fd < 0)
        {
            printf ("shm_open(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }

        buffer->vaddr = mmap (NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->fd, 0);
        if (buffer->vaddr == (void *) -1)
        {
            buffer->vaddr = NULL;
            printf ("mmap(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }
        break;

    case VFIO_BUFFER_ALLOCATION_HUGE_PAGES:
        buffer->vaddr = mmap (NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (buffer->vaddr == (void *) -1)
        {
            buffer->vaddr = NULL;
            printf ("mmap(%zu) failed : %s\n", buffer->size, strerror (errno));
            return;
        }
        break;

    case VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A32:
    case VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A64:
#ifdef HAVE_CMEM
        /* Perform a dynamic memory allocation of physical contiguous memory for a single buffer */
        buffer->vaddr = NULL;
        rc = cmem_drv_alloc (buffer->allocation_type == VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A64,
                1, size, &buffer->cmem_host_buf_desc);
        if (rc == 0)
        {
            buffer->vaddr = buffer->cmem_host_buf_desc.userAddr;
        }
        else
        {
            printf ("cmem_drv_alloc(%zu) failed : %s\n", buffer->size, strerror (errno));
            return;
        }
#endif
        break;
    }
}


/**
 * @brief Release the resources for a memory buffer used for VFIO
 * @param[in/out] buffer The memory buffer to release
 */
void free_vfio_buffer (vfio_buffer_t *const buffer)
{
    int rc;

    switch (buffer->allocation_type)
    {
    case VFIO_BUFFER_ALLOCATION_HEAP:
        free (buffer->vaddr);
        break;

    case VFIO_BUFFER_ALLOCATION_SHARED_MEMORY:
        rc = munmap (buffer->vaddr, buffer->size);
        if (rc != 0)
        {
            printf ("munmap(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }
        rc = close (buffer->fd);
        if (rc != 0)
        {
            printf ("close(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }
        rc = shm_unlink (buffer->pathname);
        if (rc != 0)
        {
            printf ("shm_unlink(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }
        break;

    case VFIO_BUFFER_ALLOCATION_HUGE_PAGES:
        /* @todo If buffer->size is only a 4K normal page then munmap() fails with EINVAL, even though the mmap()
         *       call succeeded with the same size.
         *       Seen on AlmaLinux 8.7 with a 4.18.0-425.10.1.el8_7.x86_64 Kernel and 2MB huge pages.
         *
         *       To avoid this error would probably need to parse the actual huge page size and use that to
         *       round-up the buffer->size.
         *
         *       When the program exits the huge pages are freed.
         */
        rc = munmap (buffer->vaddr, buffer->size);
        if (rc != 0)
        {
            printf ("munmap(%zu) failed : %s\n", buffer->size, strerror (errno));
            return;
        }
        break;

    case VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A32:
    case VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A64:
#ifdef HAVE_CMEM
        rc = cmem_drv_free (1, &buffer->cmem_host_buf_desc);
#endif
        break;
    }

    buffer->size = 0;
    buffer->vaddr = NULL;
    buffer->fd = -1;
}


/*
 * @brief Scan a fd directory, reporting if any of the files references an absolute pathname
 * @param[in] fd_dir_to_scan The directory to scan, which may be at either:
 *            - /proc/pid/fd meaning scanning PIDs
 *            - /proc/pid/task/tid/fd meaning scanning TIDs
 * @param[in] pathname The absolute pathname to check if is referenced
 * @returns Returns true if pathname if referenced within fd_dir_to_scan
 */
static bool scan_fd_dir_for_pathname_matches (const char *const fd_dir_to_scan, const char *const pathname)
{
    bool matched = false;
    DIR *fd_dir;
    struct dirent *fd_ent;
    int rc;
    char fd_symlnk[PATH_MAX];
    char fd_target[PATH_MAX + 1];
    ssize_t fd_target_len;
    volatile size_t num_chars;

    errno = 0;
    fd_dir = opendir (fd_dir_to_scan);
    if (fd_dir != NULL)
    {
        fd_ent = readdir (fd_dir);
        while (!matched && (fd_ent != NULL))
        {
            if (fd_ent->d_type == DT_LNK)
            {
                num_chars = sizeof (fd_symlnk);
                snprintf (fd_symlnk, num_chars, "%s/%s", fd_dir_to_scan, fd_ent->d_name);
                errno = 0;
                fd_target_len = readlink (fd_symlnk, fd_target, sizeof (fd_target) - 1);
                if (fd_target_len > 0)
                {
                    fd_target[fd_target_len] = '\0';
                    matched = strcmp (fd_target, pathname) == 0;
                }
            }

            fd_ent = readdir (fd_dir);
        }

        rc = closedir (fd_dir);
        if (rc != 0)
        {
            printf ("closedir() failed\n");
        }
    }

    return matched;
}


/**
 * @brief Find which PID, if any, is using a specific pathname
 * @details This is to report diagnostics in the event that unable to open an IOMMU group which is already open by a different
 *          process. It only reports the first PID found which is using the pathname
 * @param[in] pathname The absolute pathname to find references to
 * @return Either:
 *         - If -1 failed to find a PID using pathname, which may occur if the user running the program doesn't have permission
 *           to open the fd directory. E.g. when the process with the fd directory is a different user.
 *         - Otherwise the PID which is using pathname
 */
static pid_t find_pid_using_file (const char *const pathname)
{
    pid_t pid_using_file = -1;
    DIR *proc_dir;
    struct dirent *proc_ent;
    DIR *task_dir;
    struct dirent *task_ent;
    int rc;
    pid_t pid;
    pid_t tid;
    char task_dirname[PATH_MAX];
    char fd_dir_to_scan[PATH_MAX];
    char junk;
    volatile size_t num_chars;

    proc_dir = opendir ("/proc");
    if (proc_dir != NULL)
    {
        proc_ent = readdir (proc_dir);
        while ((pid_using_file == -1) && (proc_ent != NULL))
        {
            if ((proc_ent->d_type == DT_DIR) && (sscanf (proc_ent->d_name, "%d%c", &pid, &junk) == 1))
            {
                snprintf (task_dirname, sizeof (task_dirname), "/proc/%s/task", proc_ent->d_name);
                task_dir = opendir (task_dirname);
                if (task_dir != NULL)
                {
                    task_ent = readdir (task_dir);
                    while ((pid_using_file == -1) && (task_ent != NULL))
                    {
                        if ((task_ent->d_type == DT_DIR) && (sscanf (task_ent->d_name, "%d%c", &tid, &junk) == 1))
                        {
                            /* use of num_chars suppresses -Wformat-truncation, as suggested by
                             * https://stackoverflow.com/a/70938456/4207678 */
                            num_chars = sizeof (fd_dir_to_scan);
                            snprintf (fd_dir_to_scan, num_chars, "%s/%s/fd", task_dirname, task_ent->d_name);
                            if (scan_fd_dir_for_pathname_matches (fd_dir_to_scan, pathname))
                            {
                                pid_using_file = pid;
                            }
                        }

                        task_ent = readdir (task_dir);
                    }

                    rc = closedir (task_dir);
                    if (rc != 0)
                    {
                        printf ("closedir() failed\n");
                    }
                }
            }

            proc_ent = readdir (proc_dir);
        }

        rc = closedir (proc_dir);
        if (rc != 0)
        {
            printf ("closedir() failed\n");
        }
    }

    return pid_using_file;
}


/**
 * @brief Receive a message exchanged between the VFIO multi process manager and a client
 * @details This is a blocking call. Assumed to be only call when either:
 *          a. In a client and are waiting for a reply to a request which has been sent.
 *          b. In the manager are waiting for a request.
 * @param[in] socket_fd The socket to receive the message on
 * @param[out] rx_buffer The received messages
 * @param[out] vfio_fds The VFIO device file descriptors received for VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY.
 *                      May be NULL if the caller doesn't expect to receive a VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY
 * @return Returns true if a valid message has been received, where the validation checks the message ID and length.
 *         Returns false if either:
 *         a. The remote end has closed the socket
 *         b. An invalid message was received, in which case a diagnostic message has been displayed.
 */
bool vfio_receive_manage_message (const int socket_fd, vfio_manage_messages_t *const rx_buffer,
                                  vfio_open_device_reply_fds_t *const vfio_fds)
{
    bool valid_message = false;
    ssize_t num_rx_data_bytes;
    struct iovec rx_iovec[1];
    struct msghdr rx_msg = {0};
    char rx_msg_control[1024];
    int saved_errno;
    struct cmsghdr *cmsg;

    rx_iovec[0].iov_base = rx_buffer;
    rx_iovec[0].iov_len = sizeof (*rx_buffer);
    rx_msg.msg_iov = rx_iovec;
    rx_msg.msg_iovlen = 1;
    rx_msg.msg_name = NULL;
    rx_msg.msg_namelen = 0;
    if (vfio_fds != NULL)
    {
        /* Allow ancillary information to receive device file descriptors */
        rx_msg.msg_control = rx_msg_control;
        rx_msg.msg_controllen = sizeof (rx_msg_control);
    }
    else
    {
        /* No ancillary information is expected */
        rx_msg.msg_control = NULL;
        rx_msg.msg_controllen = 0;
    }
    rx_msg.msg_flags = 0;
    errno = 0;
    num_rx_data_bytes = recvmsg (socket_fd, &rx_msg, 0);
    saved_errno = errno;
    if (num_rx_data_bytes == 0)
    {
        /* The remote end closed the socket */
        valid_message = false;
    }
    else if (num_rx_data_bytes < 0)
    {
        printf ("recvmsg() failed : %s\n", strerror (saved_errno));
        valid_message = false;
    }
    else if (num_rx_data_bytes < sizeof (rx_buffer->msg_id))
    {
        printf ("Received message length %zd too short for msg_id\n", num_rx_data_bytes);
        valid_message = false;
    }
    else
    {
       switch (rx_buffer->msg_id)
       {
       case VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST:
           valid_message = num_rx_data_bytes == sizeof (vfio_open_device_request_t);
           break;

       case VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY:
           /* For an open device reply the message if success was reported in the reply then for the message to be considered
            * valid also need to check that the VFIO file descriptors have been received in the ancillary information. */
           valid_message = (num_rx_data_bytes == sizeof (vfio_open_device_reply_t)) && (vfio_fds != NULL);
           if (valid_message && rx_buffer->open_device_reply.success)
           {
               bool found_fds = false;

               vfio_fds->device_fd = -1;
               vfio_fds->container_fd = -1;
               for (cmsg = CMSG_FIRSTHDR (&rx_msg); !found_fds && (cmsg != NULL); cmsg = CMSG_NXTHDR (&rx_msg, cmsg))
               {
                   if ((cmsg->cmsg_level == SOL_SOCKET) && (cmsg->cmsg_type == SCM_RIGHTS))
                   {
                       const size_t num_ancillary_bytes = cmsg->cmsg_len - CMSG_LEN (0);
                       const size_t num_fds_received = num_ancillary_bytes / sizeof (int);
                       if ((num_fds_received == 1) || (num_fds_received == 2))
                       {
                           memcpy (vfio_fds, CMSG_DATA (cmsg), num_ancillary_bytes);
                       }
                       found_fds = true;
                   }
               }

               if (!found_fds)
               {
                   valid_message = false;
               }
           }
           break;

       case VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST:
           valid_message = num_rx_data_bytes == sizeof (vfio_close_device_request_t);
           break;

       case VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REPLY:
           valid_message = num_rx_data_bytes == sizeof (vfio_close_device_reply_t);
           break;

       case VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST:
           valid_message = num_rx_data_bytes == sizeof (vfio_allocate_iova_request_t);
           break;

       case VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REPLY:
           valid_message = num_rx_data_bytes == sizeof (vfio_allocate_iova_reply_t);
           break;

       case VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST:
           valid_message = num_rx_data_bytes == sizeof (vfio_free_iova_request_t);
           break;

       case VFIO_MANAGE_MSG_ID_FREE_IOVA_REPLY:
           valid_message = num_rx_data_bytes == sizeof (vfio_free_iova_reply_t);
           break;

       default:
           valid_message = false;
       }

       if (!valid_message)
       {
           printf ("Invalid message ID %u size %zd bytes\n", rx_buffer->msg_id, num_rx_data_bytes);
       }
    }

    return valid_message;
}


/**
 * @brief In a client receive the reply for a request sent to the manager
 * @param[in] socket_fd The socket to receive the message on
 * @param[out] rx_buffer The received messages
 * @param[out] vfio_fds The VFIO device file descriptors received for VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY.
 *                      May be NULL if the caller doesn't expect to receive a VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY
 * @param[in] expected_reply_msg_id Which type of reply is expected
 */
static void vfio_receive_manage_reply (const int socket_fd, vfio_manage_messages_t *const rx_buffer,
                                       vfio_open_device_reply_fds_t *const vfio_fds,
                                       const vfio_manager_msg_id_t expected_reply_msg_id)
{
    const bool success = vfio_receive_manage_message (socket_fd, rx_buffer, vfio_fds) &&
            (rx_buffer->msg_id == expected_reply_msg_id);

    if (!success)
    {
        printf ("Failed to received expected reply msg_id %d from VFIO multi-process manager\n", expected_reply_msg_id);
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Send a message exchanged between the VFIO multi process manager and a client
 * @details If the send fails a diagnostic message is displayed, but there is no return status.
 *          On the assumption that the send fails due to the remote end closed the socket after crashing, then the next
 *          attempt to read the socket will cause the local end to detect the failure.
 * @param[in] socket_fd The socket to send the message on
 * @param[in] tx_buffer The populated message to send
 * @param[in] vfio_fds When tx_buffer is VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY indicating success, contains the
 *                     VFIO device file descriptors to send as ancillary information.
 */
void vfio_send_manage_message (const int socket_fd, vfio_manage_messages_t *const tx_buffer,
                               vfio_open_device_reply_fds_t *const vfio_fds)
{
    struct iovec tx_iovec[1];
    struct msghdr tx_msg = {0};
    char tx_msg_control[1024];
    int saved_errno;
    struct cmsghdr *cmsg;
    size_t cmsg_space;
    ssize_t num_tx_data_bytes;

    tx_iovec[0].iov_base = tx_buffer;
    switch (tx_buffer->msg_id)
    {
    case VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST:
        tx_iovec[0].iov_len = sizeof (vfio_open_device_request_t);
        break;

    case VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY:
        tx_iovec[0].iov_len = sizeof (vfio_open_device_reply_t);
        break;

    case VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST:
        tx_iovec[0].iov_len = sizeof (vfio_close_device_request_t);
        break;

    case VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REPLY:
        tx_iovec[0].iov_len = sizeof (vfio_close_device_reply_t);
        break;

    case VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST:
        tx_iovec[0].iov_len = sizeof (vfio_allocate_iova_request_t);
        break;

    case VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REPLY:
        tx_iovec[0].iov_len = sizeof (vfio_allocate_iova_reply_t);
        break;

    case VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST:
        tx_iovec[0].iov_len = sizeof (vfio_free_iova_request_t);
        break;

    case VFIO_MANAGE_MSG_ID_FREE_IOVA_REPLY:
        tx_iovec[0].iov_len = sizeof (vfio_free_iova_reply_t);
        break;
    }
    tx_msg.msg_iov = tx_iovec;
    tx_msg.msg_iovlen = 1;
    tx_msg.msg_name = NULL;
    tx_msg.msg_namelen = 0;
    if (vfio_fds != NULL)
    {
        /* The number of bytes for the file descriptors depends upon which are valid */
        const size_t num_bytes_of_fds = (vfio_fds->container_fd != -1) ?
                sizeof (*vfio_fds) : offsetof (vfio_open_device_reply_fds_t, container_fd);

        /* Populate ancillary information with the device file descriptors to send */
        tx_msg.msg_control = tx_msg_control;
        tx_msg.msg_controllen = sizeof (tx_msg_control);
        cmsg_space = 0;
        cmsg = CMSG_FIRSTHDR (&tx_msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN (num_bytes_of_fds);
        memcpy (CMSG_DATA (cmsg), vfio_fds, num_bytes_of_fds);
        cmsg_space += CMSG_SPACE (num_bytes_of_fds);
        tx_msg.msg_controllen = cmsg_space;
    }
    else
    {
        /* No ancillary information is sent */
        tx_msg.msg_control = NULL;
        tx_msg.msg_controllen = 0;
    }
    tx_msg.msg_flags = 0;

    errno = 0;
    num_tx_data_bytes = sendmsg (socket_fd, &tx_msg, 0);
    saved_errno = errno;
    if (num_tx_data_bytes != tx_iovec[0].iov_len)
    {
        printf ("sendmsg() only sent %zd out of %zu bytes : %s\n", num_tx_data_bytes, tx_iovec[0].iov_len, strerror (saved_errno));
    }
}


/**
 * @brief Obtain the information for one region of a VFIO device
 * @details This may be called multiple times for the same region, and and so effect if the region information has already
 *          been obtained.
 * @param[in/out] vfio_device The VFIO device to obtain the region information for
 * @param[in] region_index Which region to obtain the information for
 */
void get_vfio_device_region (vfio_device_t *const vfio_device, const uint32_t region_index)
{
    int rc;

    if (!vfio_device->regions_info_populated[region_index])
    {
        struct vfio_region_info *const region_info = &vfio_device->regions_info[region_index];

        /* Get region information for PCI BAR, to determine if an implemented BAR which can be mapped */
        memset (region_info, 0, sizeof (*region_info));
        region_info->argsz = sizeof (*region_info);
        region_info->index = region_index;
        rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_GET_REGION_INFO, region_info);
        if (rc == 0)
        {
            vfio_device->regions_info_populated[region_index] = true;
        }
        else
        {
            printf ("VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
        }
    }
}


/**
 * @brief Attempt to map a memory BAR for a VFIO device before use.
 * @details This may be called multiple times for the same BAR, and has no effect if the BAR is already mapped.
 *          On return vfio_device->mapped_bars[bar_index] is non-NULL if the BAR has been mapped into the virtual
 *          address space of the calling process.
 *          vfio_device->mapped_bars[bar_index] will be NULL if the BAR is not implemented on the VFIO device.
 * @param[in/out] vfio_device The VFIO device to map a BAR for
 * @param[in] bar_index Which BAR on the VFIO device to map
 */
void map_vfio_device_bar_before_use (vfio_device_t *const vfio_device, const uint32_t bar_index)
{
    void *addr;

    if (vfio_device->mapped_bars[bar_index] == NULL)
    {
        /* Get region information for PCI BAR, to determine if an implemented BAR which can be mapped */
        get_vfio_device_region (vfio_device, bar_index);

        struct vfio_region_info *const region_info = &vfio_device->regions_info[bar_index];

        if ((region_info->size > 0) && ((region_info->flags & VFIO_REGION_INFO_FLAG_MMAP) != 0))
        {
            /* Map the entire BAR */
            addr = mmap (NULL, region_info->size, PROT_READ | PROT_WRITE, MAP_SHARED, vfio_device->device_fd, (off_t) region_info->offset);
            if (addr == MAP_FAILED)
            {
                printf ("mmap() failed : %s\n", strerror (errno));
                return;
            }
            vfio_device->mapped_bars[bar_index] = addr;
        }
        else
        {
            vfio_device->mapped_bars[bar_index] = NULL;
        }
    }
}


/**
 * @brief Return a mapping for a block of registers
 * @param[in/out] vfio_device The VFIO device to map the registers for.
 * @param[in] bar_index Which BAR on the vfio_device contains the block of registers.
 * @param[in] base_offset The base offset in bytes into the BAR for the block of registers.
 * @param[in] frame_size The decoded frame size of the block of registers, used to check the BAR is large enough
 * @return When non-NULL the local process mapping for the start of the block of registers.
 *         Returns NULL if the BAR doesn't contains the requested block of registers.
 */
uint8_t *map_vfio_registers_block (vfio_device_t *const vfio_device, const uint32_t bar_index,
                                   const size_t base_offset, const size_t frame_size)
{
    uint8_t *mapped_registers = NULL;

    map_vfio_device_bar_before_use (vfio_device, bar_index);
    if (vfio_device->mapped_bars[bar_index] != NULL)
    {
        if (vfio_device->regions_info[bar_index].size >= (base_offset + frame_size))
        {
            mapped_registers = &vfio_device->mapped_bars[bar_index][base_offset];
        }
    }

    return mapped_registers;
}


/**
 * @brief Reset a VFIO device
 * @details With the Xilinx "DMA/Bridge Subsystem for PCI Express" PG195 the configuration registers are shown to be
 *          reset to zero when probe_xilinx_ip runs even when this function isn't called.
 *          Looking at kernel-4.18.0-425.3.1.el8/linux-4.18.0-425.3.1.el8.x86_64/drivers/vfio/pci/vfio_pci.c
 *          vfio_pci_open() ends up calling pci_try_reset_function(), so think the VFIO device is reset every time
 *          it is opened by user space.
 * @param[in/out] vfio_device The device to reset
 */
void reset_vfio_device (vfio_device_t *const vfio_device)
{
    int rc;
    int saved_errno;

    /* Check the device information to determine if reset is support */
    if ((vfio_device->device_info.flags & VFIO_DEVICE_FLAGS_RESET) != 0)
    {
        errno = 0;
        rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_RESET);
        saved_errno = errno;
        if (rc == 0)
        {
            printf ("Reset VFIO device %s\n", vfio_device->device_name);
        }
        else
        {
            printf ("VFIO_DEVICE_RESET %s failed : %s\n", vfio_device->device_name, strerror (saved_errno));
        }
    }
    else
    {
        printf ("VFIO device %s doesn't support reset\n", vfio_device->device_name);
    }
}


/**
 * @brief qsort comparison function for IOVA regions, which compares the start values
 */
static int iova_region_compare (const void *const compare_a, const void *const compare_b)
{
    const vfio_iova_region_t *const region_a = compare_a;
    const vfio_iova_region_t *const region_b = compare_b;

    if (region_a->start < region_b->start)
    {
        return -1;
    }
    else if (region_a->start == region_b->start)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


/**
 * @brief Append a new IOVA region to the end of array.
 * @details This is a helper function which can leave the iova_regions[] un-sorted
 * @param[in/out] container Contains the IOVA regions to modify
 * @param[in] new_region The new IOVA region to append
 */
static void append_iova_region (vfio_iommu_container_t *const container, const vfio_iova_region_t *const new_region)
{
    const uint32_t grow_length = 64;

    /* Increase the allocated length of the array to ensure has space for the region */
    if (container->num_iova_regions == container->iova_regions_allocated_length)
    {
        container->iova_regions_allocated_length += grow_length;
        container->iova_regions = realloc (container->iova_regions,
                container->iova_regions_allocated_length * sizeof (container->iova_regions[0]));
        if (container->iova_regions == NULL)
        {
            fprintf (stderr, "Failed to allocate memory for iova_regions\n");
            exit (EXIT_FAILURE);
        }
    }

    /* Append the new region to the end of the array. May not be IOVA order, is sorted later */
    container->iova_regions[container->num_iova_regions] = *new_region;
    container->num_iova_regions++;
}


/**
 * @brief Remove one IOVA region, shuffling down the following entries in the array
 * @param[in/out] container Contains the IOVA regions to modify
 * @param[in] region_index Identifies which region to remove
 */
static void remove_iova_region (vfio_iommu_container_t *const container, const uint32_t region_index)
{
    const uint32_t num_regions_to_shuffle = container->num_iova_regions - region_index;

    memmove (&container->iova_regions[region_index], &container->iova_regions[region_index + 1],
            sizeof (container->iova_regions[region_index]) * num_regions_to_shuffle);
    container->num_iova_regions--;
}


/**
 * @brief Update the array of IOVA regions with a new region.
 * @brief The new region can either:
 *        a. Add a free region at initialisation.
 *        b. Mark a region as allocated. This may split an existing free region.
 *        c. Free a previously allocated region. This may combine adjacent free regions.
 * @param[in/out] container Contains the IOVA regions to update
 * @param[in] new_region Defines the new region
 */
void update_iova_regions (vfio_iommu_container_t *const container, const vfio_iova_region_t *const new_region)
{
    bool new_region_processed = false;
    uint32_t region_index;

    /* Search for which existing region the region overlaps */
    for (region_index = 0; !new_region_processed && (region_index < container->num_iova_regions); region_index++)
    {
        /* Take a copy of the existing region, since append_iova_region() may change the pointers */
        const vfio_iova_region_t existing_region = container->iova_regions[region_index];

        if ((new_region->start >= existing_region.start) && (new_region->end <= existing_region.end))
        {
            /* Remove the existing region */
            remove_iova_region (container, region_index);

            /* If the new region doesn't start at the beginning of the existing region, then append a region
             * with the same allocated state as the existing region to fill up to the start of the new region */
            if (new_region->start > existing_region.start)
            {
                const vfio_iova_region_t before_region =
                {
                    .start = existing_region.start,
                    .end = new_region->start - 1,
                    .allocated = existing_region.allocated,
                    .allocating_client_id = existing_region.allocating_client_id
                };

                append_iova_region (container, &before_region);
            }

            /* Append the new region */
            append_iova_region (container, new_region);

            /* If the new region ends before that of the existing region, then append a region with the same
             * allocated state as the existing region to fill up to the end of the existing region */
            if (new_region->end < existing_region.end)
            {
                const vfio_iova_region_t after_region =
                {
                    .start = new_region->end + 1,
                    .end = existing_region.end,
                    .allocated = existing_region.allocated,
                    .allocating_client_id = existing_region.allocating_client_id
                };

                append_iova_region (container, &after_region);
            }

            new_region_processed = true;
        }
    }

    if (!new_region_processed)
    {
        /* The new region doesn't overlap an existing region. Must be inserting free regions at initialisation */
        append_iova_region (container, new_region);
    }

    /* Sort the IOVA regions into ascending start order */
    qsort (container->iova_regions, container->num_iova_regions, sizeof (container->iova_regions[0]), iova_region_compare);

    /* Combine adjacent IOVA regions which have the same allocated state */
    region_index = 0;
    while ((region_index + 1) < container->num_iova_regions)
    {
        vfio_iova_region_t *const this_region = &container->iova_regions[region_index];
        const uint32_t next_region_index = region_index + 1;
        const vfio_iova_region_t *const next_region = &container->iova_regions[next_region_index];
        const bool compare_client_id = this_region->allocated &&
                (container->vfio_devices->devices_usage == VFIO_DEVICES_USAGE_MANAGER);
        bool regions_can_be_combined =
                ((this_region->end + 1) == next_region->start) && (this_region->allocated == next_region->allocated);

        if (compare_client_id)
        {
            regions_can_be_combined = regions_can_be_combined &&
                    (this_region->allocating_client_id == next_region->allocating_client_id);
        }

        if (regions_can_be_combined)
        {
            this_region->end = next_region->end;
            remove_iova_region (container, next_region_index);
        }
        else if (this_region->end >= next_region->start)
        {
            /* Bug if the adjacent IOVA regions are overlapping */
            fprintf (stderr, "Adjacent iova_regions overlap\n");
            exit (EXIT_FAILURE);
        }
        else
        {
            region_index++;
        }
    }
}


/**
 * @brief Attempt to perform an IOVA allocation, by searching the free IOVA regions
 * @param[in] container The container to use for the IOVA allocation
 * @param[in] dma_capability Indicates if the allocation is for a 64-bit IOVA capable device
 * @param[in] min_start Minimum start IOVA to use for the allocation.
 *                      May be non-zero to cause a 64-bit IOVA capable device to initially avoid the
 *                      first 4 GiB IOVA space.
 * @param[in] aligned_size The size of the IOVA allocation required
 * @param[in] allocating_client_id For the manager used to identify which client is performing the allocation
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
static void attempt_iova_allocation (const vfio_iommu_container_t *const container,
                                     const vfio_device_dma_capability_t dma_capability,
                                     const uint64_t min_start, const size_t aligned_size,
                                     const uint32_t allocating_client_id,
                                     vfio_iova_region_t *const region)
{
    const uint64_t max_a32_end = 0xffffffffUL;
    size_t min_unused_space;

    /* Search for the smallest existing free region in which the aligned size will fit,
     * to try and reduce running out of IOVA addresses due to fragmentation. */
    min_unused_space = 0;
    for (uint32_t region_index = 0; region_index < container->num_iova_regions; region_index++)
    {
        const vfio_iova_region_t *const existing_region = &container->iova_regions[region_index];

        if (existing_region->allocated)
        {
            /* Skip this region, as not free */
        }
        else if (existing_region->end < min_start)
        {
            /* Skip this region, as all of it is below the minimum start */
        }
        else if ((dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A32) && (existing_region->start > max_a32_end))
        {
            /* Skip this region, as all of it is above what can be addressed the device which is only 32-bit capable */
        }
        else
        {
            /* Limit the usable start for the region to the minimum */
            const uint64_t usable_region_start = (existing_region->start >= min_start) ? existing_region->start : min_start;

            /* When the device is only 32-bit IOVA capable limit the end to the first 4 GiB */
            const uint64_t usable_region_end =
                    ((dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A32) && (max_a32_end < existing_region->end)) ?
                            max_a32_end : existing_region->end;

            const uint64_t usable_region_size = (usable_region_end + 1) - usable_region_start;

            if (usable_region_size >= aligned_size)
            {
                const uint64_t region_unused_space = usable_region_size - aligned_size;

                if (!region->allocated || (region_unused_space < min_unused_space))
                {
                    region->start = usable_region_start;
                    region->end = region->start + (aligned_size - 1);
                    region->allocating_client_id = allocating_client_id;
                    region->allocated = true;
                    min_unused_space = region_unused_space;
                }
            }
        }
    }
}


/**
 * @brief Allocate a IOVA region for use by a DMA mapping for a device, where the local process directly performs the allocation.
 * @param[in/out] container The container to use for the IOVA allocation
 * @param[in] dma_capability Indicates if the allocation is for a 64-bit IOVA capable device
 * @param[in] requested_size The requested size in bytes to allocate.
 *                           The actual size allocated may be increased to allow for the supported IOVA page sizes.
 * @param[in] allocating_client_id For the manager used to identify which client is performing the allocation
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
void allocate_iova_region_direct (vfio_iommu_container_t *const container,
                                  const vfio_device_dma_capability_t dma_capability,
                                  const size_t requested_size,
                                  const uint32_t allocating_client_id,
                                  vfio_iova_region_t *const region)
{
    /* Default to no allocation */
    region->start = 0;
    region->end = 0;
    region->allocating_client_id = 0;
    region->allocated = false;

    /* Increase the requested size to be aligned to the smallest page size supported by the IOMMU */
    bool alignment_found = false;
    size_t aligned_size = requested_size;
    for (uint64_t page_size = 1; (!alignment_found) && (page_size != 0); page_size <<= 1)
    {
        if ((container->iommu_info->iova_pgsizes & page_size) == page_size)
        {
            aligned_size = (requested_size + (page_size - 1)) & (~(page_size - 1));
            alignment_found = true;
        }
    }

    if (dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A64)
    {
        /* For 64-bit IOVA capable devices first attempt to allocate IOVA above the first 4 GiB,
         * to try and keep the first 4 GiB for devices which are only 32-bit IOVA capable. */
        const uint64_t a64_min_start = 0x100000000UL;

        attempt_iova_allocation (container, dma_capability, a64_min_start, aligned_size, allocating_client_id, region);
    }

    /* If allocation wasn't successful, or only a 32-bit IOVA capable device, try the allocation with no minimum start */
    if (!region->allocated)
    {
        attempt_iova_allocation (container, dma_capability, 0, aligned_size, allocating_client_id, region);
    }

    if (region->allocated)
    {
        /* Record the region as now allocated */
        update_iova_regions (container, region);
    }
    else
    {
        /* Report a diagnostic message that the allocation failed */
        printf ("No free IOVA to allocate %zu bytes for %d bit IOVA capable device\n",
                aligned_size, (dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A64) ? 64 : 32);
    }
}


/**
 * @brief Allocate a IOVA region for use by a DMA mapping for a device, where communicates with the manager to make the allocation
 * @param[in] container The container to use for the IOVA allocation
 * @param[in] dma_capability Indicates if the allocation is for a 64-bit IOVA capable device
 * @param[in] requested_size The requested size in bytes to allocate.
 *                           The actual size allocated may be increased to allow for the supported IOVA page sizes.
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
static void allocate_iova_region_indirect (const vfio_iommu_container_t *const container,
                                           const vfio_device_dma_capability_t dma_capability,
                                           const size_t requested_size,
                                           vfio_iova_region_t *const region)
{
    vfio_manage_messages_t tx_buffer;
    vfio_manage_messages_t rx_buffer;

    /* Send the request */
    tx_buffer.allocate_iova_request.msg_id = VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST;
    tx_buffer.allocate_iova_request.dma_capability = dma_capability;
    tx_buffer.allocate_iova_request.container_id = container->container_id;
    tx_buffer.allocate_iova_request.requested_size = requested_size;
    vfio_send_manage_message (container->vfio_devices->manager_client_socket_fd, &tx_buffer, NULL);

    /* Wait for the reply */
    vfio_receive_manage_reply (container->vfio_devices->manager_client_socket_fd, &rx_buffer, NULL,
            VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REPLY);
    region->allocated = rx_buffer.allocate_iova_reply.success;
    region->start = rx_buffer.allocate_iova_reply.start;
    region->end = rx_buffer.allocate_iova_reply.end;
    region->allocating_client_id = 0;
}


/**
 * @brief Get the capabilities for a type 1 IOMMU, at the container level, to be used to perform IOVA allocations
 * @param[in/out] container The container being opened
 * @return Returns true if have obtained the capabilities, or false upon error.
 */
static bool get_type1_iommu_capabilities (vfio_iommu_container_t *const container)
{
    int rc;
    struct vfio_iommu_type1_info iommu_info_get_size;

    /* Determine the size required to get the capabilities for the IOMMU.
     * This updates the argsz to indicate how much space is required. */
    memset (&iommu_info_get_size, 0, sizeof iommu_info_get_size);
    iommu_info_get_size.argsz = sizeof (iommu_info_get_size);
    rc = ioctl (container->container_fd, VFIO_IOMMU_GET_INFO, &iommu_info_get_size);
    if (rc != 0)
    {
        printf ("VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
        return false;
    }

    /* Allocate a structure of the required size for the IOMMU information, and get it */
    container->iommu_info = calloc (iommu_info_get_size.argsz, 1);
    if (container->iommu_info == NULL)
    {
        printf ("Failed to allocate %" PRIu32 " bytes for iommu_info\n", iommu_info_get_size.argsz);
        return false;
    }
    container->iommu_info->argsz = iommu_info_get_size.argsz;
    rc = ioctl (container->container_fd, VFIO_IOMMU_GET_INFO, container->iommu_info);
    if (rc != 0)
    {
        printf ("  VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
        return false;
    }

    /* Initialise the free IOVA ranges to the valid IOVA ranges reported by the capabilities.
     * If VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE isn't present, then any attempt to call allocate_vfio_container_dma_mapping()
     * will fail. */
    if (((container->iommu_info->flags & VFIO_IOMMU_INFO_CAPS) != 0) && (container->iommu_info->cap_offset > 0))
    {
        const char *const info_start = (const char *) container->iommu_info;
        __u32 cap_offset = container->iommu_info->cap_offset;

        while ((cap_offset > 0) && (cap_offset < container->iommu_info->argsz))
        {
            const struct vfio_info_cap_header *const cap_header =
                    (const struct vfio_info_cap_header *) &info_start[cap_offset];

            if (cap_header->id == VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE)
            {
                const struct vfio_iommu_type1_info_cap_iova_range *const cap_iova_range =
                        (const struct vfio_iommu_type1_info_cap_iova_range *) cap_header;

                for (uint32_t iova_index = 0; iova_index < cap_iova_range->nr_iovas; iova_index++)
                {
                    const struct vfio_iova_range *const iova_range = &cap_iova_range->iova_ranges[iova_index];
                    const vfio_iova_region_t new_region =
                    {
                        .start = iova_range->start,
                        .end = iova_range->end,
                        .allocating_client_id = 0,
                        .allocated = false
                    };

                    update_iova_regions (container, &new_region);
                }
            }

            cap_offset = cap_header->next;
        }
    }

    return true;
}


/**
 * @brief Get the IOMMU group for a PCI device
 * @details Uses conditional compilation to allow for the version of the pciutils library not supporting PCI_FILL_IOMMU_GROUP.
 *          If pciutils doesn't support PCI_FILL_IOMMU_GROUP, then falls back to reading the Linux sysfs directly.
 * @param[in/out] pci_dev The PCI device to get the IOMMU group for
 * @return The IOMMU group as a numeric string if non-NULL, or NULL if the IOMMU group isn't defined
 */
char *vfio_get_iommu_group (struct pci_dev *const pci_dev)
{
    char *iommu_group = NULL;
#ifdef PCI_FILL_IOMMU_GROUP
    const int known_fields = pci_fill_info (pci_dev, PCI_FILL_IOMMU_GROUP);

    if ((known_fields & PCI_FILL_IOMMU_GROUP) == PCI_FILL_IOMMU_GROUP)
    {
        iommu_group = pci_get_string_property (pci_dev, PCI_FILL_IOMMU_GROUP);
    }
#else
    iommu_group = pci_sysfs_read_device_symlink_name ((uint32_t) pci_dev->domain, pci_dev->bus, pci_dev->dev, pci_dev->func,
            "iommu_group");
#endif

    return iommu_group;
}


/**
 * @brief Search for an IOMMU group which is already open
 * @param[in] vfio_devices Contains the IOMMU groups which have already been opened.
 * @param[in] iommu_group_name The name of the IOMMU group to find
 * @return If non-NULL the existing opened IOMMU group
 *         If NULL the IOMMU group is not already opened
 */
static vfio_iommu_group_t *find_open_iommu_group (vfio_devices_t *const vfio_devices, const char *const iommu_group_name)
{
    vfio_iommu_group_t *iommu_group = NULL;

    for (uint32_t container_index = 0; (iommu_group == NULL) && (container_index < vfio_devices->num_containers); container_index++)
    {
        vfio_iommu_container_t *const container = &vfio_devices->containers[container_index];

        for (uint32_t group_index = 0; (iommu_group == NULL) && (group_index < container->num_iommu_groups); group_index++)
        {
            if (strcmp (container->iommu_groups[group_index].iommu_group_name, iommu_group_name) == 0)
            {
                iommu_group = &container->iommu_groups[group_index];
            }
        }
    }

    return iommu_group;
}


/**
 * @brief Create an IOMMU container, which has no IOMMU groups
 * @param[in/out] vfio_devices The list of vfio devices to append the created container to.
 * @return The IOMMU container which has been created, and append to vfio_devices
 */
static vfio_iommu_container_t *vfio_iommu_create_container (vfio_devices_t *const vfio_devices)
{
    int rc;
    int saved_errno;
    int api_version;

    if (vfio_devices->num_containers == MAX_VFIO_DEVICES)
    {
        /* This shouldn't happen since the caller should have checked for exceeding the maximum number of devices */
        fprintf (stderr, "No free containers\n");
        exit (EXIT_FAILURE);
    }

    /* Initialise to an empty container */
    vfio_iommu_container_t *const container = &vfio_devices->containers[vfio_devices->num_containers];
    container->container_id = vfio_devices->num_containers;
    container->num_iommu_groups = 0;
    container->num_iova_regions = 0;
    container->iova_regions_allocated_length = 0;
    container->iova_regions = NULL;
    container->vfio_devices = vfio_devices;

    /* Sanity check that the VFIO container path exists, and the user has access */
    errno = 0;
    rc = faccessat (0, VFIO_CONTAINER_PATH, R_OK | W_OK, AT_EACCESS);
    saved_errno = errno;
    if (rc != 0)
    {
        if (saved_errno == ENOENT)
        {
            fprintf (stderr, "%s doesn't exist, implying no VFIO support\n", VFIO_CONTAINER_PATH);
            exit (EXIT_SUCCESS);
        }
        else if (saved_errno == EACCES)
        {
            /* The act of loading the vfio-pci driver should give user access to the VFIO container */
            fprintf (stderr, "No permission on %s, implying no vfio-pci driver loaded\n", VFIO_CONTAINER_PATH);
            exit (EXIT_FAILURE);
        }
        else
        {
            fprintf (stderr, "faccessat (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
            exit (EXIT_FAILURE);
        }
    }

    container->container_fd = open (VFIO_CONTAINER_PATH, O_RDWR);
    if (container->container_fd == -1)
    {
        fprintf (stderr, "open (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }

    api_version = ioctl (container->container_fd, VFIO_GET_API_VERSION);
    if (api_version != VFIO_API_VERSION)
    {
        fprintf (stderr, "Got VFIO_API_VERSION %d, expected %d\n", api_version, VFIO_API_VERSION);
        exit (EXIT_FAILURE);
    }

    /* Determine the type of IOMMU to use.
     * If VFIO_NOIOMMU_IOMMU is supported use that, otherwise default to VFIO_TYPE1_IOMMU.
     *
     * While support for VFIO_TYPE1v2_IOMMU and VFIO_TYPE1_NESTING_IOMMU was indicated as available
     * on the Intel Xeon W system tested, as for different IOMMU types:
     * 1. DPDK uses VFIO_TYPE1_IOMMU if supported, otherwise VFIO_NOIOMMU_IOMMU.
     * 2. In the Kernel drivers/vfio/vfio_iommu_type1.c using VFIO_TYPE1v2_IOMMU enabled v2 support which:
     *    a. Enables pining or unpining of pages. This seems to be internal functionality not exposed in the VFIO API.
     *    b. Enables VFIO_IOMMU_DIRTY_PAGES. This vfio_access library currently has no use for dirty page tracking.
     *
     *    Temporarily tried using VFIO_TYPE1v2_IOMMU and DMA could still be be successfully used.
     * 3. In the Kernel drivers/vfio/vfio_iommu_type1.c using VFIO_TYPE1_NESTING_IOMMU:
     *    a. Enables support for nesting.
     *    b. Enables v2 support.
     *
     *    Temporarily tried using VFIO_TYPE1_NESTING_IOMMU but VFIO_SET_IOMMU failed with EPERM.
     *    Didn't trace what caused EPERM.
     */
    const __u32 extension = VFIO_NOIOMMU_IOMMU;
    const int extension_supported = ioctl (container->container_fd, VFIO_CHECK_EXTENSION, extension);
    container->iommu_type = extension_supported ? VFIO_NOIOMMU_IOMMU : VFIO_TYPE1_IOMMU;

    vfio_devices->num_containers++;

    return container;
}


/**
 * @brief Open an IOMMU group, storing it in the container.
 * @details For the first IOMMU group in a container also sets the IOMMU type of the contain and obtained the IOMMU
 *          capabilities.
 * @param[in/out] container The container to place the opened IOMMU group in.
 * @param[in] iommu_group_name The name of the IOMMU group to open
 * @param[in] device_description Name of the device for which the IOMMU group is being opened for. Used to report diagnostic
 *            information if unable to open the IOMMU group
 * @return Indicates the status of opening the IOMMU group:
 *         - When non-NULL the IOMMU group has been opened, and points at the IOMMU in the container
 *         - When NULL were unable to open the IOMMU group, and a diagnostic message has been reported.
 */
static vfio_iommu_group_t *vfio_open_iommu_group (vfio_iommu_container_t *const container, const char *const iommu_group_name,
                                                  const char *const device_description)
{
    int rc;
    int saved_errno;

    if (container->num_iommu_groups == MAX_VFIO_DEVICES)
    {
        /* This shouldn't happen since the caller should have checked for exceeding the maximum number of devices */
        printf ("Skipping %s as no free IOMMU groups\n", device_description);
        return NULL;
    }

    vfio_iommu_group_t *const group = &container->iommu_groups[container->num_iommu_groups];
    group->container = container;
    group->iommu_group_name = iommu_group_name;

    /* Sanity check that the IOMMU group file exists and the effective user ID has read/write permission before attempting
     * to probe the device. This checks that a script been run to bind the vfio-pci driver
     * (which creates the IOMMU group file) and has given the user permission. */
    snprintf (group->group_pathname, sizeof (group->group_pathname), "%s%s%s",
            VFIO_ROOT_PATH, (group->container->iommu_type == VFIO_NOIOMMU_IOMMU) ? "noiommu-" : "", group->iommu_group_name);
    errno = 0;
    rc = faccessat (0, group->group_pathname, R_OK | W_OK, AT_EACCESS);
    saved_errno = errno;
    if (rc != 0)
    {
        /* Sanity check failed, reported diagnostic message and return to skip the device */
        if (saved_errno == ENOENT)
        {
            printf ("Skipping %s as %s doesn't exist implying vfio-pci driver not bound to the device\n",
                    device_description, group->group_pathname);
        }
        else if (saved_errno == EACCES)
        {
            printf ("Skipping %s as %s doesn't have read/write permission\n", device_description, group->group_pathname);
        }
        else
        {
            printf ("Skipping %s as %s : %s\n", device_description, group->group_pathname, strerror (saved_errno));
        }
        return NULL;
    }

    /* Need to open the IOMMU group */
    printf ("Opening %s with IOMMU group %s\n", device_description, group->iommu_group_name);
    errno = 0;
    group->group_fd = open (group->group_pathname, O_RDWR);
    saved_errno = errno;
    if (group->group_fd == -1)
    {
        const pid_t pid_using_file = find_pid_using_file (group->group_pathname);

        if ((saved_errno == EBUSY) && (pid_using_file != -1))
        {
            /* If the IOMMU group is already open by another process display the process executable and PID for
             * diagnostic information. */
            char pid_exe_symlink[PATH_MAX];
            char exe_pathname[PATH_MAX];
            ssize_t exe_pathname_len;

            snprintf (pid_exe_symlink, sizeof (pid_exe_symlink), "/proc/%d/exe", pid_using_file);
            exe_pathname_len = readlink (pid_exe_symlink, exe_pathname, sizeof (exe_pathname) - 1);
            if (exe_pathname_len > 0)
            {
                exe_pathname[exe_pathname_len] = '\0';
                printf ("Unable to open %s as is already open by %s PID %d\n",
                        group->group_pathname, exe_pathname, pid_using_file);
            }
            else
            {
                printf ("Unable to open %s as is already open by PID %d\n", group->group_pathname, pid_using_file);
            }
        }
        else if ((saved_errno == EPERM) && (group->container->iommu_type == VFIO_NOIOMMU_IOMMU))
        {
            /* With a noiommu group permission on the group file isn't sufficient.
             * Need to sys_rawio capability to open the group. */
            char executable_path[PATH_MAX];
            ssize_t executable_path_len;

            executable_path_len = readlink ("/proc/self/exe", executable_path, sizeof (executable_path) - 1);
            if (executable_path_len != -1)
            {
                executable_path[executable_path_len] = '\0';
            }
            else
            {
                snprintf (executable_path, sizeof (executable_path), "<executable>");
            }
            printf ("  No permission to open %s. Try:\nsudo setcap cap_sys_rawio=ep %s\n",
                    group->group_pathname, executable_path);
        }
        else
        {
            printf ("open (%s) failed : %s\n", group->group_pathname, strerror (errno));
        }
        return NULL;
    }

    /* Get the status of the group and check that viable */
    memset (&group->group_status, 0, sizeof (group->group_status));
    group->group_status.argsz = sizeof (group->group_status);
    rc = ioctl (group->group_fd, VFIO_GROUP_GET_STATUS, &group->group_status);
    if (rc != 0)
    {
        printf ("FIO_GROUP_GET_STATUS failed : %s\n", strerror (-rc));
        return NULL;
    }

    if ((group->group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0)
    {
        printf ("group is not viable (ie, not all devices bound for vfio)\n");
        return NULL;
    }

    /* Need to add the group to a container before further IOCTLs are possible */
    if ((group->group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) == 0)
    {
        rc = ioctl (group->group_fd, VFIO_GROUP_SET_CONTAINER, &group->container->container_fd);
        if (rc != 0)
        {
            printf ("VFIO_GROUP_SET_CONTAINER failed : %s\n", strerror (-rc));
            return NULL;
        }
    }

    /* Set the IOMMU type used for the container, before opening the first IOMMU group */
    if (group->container->num_iommu_groups == 0)
    {
        rc = ioctl (group->container->container_fd, VFIO_SET_IOMMU, group->container->iommu_type);
        if (rc != 0)
        {
            printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (-rc));
            return NULL;
        }
    }

    if (group->container->num_iommu_groups == 0)
    {
        /* When using a type 1 IOMMU, get the capabilities */
        group->container->iommu_info = NULL;
        if (group->container->iommu_type == VFIO_TYPE1_IOMMU)
        {
            if (!get_type1_iommu_capabilities (group->container))
            {
                exit (EXIT_FAILURE);
            }
        }
    }

    group->container->num_iommu_groups++;

    return group;
}


/**
 * @brief If a VFIO device has DMA capability, ensure bus mastering is enabled
 * @param[in/out] device The VFIO device being opened.
 */
void enable_bus_master_for_dma (vfio_device_t *const device)
{
    if (device->dma_capability != VFIO_DEVICE_DMA_CAPABILITY_NONE)
    {
        /* Ensure the VFIO device is enabled as a PCI bus master */
        uint16_t command;

        if (vfio_read_pci_config_u16 (device, PCI_COMMAND, &command))
        {
            if ((command & PCI_COMMAND_MASTER) == 0)
            {
                command |= PCI_COMMAND_MASTER;
                if (vfio_write_pci_config_u16 (device, PCI_COMMAND, command))
                {
                    printf ("Enabled bus master for %s\n", device->device_name);
                }
            }
        }
    }
}


/**
 * @brief Get the device information.
 * @details As this program is written for a PCI device which has fixed enumerations for regions,
 *          performs a sanity check that VFIO reports a PCI device.
 * @param[in/out] new_device The VFIO device to get the information for.
 * @return Returns true if the device information was obtained, or false if an error
 */
static bool get_vfio_device_info (vfio_device_t *const new_device)
{
    int rc;

    memset (&new_device->device_info, 0, sizeof (new_device->device_info));
    new_device->device_info.argsz = sizeof (new_device->device_info);
    rc = ioctl (new_device->device_fd, VFIO_DEVICE_GET_INFO, &new_device->device_info);
    if (rc != 0)
    {
        printf ("VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
        return false;
    }

    if ((new_device->device_info.flags & VFIO_DEVICE_FLAGS_PCI) == 0)
    {
        printf ("VFIO_DEVICE_GET_INFO flags don't report a PCI device\n");
        return false;
    }

    return true;
}


/**
 * @brief Open a VFIO device file descriptor, also enabling as a bus master if DMA capability is required
 * @param[in/out] new_device The VFIO device being opened.
 * @return Returns true if the VFIO device was opened, or false if an error
 */
bool open_vfio_device_fd (vfio_device_t *const new_device)
{
    /* Open the device */
    new_device->device_fd = ioctl (new_device->group->group_fd, VFIO_GROUP_GET_DEVICE_FD, new_device->device_name);
    if (new_device->device_fd < 0)
    {
        fprintf (stderr, "VFIO_GROUP_GET_DEVICE_FD (%s) failed : %s\n", new_device->device_name, strerror (-new_device->device_fd));
        return false;
    }

    /* Get the device information.  */
    if (!get_vfio_device_info (new_device))
    {
        return false;
    }

    enable_bus_master_for_dma (new_device);

    return true;
}


/**
 * @brief Ensure an IOMMU group is opened with direct access in the calling process, to access a VFIO device
 * @details This:
 *          a. Creates an IOMMU container if required.
 *          b. When multiple devices are in the same IOMMU group can re-use an existing open IOMMU group.
 * @param[in/out] vfio_devices The context for vfio devices which are being opened, used to create containers
 * @param[in/out] new_device Which device to open the IOMMU group for
 *                On return group will be:
 *                - non-NULL if the IOMMU group was opened
 *                - NULL if failed to open the IOMMU group, after reporting a diagnostic message
 * @param[in] iommu_group_name The name of the IOMMU group to open
 */
static void open_group_with_direct_access (vfio_devices_t *const vfio_devices, vfio_device_t *const new_device,
                                           const char *const iommu_group_name)
{
    vfio_iommu_container_t *container = NULL;

    /* If the IOMMU group has already been opened, use it */
    new_device->group = find_open_iommu_group (vfio_devices, iommu_group_name);

    if (new_device->group == NULL)
    {
        /* The IOMMU group is not already open. First need to create a container.
         * This is done before trying open the VFIO device to determine which type of IOMMU to use. */
        if ((vfio_devices->num_containers > 0) && (!vfio_isolate_iommu_groups))
        {
            /* Use an existing container */
            container = &vfio_devices->containers[0];
        }
        else
        {
            /* Create a new container */
            container = vfio_iommu_create_container (vfio_devices);
        }

        new_device->group = vfio_open_iommu_group (container, iommu_group_name, new_device->device_description);
    }
}


/**
 * @brief Open a VFIO device for indirect access, by communicating with the VFIO multi process manager
 * @param[in/out] vfio_devices The context for vfio devices which are being opened
 * @param[in/out] new_device The VFIO device being opened
 * @param[in] iommu_group_name The name of the IOMMU group used by the device
 * @return Returns true if the VFIO device was opened, or false if an error
 */
static bool open_vfio_device_with_indirect_access (vfio_devices_t *const vfio_devices, vfio_device_t *const new_device,
                                                   const char *const iommu_group_name)
{
    bool success;
    vfio_manage_messages_t tx_buffer;
    vfio_manage_messages_t rx_buffer;
    vfio_open_device_reply_fds_t vfio_fds;

    /* Determine if an IOMMU group and therefore container has been previously stored */
    new_device->group = find_open_iommu_group (vfio_devices, iommu_group_name);

    /* Send a request to the manager to open the VFIO device.
     * Indicate the container_fd is required if no existing IOMMU group. */
    tx_buffer.open_device_request.msg_id = VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST;
    tx_buffer.open_device_request.device_id.domain = new_device->pci_dev->domain;
    tx_buffer.open_device_request.device_id.bus = new_device->pci_dev->bus;
    tx_buffer.open_device_request.device_id.dev = new_device->pci_dev->dev;
    tx_buffer.open_device_request.device_id.func = new_device->pci_dev->func;
    tx_buffer.open_device_request.dma_capability = new_device->dma_capability;
    tx_buffer.open_device_request.container_fd_required = new_device->group == NULL;
    vfio_send_manage_message (vfio_devices->manager_client_socket_fd, &tx_buffer, NULL);

    /* Wait to reply to the request */
    vfio_receive_manage_reply (vfio_devices->manager_client_socket_fd, &rx_buffer, &vfio_fds, VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY);

    success = rx_buffer.open_device_reply.success;
    if (success)
    {
        if (tx_buffer.open_device_request.container_fd_required)
        {
            /* When requested a container_fd, store the received file descriptor and the IOMMU groups the container
             * is used on. This allows calls to find_open_iommu_group() for further opened devices to locate the information. */
            if (vfio_devices->num_containers == MAX_VFIO_DEVICES)
            {
                /* This shouldn't happen since the caller should have checked for exceeding the maximum number of devices */
                fprintf (stderr, "No free containers\n");
                exit (EXIT_FAILURE);
            }

            /* Set the minimum container and IOMMU group information needed in the client for indirect access.
             *
             * For the IOMMU group only the name needs to be populated. */
            vfio_iommu_container_t *const container = &vfio_devices->containers[vfio_devices->num_containers];
            container->vfio_devices = vfio_devices;
            container->iommu_type = rx_buffer.open_device_reply.iommu_type;
            container->container_id = rx_buffer.open_device_reply.container_id;
            container->container_fd = vfio_fds.container_fd;
            container->num_iommu_groups = rx_buffer.open_device_reply.num_iommu_groups;
            for (uint32_t group_index = 0; group_index < container->num_iommu_groups; group_index++)
            {
                vfio_iommu_group_t *const group = &container->iommu_groups[group_index];

                group->iommu_group_name = strdup (rx_buffer.open_device_reply.iommu_group_names[group_index]);
                group->group_fd = -1;
                group->container = container;
                if (strcmp (group->iommu_group_name, iommu_group_name) == 0)
                {
                    new_device->group = group;
                }
            }

            vfio_devices->num_containers++;
        }

        /* Store the device file descriptor to access the device, and get the information */
        new_device->device_fd = vfio_fds.device_fd;
        success = get_vfio_device_info (new_device);
    }
    else
    {
        printf ("Manager failed to open device %s in IOMMU group %s\n", new_device->device_description, iommu_group_name);
    }

    return success;
}


/**
 * @brief Open an VFIO device, without mapping it's memory BARs.
 * @details This handles the different action to open the VFIO device depending upon devices_usage
 * @param[in/out] vfio_devices The list of vfio devices to append the opened device to.
 *                             If this function is successful vfio_devices->num_devices is incremented
 * @param[in] pci_dev The PCI device to open using VFIO
 * @param[in] dma_capability Used by this function to determine if to enable the device as a bus master for DMA.
 *                           Stored for later use in allocation IOVA according to the addressing capabilities of the DMA engine.
 */
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev,
                       const vfio_device_dma_capability_t dma_capability)
{
    vfio_device_t *const new_device = &vfio_devices->devices[vfio_devices->num_devices];

    /* Check the PCI device has an IOMMU group. */
    snprintf (new_device->device_name, sizeof (new_device->device_name), "%04x:%02x:%02x.%x",
            pci_dev->domain, pci_dev->bus, pci_dev->dev, pci_dev->func);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
    /* Suppress warning of the form, which was seen with GCC 10.3.1 when compiling for the release platform:
     *   warning: 'snprintf' argument 4 may overlap destination object 'vfio_devices' [-Wrestrict]
     *
     * "Bug 102919 - spurious -Wrestrict warning for sprintf into the same member array as argument plus offset"
     * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102919 suggests this is a spurious warning
     */
    snprintf (new_device->device_description, sizeof (new_device->device_description), "device %s (%04x:%04x)",
            new_device->device_name, pci_dev->vendor_id, pci_dev->device_id);
#pragma GCC diagnostic pop
    char *const iommu_group_name = vfio_get_iommu_group (pci_dev);
    if (iommu_group_name == NULL)
    {
        printf ("Skipping %s as no IOMMU group\n", new_device->device_description);
        return;
    }

    /* Save PCI device identification */
    new_device->pci_dev = pci_dev;
    new_device->pci_subsystem_vendor_id = pci_read_word (pci_dev, PCI_SUBSYSTEM_VENDOR_ID);
    new_device->pci_subsystem_device_id = pci_read_word (pci_dev, PCI_SUBSYSTEM_ID);
    new_device->dma_capability = dma_capability;

    switch (vfio_devices->devices_usage)
    {
    case VFIO_DEVICES_USAGE_DIRECT_ACCESS:
        open_group_with_direct_access (vfio_devices, new_device, iommu_group_name);
        if (new_device->group == NULL)
        {
            /* open_group_with_direct_access() has reported a diagnostic message why the IOMMU group couldn't be opened */
            return;
        }

        if (!open_vfio_device_fd (new_device))
        {
            /* open_vfio_device_fd() has reported a diagnostic message why the device couldn't be opened */
            return;
        }
        break;

    case VFIO_DEVICES_USAGE_INDIRECT_ACCESS:
        if (!open_vfio_device_with_indirect_access (vfio_devices, new_device, iommu_group_name))
        {
            return;
        }
        break;

    case VFIO_DEVICES_USAGE_MANAGER:
        open_group_with_direct_access (vfio_devices, new_device, iommu_group_name);
        if (new_device->group == NULL)
        {
            /* open_group_with_direct_access() has reported a diagnostic message why the IOMMU group couldn't be opened */
            return;
        }

        /* The VFIO device will be opened when the first client process requests access */
        new_device->device_fd = -1;
        break;
    }

    /* Record device successfully opened */
    vfio_devices->num_devices++;
}


/*
 * @brief Determine if one PCI device identity field match a filter, either a specific value or the "ANY" value
 * @param[in] pci_id The identity field from the PCI device to compare against the filter
 * @param[in] filter_id The filter identity field
 * @return Returns true if pci_id matches the filter
 */
static bool pci_filter_id_match (const u16 pci_id, const int filter_id)
{
    return (filter_id == VFIO_PCI_DEVICE_FILTER_ANY) || ((int) pci_id == filter_id);
}


/**
 * @brief Match a VFIO device against a filter
 * @param[in] vfio_device The VFIO device to match
 * @param[in] filter The filter to match
 * @return Returns true if the VFIO device matches the PCI device filter
 */
bool vfio_device_pci_filter_match (const vfio_device_t *const vfio_device, const vfio_pci_device_identity_filter_t *const filter)
{
    return pci_filter_id_match (vfio_device->pci_dev->vendor_id, filter->vendor_id) &&
            pci_filter_id_match (vfio_device->pci_dev->device_id, filter->device_id) &&
            pci_filter_id_match (vfio_device->pci_subsystem_vendor_id, filter->subsystem_vendor_id) &&
            pci_filter_id_match (vfio_device->pci_subsystem_device_id, filter->subsystem_device_id);
}


/**
 * @brief Detect if the VFIO multi process manager is running, by trying to connect to it.
 * @details This sets the devices_usage option for programs which access VFIO devices
 * @param[in/out] vfio_devices The VFIO devices being opened.
 */
static void vfio_detect_manager (vfio_devices_t *const vfio_devices)
{
    struct sockaddr_un manager_addr;
    int rc;
    int saved_errno;

    vfio_devices->manager_client_socket_fd = socket (AF_UNIX, SOCK_SEQPACKET, 0);
    if (vfio_devices->manager_client_socket_fd == -1)
    {
        fprintf (stderr, "socket() failed\n");
        exit (EXIT_FAILURE);
    }

    memset (&manager_addr, 0, sizeof (manager_addr));
    manager_addr.sun_family = AF_UNIX;
    memcpy (manager_addr.sun_path, VFIO_MULTI_PROCESS_MANAGER_ABSTRACT_NAMESPACE,
            sizeof (VFIO_MULTI_PROCESS_MANAGER_ABSTRACT_NAMESPACE));
    const socklen_t socklen = offsetof(struct sockaddr_un, sun_path[1]) + (socklen_t) strlen (&manager_addr.sun_path[1]);
    errno = 0;

    errno = 0;
    rc = connect (vfio_devices->manager_client_socket_fd, (const struct sockaddr *) &manager_addr, socklen);
    saved_errno = errno;
    if (rc == 0)
    {
        /* Connected to the VFIO multi process manager, so indicate the manager client socket needs to be used */
        vfio_devices->devices_usage = VFIO_DEVICES_USAGE_INDIRECT_ACCESS;
    }
    else if (saved_errno == ECONNREFUSED)
    {
        /* The VFIO multi process manager isn't running, so close the client socket and indicate direct access needs to be used */
        rc = close (vfio_devices->manager_client_socket_fd);
        if (rc != 0)
        {
            fprintf (stderr, "close() failed\n");
            exit (EXIT_FAILURE);
        }
        vfio_devices->manager_client_socket_fd = -1;
        vfio_devices->devices_usage = VFIO_DEVICES_USAGE_DIRECT_ACCESS;
    }
    else
    {
        /* Unexpected error, so treat as a bug */
        fprintf (stderr, "connect() failed : %s\n", strerror (saved_errno));
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Scan the PCI bus, attempting to open all devices using VFIO which match the filter.
 * @details If an error occurs attempting to open the VFIO device then a message is output to the console and the
 *          offending device isn't returned in vfio_devices.
 *          The memory BARs of the VFIO devices are not mapped.
 *
 *          Where the filter is:
 *          a. The identity filters passed to this function.
 *          b. Any optional location filters set by proceeding calls to vfio_add_pci_device_location_filter()
 * @param[out] vfio_devices The list of opened VFIO devices
 * @param[in] num_id_filters The number of PCI device identity filters
 * @param[in] id_filters The identity filters for PCI devices to open
 */
void open_vfio_devices_matching_filter (vfio_devices_t *const vfio_devices,
                                        const size_t num_id_filters,
                                        const vfio_pci_device_identity_filter_t id_filters[const num_id_filters])
{
    struct pci_dev *dev;
    int known_fields;
    u16 subsystem_vendor_id;
    u16 subsystem_device_id;
    bool pci_device_matches_identity_filter;
    bool pci_device_matches_location_filter;
    vfio_device_dma_capability_t dma_capability;

    memset (vfio_devices, 0, sizeof (*vfio_devices));
    vfio_devices->num_containers = 0;
    vfio_devices->cmem_usage = VFIO_CMEM_USAGE_NONE;
    vfio_detect_manager (vfio_devices);

    /* Initialise PCI access using the defaults */
    vfio_devices->pacc = pci_alloc ();
    if (vfio_devices->pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (vfio_devices->pacc);

    /* Scan the entire bus */
    pci_scan_bus (vfio_devices->pacc);

    /* Open the PCI devices which match the filters and have an IOMMU group assigned */
    const int required_fields = PCI_FILL_IDENT;
    for (dev = vfio_devices->pacc->devices; (dev != NULL) && (vfio_devices->num_devices < MAX_VFIO_DEVICES); dev = dev->next)
    {
        known_fields = pci_fill_info (dev, required_fields);
        if ((known_fields & required_fields) == required_fields)
        {
            /* Always apply the caller supplied identity filter */
            pci_device_matches_identity_filter = false;
            dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE;
            for (size_t filter_index = 0; (!pci_device_matches_identity_filter) && (filter_index < num_id_filters); filter_index++)
            {
                const vfio_pci_device_identity_filter_t *const filter = &id_filters[filter_index];

                pci_device_matches_identity_filter = pci_filter_id_match (dev->vendor_id, filter->vendor_id) &&
                        pci_filter_id_match (dev->device_id , filter->device_id);
                if (pci_device_matches_identity_filter)
                {
                    subsystem_vendor_id = pci_read_word (dev, PCI_SUBSYSTEM_VENDOR_ID);
                    subsystem_device_id = pci_read_word (dev, PCI_SUBSYSTEM_ID);
                    pci_device_matches_identity_filter = pci_filter_id_match (subsystem_vendor_id, filter->subsystem_vendor_id) &&
                            pci_filter_id_match (subsystem_device_id, filter->subsystem_device_id);
                    if (pci_device_matches_identity_filter)
                    {
                        dma_capability = filter->dma_capability;
                    }
                }
            }

            if (num_pci_device_location_filters > 0)
            {
                /* Apply location filter */
                pci_device_matches_location_filter = false;
                for (uint32_t filter_index = 0;
                        (!pci_device_matches_location_filter) && (filter_index < num_pci_device_location_filters);
                        filter_index++)
                {
                    const vfio_pci_device_location_filter_t *const filter = &vfio_pci_device_location_filters[filter_index];

                    pci_device_matches_location_filter =
                            (dev->domain == filter->domain) && (dev->bus == filter->bus) &&
                            (dev->dev == filter->dev) && (dev->func == filter->func);
                }
            }
            else
            {
                /* No location filter to apply */
                pci_device_matches_location_filter = true;
            }

            if (pci_device_matches_identity_filter && pci_device_matches_location_filter)
            {
                open_vfio_device (vfio_devices, dev, dma_capability);
            }
        }
    }
}


/**
 * @brief Cause each IOMMU group to use a separate IOMMU container, which isolates the IOVA allocations for each IOMMU group
 * @details To have an effect, this must be called before an IOMMU group is opened by a call to
 *          open_vfio_devices_matching_filter() or open_vfio_device().
 */
void vfio_enable_iommu_group_isolation (void)
{
    vfio_isolate_iommu_groups = true;
}


/**
 * @brief Close an IOMMU container, including any IOMMU groups in the container
 * @param[in/out] container The contains to close
 */
static void close_vfio_container (vfio_iommu_container_t *const container)
{
    int rc;

    /* Close the IOMMU groups in the container */
    for (uint32_t group_index = 0; group_index < container->num_iommu_groups; group_index++)
    {
        vfio_iommu_group_t *const group = &container->iommu_groups[group_index];

        if (group->group_fd != -1)
        {
            rc = close (group->group_fd);
            if (rc != 0)
            {
                fprintf (stderr, "close (%s) failed : %s\n", group->group_pathname, strerror (errno));
                exit (EXIT_FAILURE);
            }
            group->group_fd = -1;
        }
    }

    /* Close the container */
    container->num_iommu_groups = 0;
    rc = close (container->container_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }
    container->container_fd = -1;

    free (container->iommu_info);
    container->iommu_info = NULL;
}


/**
 * @brief Close all the open VFIO devices
 * @param[in/out] vfio_devices The VFIO devices to close
 */
void close_vfio_devices (vfio_devices_t *const vfio_devices)
{
    int rc;

    /* Close the VFIO devices, including unmapping their bars */
    for (uint32_t device_index = 0; device_index < vfio_devices->num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices->devices[device_index];

        for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
        {
            if (vfio_device->mapped_bars[bar_index] != NULL)
            {
                /* Unmap the BAR */
                rc = munmap (vfio_device->mapped_bars[bar_index], vfio_device->regions_info[bar_index].size);
                if (rc != 0)
                {
                    printf ("munmap() failed : %s\n", strerror (errno));
                    exit (EXIT_FAILURE);
                }
                vfio_device->mapped_bars[bar_index] = NULL;
            }
        }

        if (vfio_device->device_fd != -1)
        {
            /* Close the file descriptor for the the local process.
             * For VFIO_DEVICES_USAGE_INDIRECT_ACCESS this will leave the device open by the manager. */
            rc = close (vfio_device->device_fd);
            if (rc != 0)
            {
                fprintf (stderr, "close (%s) failed : %s\n", vfio_device->device_name, strerror (errno));
                exit (EXIT_FAILURE);
            }
            vfio_device->device_fd = -1;

            if (vfio_devices->devices_usage == VFIO_DEVICES_USAGE_INDIRECT_ACCESS)
            {
                vfio_manage_messages_t tx_buffer;
                vfio_manage_messages_t rx_buffer;

                /* Tell the manager this client is no longer using the VFIO device.
                 * The manager will close the device once no longer in use by any client. */
                tx_buffer.close_device_request.msg_id = VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST;
                tx_buffer.close_device_request.device_id.domain = vfio_device->pci_dev->domain;
                tx_buffer.close_device_request.device_id.bus    = vfio_device->pci_dev->bus;
                tx_buffer.close_device_request.device_id.dev    = vfio_device->pci_dev->dev;
                tx_buffer.close_device_request.device_id.func   = vfio_device->pci_dev->func;
                vfio_send_manage_message (vfio_devices->manager_client_socket_fd, &tx_buffer, NULL);
                vfio_receive_manage_reply (vfio_devices->manager_client_socket_fd, &rx_buffer, NULL,
                        VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REPLY);
                if (!rx_buffer.close_device_reply.success)
                {
                    printf ("Manager failed to open device %s\n", vfio_device->device_description);
                    exit (EXIT_FAILURE);
                }
            }
        }
    }

    /* Close the containers */
    for (uint32_t container_index = 0; container_index < vfio_devices->num_containers; container_index++)
    {
        close_vfio_container (&vfio_devices->containers[container_index]);
    }
    vfio_devices->num_containers = 0;

#ifdef HAVE_CMEM
    /* Close the cmem driver if it has been opened */
    if (vfio_devices->cmem_usage == VFIO_CMEM_USAGE_DRIVER_OPEN)
    {
        /* Return ignored as the function returns a fixed value of zero */
        (void) cmem_drv_close ();
    }
#endif

    /* Cleanup the PCI access, if was used */
    if (vfio_devices->pacc != NULL)
    {
        pci_cleanup (vfio_devices->pacc);
        vfio_devices->pacc = NULL;
    }

    if (vfio_devices->devices_usage == VFIO_DEVICES_USAGE_INDIRECT_ACCESS)
    {
        /* Close the connection to the VFIO multi process manager */
        rc = close (vfio_devices->manager_client_socket_fd);
        if (rc != 0)
        {
            printf ("close() failed\n");
            exit (EXIT_FAILURE);
        }
    }
}


/**
 * @brief Display the possible VFIO devices in the PC which can opened by a group of filters
 * @details This is for utilities to report supported PCI identities for a group of filters in a program, without
 *          needing to actually open the VFIO devices (to avoid errors if the VFIO device is already open by a process).
 *
 *          The physical slot is displayed in case the there are multiple instances of the same design.
 *          It is machine specific about if a physical slot is reported.
 *
 *          Warns if a matching device doesn't have an IOMMU group assigned, since won't be able to opened using VFIO.
 *          This may happen when an IOMMU isn't present, and the noiommu mode hasn't been used for the device.
 * @param[in] num_filters The number of PCI device filters
 * @param[in] filters The filters for PCI devices to use to search for PCI devices.
 * @param[in] design_names When non-NULL a descriptive name for each PCI device filter.
 */
void display_possible_vfio_devices (const size_t num_filters, const vfio_pci_device_identity_filter_t filters[const num_filters],
                                    const char *const design_names[const num_filters])
{
    struct pci_access *pacc;
    struct pci_dev *dev;
    int known_fields;
    u16 subsystem_vendor_id;
    u16 subsystem_device_id;
    uint32_t num_matches;
    bool pci_device_matches_filter;
    char *iommu_group;

    /* Initialise PCI access using the defaults */
    pacc = pci_alloc ();
    if (pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (pacc);

    /* Scan the entire bus */
    pci_scan_bus (pacc);

    /* Display PCI devices which match the filters */
    const int requested_fields = PCI_FILL_IDENT | PCI_FILL_PHYS_SLOT;
    printf ("Scanning bus for %lu PCI device filters\n", num_filters);
    for (dev = pacc->devices; dev != NULL; dev = dev->next)
    {
        known_fields = pci_fill_info (dev, requested_fields);
        if ((known_fields & PCI_FILL_IDENT) != 0)
        {
            /* Since only looks at the PCI identities it is possible that multiple designs match.
             * Therefore, report the names of all designs which match the identity. */
            num_matches = 0;
            for (size_t filter_index = 0; filter_index < num_filters; filter_index++)
            {
                const vfio_pci_device_identity_filter_t *const filter = &filters[filter_index];

                pci_device_matches_filter = pci_filter_id_match (dev->vendor_id, filter->vendor_id) &&
                        pci_filter_id_match (dev->device_id , filter->device_id);
                if (pci_device_matches_filter)
                {
                    subsystem_vendor_id = pci_read_word (dev, PCI_SUBSYSTEM_VENDOR_ID);
                    subsystem_device_id = pci_read_word (dev, PCI_SUBSYSTEM_ID);
                    pci_device_matches_filter = pci_filter_id_match (subsystem_vendor_id, filter->subsystem_vendor_id) &&
                            pci_filter_id_match (subsystem_device_id, filter->subsystem_device_id);
                    if (pci_device_matches_filter)
                    {
                        if (num_matches == 0)
                        {
                            printf ("PCI device %04x:%02x:%02x.%x", dev->domain, dev->bus, dev->dev, dev->func);
                            if (((known_fields & PCI_FILL_PHYS_SLOT) != 0) && (dev->phy_slot != NULL))
                            {
                                printf ("  physical slot %s", dev->phy_slot);
                            }
                        }
                        if ((design_names != NULL) && (design_names[filter_index] != NULL))
                        {
                            printf ("%s %s", (num_matches == 0) ? "  Design name" : " or", design_names[filter_index]);
                        }
                        num_matches++;
                    }
                }
            }

            if (num_matches > 0)
            {
                iommu_group = vfio_get_iommu_group (dev);
                if (iommu_group != NULL)
                {
                    printf ("  IOMMU group %s", iommu_group);
                }
                else
                {
                    printf ("  No IOMMU group set, won't be able to open device using VFIO");
                }
                printf ("\n");
            }
        }
    }

    pci_cleanup (pacc);
}


/**
 * @brief Allocate a buffer, and create a DMA mapping for the allocated memory using a specified container.
 * @param[in/out] container The underlying container to use to perform the IOVA allocation.
 * @param[in] dma_capability Determines if the allocation is for a A32 or A64 capable DMA device.
 * @param[out] mapping Contains the process memory and associated DMA mapping which has been allocated.
 *                     On failure, mapping->buffer.vaddr is NULL.
 *                     On success the buffer contents has been zeroed.
 * @param[in] requested_size The requested size in bytes to allocate.
 *                           The actual size allocated may be increased to allow for the supported IOVA page sizes.
 * @param[in] permission Bitwise OR VFIO_DMA_MAP_FLAG_READ / VFIO_DMA_MAP_FLAG_WRITE flags to define
 *                       the device access to the DMA mapping.
 *                       Not used when using the cmem driver.
 * @param[in] buffer_allocation Controls how the buffer for the process is allocated
 */
void allocate_vfio_container_dma_mapping (vfio_iommu_container_t *const container, vfio_device_dma_capability_t dma_capability,
                                          vfio_dma_mapping_t *const mapping,
                                          const size_t requested_size, const uint32_t permission,
                                          const vfio_buffer_allocation_type_t buffer_allocation)
{
    int rc;
    size_t aligned_size = requested_size;

    mapping->container = container;
    mapping->num_allocated_bytes = 0;
    if (container->iommu_type == VFIO_NOIOMMU_IOMMU)
    {
        /* In NOIOMMU mode allocate IOVA using the contiguous physical memory cmem driver.
         * Open the cmem driver before first use. */
        mapping->buffer.vaddr = NULL;
        if (container->vfio_devices->cmem_usage == VFIO_CMEM_USAGE_NONE)
        {
#ifdef HAVE_CMEM
            rc = cmem_drv_open ();
            if (rc == 0)
            {
                container->vfio_devices->cmem_usage = VFIO_CMEM_USAGE_DRIVER_OPEN;
            }
            else
            {
                printf ("VFIO DMA not supported as failed to open cmem driver\n");
                container->vfio_devices->cmem_usage = VFIO_CMEM_USAGE_OPEN_FAILED;
            }
#else
            container->vfio_devices->cmem_usage = VFIO_CMEM_USAGE_SUPPORT_NOT_COMPILED;
            printf ("VFIO DMA not supported as cmem support not compiled in\n");
#endif
        }

#ifdef HAVE_CMEM
        if (container->vfio_devices->cmem_usage == VFIO_CMEM_USAGE_DRIVER_OPEN)
        {
            const size_t page_size = (size_t) getpagesize ();

            /* Round up the requested size to a multiple of the page size, to ensure the next physical memory allocation
             * starts on a page boundary. Otherwise, the next vaddr will be invalid. */
            aligned_size = (requested_size + (page_size - 1)) & (~(page_size - 1));

            /* cmem driver is open, so attempt the allocation and use the allocated physical memory address as the IOVA
             * to be used for DMA. */
            create_vfio_buffer (&mapping->buffer, aligned_size,
                    (dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A64) ?
                            VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A64 : VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY_A32, NULL);
            mapping->iova = mapping->buffer.cmem_host_buf_desc.physAddr;
            if (mapping->buffer.vaddr != NULL)
            {
                memset (mapping->buffer.vaddr, 0, mapping->buffer.size);
            }
        }
#endif
    }
    else
    {
        /* Allocate IOVA using the IOMMU. */
        struct vfio_iommu_type1_dma_map dma_map;
        char name_suffix[PATH_MAX];
        vfio_iova_region_t region;

        if (container->vfio_devices->devices_usage == VFIO_DEVICES_USAGE_INDIRECT_ACCESS)
        {
            allocate_iova_region_indirect (container, dma_capability, requested_size, &region);
        }
        else
        {
            const uint32_t unused_client_id = 0;
            allocate_iova_region_direct (container, dma_capability, requested_size, unused_client_id, &region);
        }
        aligned_size = (region.end + 1) - region.start;
        if (region.allocated)
        {
            mapping->iova = region.start;

            /* Create the buffer in the local process.
             * Since multiple containers may be in use, prepends the PID to make the name unique */
            snprintf (name_suffix, sizeof (name_suffix), "pid-%d_iova-%" PRIu64, getpid(), mapping->iova);
            create_vfio_buffer (&mapping->buffer, aligned_size, buffer_allocation, name_suffix);

            if (mapping->buffer.vaddr != NULL)
            {
                memset (mapping->buffer.vaddr, 0, mapping->buffer.size);
                memset (&dma_map, 0, sizeof (dma_map));
                dma_map.argsz = sizeof (dma_map);
                dma_map.flags = permission;
                dma_map.vaddr = (uintptr_t) mapping->buffer.vaddr;
                dma_map.iova = mapping->iova;
                dma_map.size = mapping->buffer.size;
                rc = ioctl (container->container_fd, VFIO_IOMMU_MAP_DMA, &dma_map);
                if (rc != 0)
                {
                    printf ("VFIO_IOMMU_MAP_DMA of size %zu failed : %s\n", mapping->buffer.size, strerror (-rc));
                    free (mapping->buffer.vaddr);
                    mapping->buffer.vaddr = NULL;
                }
            }
        }
        else
        {
            mapping->buffer.vaddr = NULL;
            printf ("Failed to allocate %zu bytes for VFIO DMA mapping\n", aligned_size);
        }
    }
}


/**
 * @brief Allocate a buffer, and create a DMA mapping for the allocated memory using a specified device
 * @param[in/out] vfio_device The VFIO device to create the DMA mapping for:
 *                a. The VFIO device is used to determine the DMA address capability to select a suitable IOVA value.
 *                b. The underlying IOMMU container is used to the allocate the IOVA for the mapping.
 * @param[out] mapping Contains the process memory and associated DMA mapping which has been allocated.
 *                     On failure, mapping->buffer.vaddr is NULL.
 *                     On success the buffer contents has been zeroed.
 * @param[in] requested_size The requested size in bytes to allocate.
 *                           The actual size allocated may be increased to allow for the supported IOVA page sizes.
 * @param[in] permission Bitwise OR VFIO_DMA_MAP_FLAG_READ / VFIO_DMA_MAP_FLAG_WRITE flags to define
 *                       the device access to the DMA mapping.
 *                       Not used when using the cmem driver.
 * @param[in] buffer_allocation Controls how the buffer for the process is allocated
 */
void allocate_vfio_dma_mapping (vfio_device_t *const vfio_device,
                                vfio_dma_mapping_t *const mapping,
                                const size_t requested_size, const uint32_t permission,
                                const vfio_buffer_allocation_type_t buffer_allocation)
{
    allocate_vfio_container_dma_mapping (vfio_device->group->container, vfio_device->dma_capability,
            mapping, requested_size, permission, buffer_allocation);
}


/**
 * @brief Allocate some space from a VFIO DMA mapping
 * @param[in/out] mapping The mapping to allocate space from
 * @param[in] allocation_size The size of the allocation in bytes
 * @param[out] allocated_iova The iova of the allocation
 * @return The allocated virtual address, or NULL if insufficient space for the allocation
 */
void *vfio_dma_mapping_allocate_space (vfio_dma_mapping_t *const mapping,
                                       const size_t allocation_size, uint64_t *const allocated_iova)
{
    uint8_t *const vaddr_bytes = mapping->buffer.vaddr;
    void *allocated_vaddr = NULL;

    *allocated_iova = mapping->iova + mapping->num_allocated_bytes;
    if ((mapping->num_allocated_bytes + allocation_size) <= mapping->buffer.size)
    {
        allocated_vaddr = &vaddr_bytes[mapping->num_allocated_bytes];
        mapping->num_allocated_bytes += allocation_size;
    }
    else
    {
        printf ("Insufficient space to allocate %zu bytes in VFIO DMA mapping\n", allocation_size);
    }

    return allocated_vaddr;
}


/**
 * @brief Round up the allocation of a VFIO DMA mapping to the cache line boundary
 * @param[in/out] mapping The mapping to align the next allocation for
 */
void vfio_dma_mapping_align_space (vfio_dma_mapping_t *const mapping)
{
    mapping->num_allocated_bytes = vfio_align_cache_line_size (mapping->num_allocated_bytes);
}


/**
 * @brief Free an IOVA region by communicating with the manager
 * @param[in] container The container which was used to allocate the IOVA region
 * @param[in] free_region The IOVA region to free
 */
static void free_vfio_region_indirect (const vfio_iommu_container_t *const container, const vfio_iova_region_t *const free_region)
{
    vfio_manage_messages_t tx_buffer;
    vfio_manage_messages_t rx_buffer;

    /* Send the request */
    tx_buffer.free_iova_request.msg_id = VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST;
    tx_buffer.free_iova_request.container_id = container->container_id;
    tx_buffer.free_iova_request.start = free_region->start;
    tx_buffer.free_iova_request.end = free_region->end;
    vfio_send_manage_message (container->vfio_devices->manager_client_socket_fd, &tx_buffer, NULL);

    /* Wait for the reply */
    vfio_receive_manage_reply (container->vfio_devices->manager_client_socket_fd, &rx_buffer, NULL,
            VFIO_MANAGE_MSG_ID_FREE_IOVA_REPLY);
    if (!rx_buffer.free_iova_reply.success)
    {
        printf ("Indirect freeing of IOVA failed\n");
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Free a DMA mapping, and the associated process virtual memory
 * @param[in/out] mapping The DMA mapping to free.
 */
void free_vfio_dma_mapping (vfio_dma_mapping_t *const mapping)
{
    int rc;
    struct vfio_iommu_type1_dma_unmap dma_unmap =
    {
        .argsz = sizeof (dma_unmap),
        .flags = 0,
        .iova = mapping->iova,
        .size = mapping->buffer.size
    };

    if (mapping->buffer.vaddr != NULL)
    {
        if (mapping->container->iommu_type == VFIO_NOIOMMU_IOMMU)
        {
            /* Using NOIOMMU so just free the buffer */
            free_vfio_buffer (&mapping->buffer);
        }
        else
        {
            /* Using IOMMU so free the IOMMU DMA mapping and then the buffer */
            rc = ioctl (mapping->container->container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
            if (rc == 0)
            {
                vfio_iova_region_t free_region =
                {
                    .start = mapping->iova,
                    .end = mapping->iova + (mapping->buffer.size - 1),
                    .allocating_client_id = 0,
                    .allocated = false
                };

                if (mapping->container->vfio_devices->devices_usage == VFIO_DEVICES_USAGE_INDIRECT_ACCESS)
                {
                    free_vfio_region_indirect (mapping->container, &free_region);
                }
                else
                {
                    update_iova_regions (mapping->container, &free_region);
                }
                free_vfio_buffer (&mapping->buffer);
            }
            else
            {
                printf ("VFIO_IOMMU_UNMAP_DMA of size %zu failed : %s\n", mapping->buffer.size, strerror (-rc));
            }
        }
    }
}


/**
 * @brief Read a number of bytes from a PCI region of a VFIO device
 * @details If an error occurs during the read displays diagnostic information and sets the returned bytes to 0xff.
 *          This is for regions which can't be directly mapped:
 *          - PCI configuration
 *          - IO bars
 * @param[in/out] vfio_device Device to read from
 * @param[in] region_index Which PCI region to read from
 * @param[in] offset Offset into the configuration space to read
 * @param[in] num_bytes The number of bytes to read
 * @param[out] config_bytes The bytes which have been read.
 * @return Returns true if the read was successful, or false otherwise.
 */
bool vfio_read_pci_region_bytes (vfio_device_t *const vfio_device, const uint32_t region_index,
                                 const uint32_t offset, const size_t num_bytes, void *const config_bytes)
{
    bool success = false;
    ssize_t num_read;

    memset (config_bytes, 0xff, num_bytes);

    get_vfio_device_region (vfio_device, region_index);
    if (vfio_device->regions_info_populated[region_index])
    {
        num_read = pread (vfio_device->device_fd, config_bytes, num_bytes,
                (off_t) (vfio_device->regions_info[region_index].offset + offset));
        if (num_read == (ssize_t) num_bytes)
        {
            success = true;
        }
        else
        {
            printf ("  PCI region %u read of %zu bytes from offset %" PRIu32 " only read %zd bytes : %s\n",
                    region_index, num_bytes, offset, num_read, strerror (errno));
        }
    }

    return success;
}


/**
 * @brief Read a 8-bit, 16-bit or 32-bit configuration value for a VFIO device
 * @details Unlike pciutuils or lbipciaccess libraries, this can read PCIe capabilities without the process needing
 *          CAP_SYS_ADMIN capability.
 * @param[in/out] vfio_device The VFIO device to perform the configuration read for.
 * @param[in] offset The byte offset for the configuration value to read
 * @param[out] value The configuration value read.
 *                   Will be all-ones if the configuration read fails
 * @return Returns true if the configuration value was read.
 */
bool vfio_read_pci_config_u8 (vfio_device_t *const vfio_device, const uint32_t offset, uint8_t *const value)
{
    return vfio_read_pci_region_bytes (vfio_device, VFIO_PCI_CONFIG_REGION_INDEX, offset, sizeof (*value), value);
}

bool vfio_read_pci_config_u16 (vfio_device_t *const vfio_device, const uint32_t offset, uint16_t *const value)
{
    return vfio_read_pci_region_bytes (vfio_device, VFIO_PCI_CONFIG_REGION_INDEX, offset, sizeof (*value), value);
}

bool vfio_read_pci_config_u32 (vfio_device_t *const vfio_device, const uint32_t offset, uint32_t *const value)
{
    return vfio_read_pci_region_bytes (vfio_device, VFIO_PCI_CONFIG_REGION_INDEX, offset, sizeof (*value), value);
}


/**
 * @brief Write a number of bytes to a PCI region of a VFIO device
 * @details If an error occurs during the read displays diagnostic information.
 *          This is for regions which can't be directly mapped:
 *          - PCI configuration
 *          - IO bars
 * @param[in/out] vfio_device Device to write to
 * @param[in] offset Offset into the configuration space to write
 * @param[in] num_bytes The number of bytes to write
 * @param[in] config_bytes The bytes to write.
 * @return Returns true if the write was successful, or false otherwise.
 */
bool vfio_write_pci_region_bytes (vfio_device_t *const vfio_device, const uint32_t region_index,
                                  const uint32_t offset, const size_t num_bytes, const void *const config_bytes)
{
    bool success = false;
    ssize_t num_written;

    get_vfio_device_region (vfio_device, region_index);
    if (vfio_device->regions_info_populated[region_index])
    {
        num_written = pwrite (vfio_device->device_fd, config_bytes, num_bytes,
                (off_t) (vfio_device->regions_info[region_index].offset + offset));
        if (num_written == (ssize_t) num_bytes)
        {
            success = true;
        }
        else
        {
            printf ("  PCI region %u write of %zu bytes to offset %" PRIu32 " only wrote %zd bytes : %s\n",
                    region_index, num_bytes, offset, num_written, strerror (errno));
        }
    }

    return success;
}


/**
 * @brief Write a 8-bit, 16-bit or 32-bit value to the PCI config space of a VFIO device
 * @param[in/out] vfio_device Device to write to
 * @param[in] offset Offset in the configuration space to write to
 * @param[in] config_word Word value to write
 * @return Returns true if the write was successful, or false otherwise.
 */
bool vfio_write_pci_config_u8 (vfio_device_t *const vfio_device, const uint32_t offset, const uint8_t value)
{
    return vfio_write_pci_region_bytes (vfio_device, VFIO_PCI_CONFIG_REGION_INDEX, offset, sizeof (value), &value);
}

bool vfio_write_pci_config_u16 (vfio_device_t *const vfio_device, const uint32_t offset, const uint16_t value)
{
    return vfio_write_pci_region_bytes (vfio_device, VFIO_PCI_CONFIG_REGION_INDEX, offset, sizeof (value), &value);
}

bool vfio_write_pci_config_u32 (vfio_device_t *const vfio_device, const uint32_t offset, const uint32_t value)
{
    return vfio_write_pci_region_bytes (vfio_device, VFIO_PCI_CONFIG_REGION_INDEX, offset, sizeof (value), &value);
}
