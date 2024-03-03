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

#include <stdlib.h>
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


/*
 * Paths for the VFIO character devices
 */
#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


/* Optional filters which may be set to only open by VFIO specific PCI device(s) by location.
 * This can be used to limit which VFIO devices one process may open. */
static vfio_pci_device_location_filter_t vfio_pci_device_location_filters[MAX_VFIO_DEVICES];
static uint32_t num_pci_device_location_filters;


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

    case VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY:
#ifdef HAVE_CMEM
        /* Perform a dynamic memory allocation of physical contiguous memory for a single buffer */
        buffer->vaddr = NULL;
        rc = cmem_drv_alloc (1, size, HOST_BUF_TYPE_DYNAMIC, &buffer->cmem_host_buf_desc);
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

    case VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY:
        /* Nothing to do here, as the cmem driver doesn't currently support freeing individual buffers */
        break;
    }

    buffer->size = 0;
    buffer->vaddr = NULL;
    buffer->fd = -1;
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
    int rc;
    void *addr;

    if (vfio_device->mapped_bars[bar_index] == NULL)
    {
        struct vfio_region_info *const region_info = &vfio_device->regions_info[bar_index];

        /* Get the device information. As this program is written for a PCI device which has fixed enumerations for regions,
         * the only use of the device information is a sanity check that VFIO reports a PCI device. */
        memset (&vfio_device->device_info, 0, sizeof (vfio_device->device_info));
        vfio_device->device_info.argsz = sizeof (vfio_device->device_info);
        rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_GET_INFO, &vfio_device->device_info);
        if (rc != 0)
        {
            printf ("VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
            return;
        }

        if ((vfio_device->device_info.flags & VFIO_DEVICE_FLAGS_PCI) == 0)
        {
            printf ("VFIO_DEVICE_GET_INFO flags don't report a PCI device\n");
            return;
        }

        /* Get region information for PCI BAR, to determine if an implemented BAR which can be mapped */
        memset (region_info, 0, sizeof (*region_info));
        region_info->argsz = sizeof (*region_info);
        region_info->index = bar_index;
        rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_GET_REGION_INFO, region_info);
        if (rc != 0)
        {
            printf ("VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
            return;
        }

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

    /* Get the device information to determine if reset is support */
    memset (&vfio_device->device_info, 0, sizeof (vfio_device->device_info));
    vfio_device->device_info.argsz = sizeof (vfio_device->device_info);
    rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_GET_INFO, &vfio_device->device_info);
    if (rc != 0)
    {
        printf ("VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
        return;
    }

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
 * @brief Find a a file descriptor for a pathname is already open in the local process.
 * @details This is to support secondary VFIO processes:
 *          a. In the primary process open_vfio_device() doesn't use O_CLOEXEC when opening the container and group FDs.
 *          b. The secondary process can find the FDs by walking the /proc/self/fd
 *
 * @todo This doesn't allow obtaining the device FD so may not be sufficient if multiple processes need to use the same FD.
 *       A more robust solution would be to get the primary to pass the container, group and device FDs via Unix domain sockets
 * @param[in] pathname_to_find The pathname to search for an existing open file descriptor.
 * @return If >= 0 the existing open file descriptor for pathname_to_find.
 *         If -1 pathname_to_find is not already open in the process.
 */
static int find_fd_from_primary_process (const char *const pathname_to_find)
{
    int existing_fd = -1;
    const char *const fd_path = "/proc/self/fd";
    DIR *fd_dir;
    struct dirent *fd_ent;
    char fd_ent_pathname[PATH_MAX];
    char pathname_of_fd[PATH_MAX];
    ssize_t link_num_bytes;

    fd_dir = opendir (fd_path);
    if (fd_dir != NULL)
    {
        fd_ent = readdir (fd_dir);
        while ((fd_ent != NULL) && (existing_fd == -1))
        {
            if (fd_ent->d_type == DT_LNK)
            {
                snprintf (fd_ent_pathname, sizeof (fd_ent_pathname), "%s/%s", fd_path, fd_ent->d_name);

                link_num_bytes = readlink (fd_ent_pathname, pathname_of_fd, sizeof (pathname_of_fd));
                if ((link_num_bytes > 0) && (strncmp (pathname_to_find, pathname_of_fd, (size_t) link_num_bytes) == 0))
                {
                    existing_fd = atoi (fd_ent->d_name);
                }
            }

            fd_ent = readdir (fd_dir);
        }

        closedir (fd_dir);
    }

    return existing_fd;
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
 * @param[in/out] vfio_devices Contains the IOVA regions to modify
 * @param[in] new_region The new IOVA region to append
 */
static void append_iova_region (vfio_devices_t *const vfio_devices, const vfio_iova_region_t *const new_region)
{
    const uint32_t grow_length = 64;

    /* Increase the allocated length of the array to ensure has space for the region */
    if (vfio_devices->num_iova_regions == vfio_devices->iova_regions_allocated_length)
    {
        vfio_devices->iova_regions_allocated_length += grow_length;
        vfio_devices->iova_regions = realloc (vfio_devices->iova_regions,
                vfio_devices->iova_regions_allocated_length * sizeof (vfio_devices->iova_regions[0]));
        if (vfio_devices->iova_regions == NULL)
        {
            fprintf (stderr, "Failed to allocate memory for iova_regions\n");
            exit (EXIT_FAILURE);
        }
    }

    /* Append the new region to the end of the array. May not be IOVA order, is sorted later */
    vfio_devices->iova_regions[vfio_devices->num_iova_regions] = *new_region;
    vfio_devices->num_iova_regions++;
}


/**
 * @brief Remove one IOVA region, shuffling down the following entries in the array
 * @param[in/out] vfio_devices Contains the IOVA regions to modify
 * @param[in] region_index Identifies which region to remove
 */
static void remove_iova_region (vfio_devices_t *const vfio_devices, const uint32_t region_index)
{
    const uint32_t num_regions_to_shuffle = vfio_devices->num_iova_regions - region_index;

    memmove (&vfio_devices->iova_regions[region_index], &vfio_devices->iova_regions[region_index + 1],
            sizeof (vfio_devices->iova_regions[region_index]) * num_regions_to_shuffle);
    vfio_devices->num_iova_regions--;
}


/**
 * @brief Update the array of IOVA regions with a new region.
 * @brief The new region can either:
 *        a. Add a free region at initialisation.
 *        b. Mark a region as allocated. This may split an existing free region.
 *        c. Free a previously allocated region. This may combine adjacent free regions.
 * @param[in/out] vfio_devices Contains the IOVA regions to update
 * @param[in] new_region Defines the new region
 */
static void update_iova_regions (vfio_devices_t *const vfio_devices, const vfio_iova_region_t *const new_region)
{
    bool new_region_processed = false;
    uint32_t region_index;

    /* Search for which existing region the region overlaps */
    for (region_index = 0; !new_region_processed && (region_index < vfio_devices->num_iova_regions); region_index++)
    {
        /* Take a copy of the existing region, since append_iova_region() may change the pointers */
        const vfio_iova_region_t existing_region = vfio_devices->iova_regions[region_index];

        if ((new_region->start >= existing_region.start) && (new_region->end <= existing_region.end))
        {
            /* Remove the existing region */
            remove_iova_region (vfio_devices, region_index);

            /* If the new region doesn't start at the beginning of the existing region, then append a region
             * with the same allocated state as the existing region to fill up to the start of the new region */
            if (new_region->start > existing_region.start)
            {
                const vfio_iova_region_t before_region =
                {
                    .start = existing_region.start,
                    .end = new_region->start - 1,
                    .allocated = existing_region.allocated
                };

                append_iova_region (vfio_devices, &before_region);
            }

            /* Append the new region */
            append_iova_region (vfio_devices, new_region);

            /* If the new region ends before that of the existing region, then append a region with the same
             * allocated state as the existing region to fill up to the end of the existing region */
            if (new_region->end < existing_region.end)
            {
                const vfio_iova_region_t after_region =
                {
                    .start = new_region->end + 1,
                    .end = existing_region.end,
                    .allocated = existing_region.allocated
                };

                append_iova_region (vfio_devices, &after_region);
            }

            new_region_processed = true;
        }
    }

    if (!new_region_processed)
    {
        /* The new region doesn't overlap an existing region. Must be inserting free regions at initialisation */
        append_iova_region (vfio_devices, new_region);
    }

    /* Sort the IOVA regions into ascending start order */
    qsort (vfio_devices->iova_regions, vfio_devices->num_iova_regions, sizeof (vfio_devices->iova_regions[0]), iova_region_compare);

    /* Combine adjacent IOVA regions which have the same allocated state */
    region_index = 0;
    while ((region_index + 1) < vfio_devices->num_iova_regions)
    {
        vfio_iova_region_t *const this_region = &vfio_devices->iova_regions[region_index];
        const uint32_t next_region_index = region_index + 1;
        const vfio_iova_region_t *const next_region = &vfio_devices->iova_regions[next_region_index];

        if (((this_region->end + 1) == next_region->start) && (this_region->allocated == next_region->allocated))
        {
            this_region->end = next_region->end;
            remove_iova_region (vfio_devices, next_region_index);
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
 * @param[in] vfio_device Which VFIO device the allocation is for, to determine if 64-bit IOVA capable
 * @param[in] min_start Minimum start IOVA to use for the allocation.
 *                      May be non-zero to cause a 64-bit IOVA capable device to initially avoid the
 *                      first 4 GiB IOVA space.
 * @param[in] aligned_size The size of the IOVA allocation required
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
static void attempt_iova_allocation (const vfio_device_t *const vfio_device,
                                     const uint64_t min_start, const size_t aligned_size,
                                     vfio_iova_region_t *const region)
{
    const uint64_t max_a32_end = 0xffffffffUL;
    vfio_devices_t *const vfio_devices = vfio_device->vfio_devices;
    size_t min_unused_space;

    /* Search for the smallest existing free region in which the aligned size will fit,
     * to try and reduce running out of IOVA addresses due to fragmentation. */
    min_unused_space = 0;
    for (uint32_t region_index = 0; region_index < vfio_devices->num_iova_regions; region_index++)
    {
        const vfio_iova_region_t *const existing_region = &vfio_devices->iova_regions[region_index];

        if (existing_region->allocated)
        {
            /* Skip this region, as not free */
        }
        else if (existing_region->end < min_start)
        {
            /* Skip this region, as all of it is below the minimum start */
        }
        else if ((vfio_device->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A32) && (existing_region->start > max_a32_end))
        {
            /* Skip this region, as all of it is above what can be addressed the device which is only 32-bit capable */
        }
        else
        {
            /* Limit the usable start for the region to the minimum */
            const uint64_t usable_region_start = (existing_region->start >= min_start) ? existing_region->start : min_start;

            /* When the device is only 32-bit IOVA capable limit the end to the first 4 GiB */
            const uint64_t usable_region_end =
                    ((vfio_device->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A32) && (max_a32_end < existing_region->end)) ?
                            max_a32_end : existing_region->end;

            const uint64_t usable_region_size = (usable_region_end + 1) - usable_region_start;

            if (usable_region_size >= aligned_size)
            {
                const uint64_t region_unused_space = usable_region_size - aligned_size;

                if (!region->allocated || (region_unused_space < min_unused_space))
                {
                    region->start = usable_region_start;
                    region->end = region->start + (aligned_size - 1);
                    region->allocated = true;
                    min_unused_space = region_unused_space;
                }
            }
        }
    }
}


/**
 * @brief Allocate a IOVA region for use by a DMA mapping for a device
 * @param[in/out] vfio_device Which VFIO device the allocation is for, to determine if 64-bit IOVA capable.
 *                            References the enclosing IOMMU contain used to track free IOVA values.
 * @param[in] aligned_size The size of the IOVA allocation required
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
static void allocate_iova_region (vfio_device_t *const vfio_device, const size_t aligned_size,
                                  vfio_iova_region_t *const region)
{
    /* Default to no allocation */
    region->start = 0;
    region->end = 0;
    region->allocated = false;

    if (vfio_device->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A64)
    {
        /* For 64-bit IOVA capable devices first attempt to allocate IOVA above the first 4 GiB,
         * to try and keep the first 4 GiB for devices which are only 32-bit IOVA capable. */
        const uint64_t a64_min_start = 0x100000000UL;

        attempt_iova_allocation (vfio_device, a64_min_start, aligned_size, region);
    }

    /* If allocation wasn't successful, or only a 32-bit IOVA capable device, try the allocation with no minimum start */
    if (!region->allocated)
    {
        attempt_iova_allocation (vfio_device, 0, aligned_size, region);
    }

    if (region->allocated)
    {
        /* Record the region as now allocated */
        update_iova_regions (vfio_device->vfio_devices, region);
    }
    else
    {
        /* Report a diagnostic message that the allocation failed */
        printf ("No free IOVA to allocate %zu bytes for %d bit IOVA capable device\n",
                aligned_size, (vfio_device->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A64) ? 64 : 32);
    }
}


/**
 * @brief Get the capabilities for a type 1 IOMMU, at the container level, to be used to perform IOVA allocations
 * @param[in/out] vfio_devices The list of VFIO devices being opened
 * @return Returns true if have obtained the capabilities, or false upon error.
 */
static bool get_type1_iommu_capabilities (vfio_devices_t *const vfio_devices)
{
    int rc;
    struct vfio_iommu_type1_info iommu_info_get_size;

    /* Determine the size required to get the capabilities for the IOMMU.
     * This updates the argsz to indicate how much space is required. */
    memset (&iommu_info_get_size, 0, sizeof iommu_info_get_size);
    iommu_info_get_size.argsz = sizeof (iommu_info_get_size);
    rc = ioctl (vfio_devices->container_fd, VFIO_IOMMU_GET_INFO, &iommu_info_get_size);
    if (rc != 0)
    {
        printf ("VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
        return false;
    }

    /* Allocate a structure of the required size for the IOMMU information, and get it */
    vfio_devices->iommu_info = calloc (iommu_info_get_size.argsz, 1);
    if (vfio_devices->iommu_info == NULL)
    {
        printf ("Failed to allocate %" PRIu32 " bytes for iommu_info\n", iommu_info_get_size.argsz);
        return false;
    }
    vfio_devices->iommu_info->argsz = iommu_info_get_size.argsz;
    rc = ioctl (vfio_devices->container_fd, VFIO_IOMMU_GET_INFO, vfio_devices->iommu_info);
    if (rc != 0)
    {
        printf ("  VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
        return false;
    }

    /* Initialise the free IOVA ranges to the valid IOVA ranges reported by the capabilities.
     * If VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE isn't present, then any attempt to call allocate_vfio_dma_mapping()
     * will fail. */
    if (((vfio_devices->iommu_info->flags & VFIO_IOMMU_INFO_CAPS) != 0) && (vfio_devices->iommu_info->cap_offset > 0))
    {
        const char *const info_start = (const char *) vfio_devices->iommu_info;
        __u32 cap_offset = vfio_devices->iommu_info->cap_offset;

        while ((cap_offset > 0) && (cap_offset < vfio_devices->iommu_info->argsz))
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
                        .allocated = false
                    };

                    update_iova_regions (vfio_devices, &new_region);
                }
            }

            cap_offset = cap_header->next;
        }
    }

    return true;
}


/**
 * @brief Open an VFIO device, without mapping it's memory BARs.
 * @param[in/out] vfio_devices The list of vfio devices to append the opened device to.
 *                             If this function is successful vfio_devices->num_devices is incremented
 * @param[in] pci_dev The PCI device to open using VFIO
 * @param[in] dma_capability Used by this function to determine if to enable the device as a bus master for DMA.
 *                           Stored for later use in allocation IOVA according to the addressing capabilities of the DMA engine.
 */
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev,
                       const vfio_device_dma_capability_t dma_capability)
{
    int rc;
    int saved_errno;
    int api_version;
    vfio_device_t *const new_device = &vfio_devices->devices[vfio_devices->num_devices];
    bool secondary_process = false;

    /* Check the PCI device has an IOMMU group. */
    snprintf (new_device->device_name, sizeof (new_device->device_name), "%04x:%02x:%02x.%x",
            pci_dev->domain, pci_dev->bus, pci_dev->dev, pci_dev->func);
    new_device->iommu_group = pci_get_string_property (pci_dev, PCI_FILL_IOMMU_GROUP);
    if (new_device->iommu_group == NULL)
    {
        printf ("Skipping device %s (%04x:%04x) as no IOMMU group\n",
                new_device->device_name, pci_dev->vendor_id, pci_dev->device_id);
        return;
    }

    /* Save PCI device identification */
    new_device->pci_dev = pci_dev;
    new_device->pci_subsystem_vendor_id = pci_read_word (pci_dev, PCI_SUBSYSTEM_VENDOR_ID);
    new_device->pci_subsystem_device_id = pci_read_word (pci_dev, PCI_SUBSYSTEM_ID);
    new_device->dma_capability = dma_capability;
    new_device->vfio_devices = vfio_devices;

    /* For the first VFIO device open a VFIO container, which is also used for subsequent devices.
     * This is done before trying open the VFIO device to determine which type of IOMMU to use. */
    if (vfio_devices->container_fd == -1)
    {
        /* Determine if we are a secondary process due to the contain already being opened by the primary */
        vfio_devices->container_fd = find_fd_from_primary_process (VFIO_CONTAINER_PATH);
        secondary_process = vfio_devices->container_fd != -1;

        if (!secondary_process)
        {
            /* Are the primary process. Sanity check that the VFIO container path exists, and the user has access */
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

            vfio_devices->container_fd = open (VFIO_CONTAINER_PATH, O_RDWR);
            if (vfio_devices->container_fd == -1)
            {
                fprintf (stderr, "open (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
                exit (EXIT_FAILURE);
            }
        }

        api_version = ioctl (vfio_devices->container_fd, VFIO_GET_API_VERSION);
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
        const int extension_supported = ioctl (vfio_devices->container_fd, VFIO_CHECK_EXTENSION, extension);
        vfio_devices->iommu_type = extension_supported ? VFIO_NOIOMMU_IOMMU : VFIO_TYPE1_IOMMU;
    }

    /* Sanity check that the IOMMU group file exists and the effective user ID has read/write permission before attempting
     * to probe the device. This checks that a script been run to bind the vfio-pci driver
     * (which creates the IOMMU group file) and has given the user permission. */
    snprintf (new_device->group_pathname, sizeof (new_device->group_pathname), "%s%s%s",
            VFIO_ROOT_PATH, (vfio_devices->iommu_type == VFIO_NOIOMMU_IOMMU) ? "noiommu-" : "", new_device->iommu_group);
    errno = 0;
    rc = faccessat (0, new_device->group_pathname, R_OK | W_OK, AT_EACCESS);
    saved_errno = errno;
    if (rc != 0)
    {
        /* Sanity check failed, reported diagnostic message and return to skip the device */
        if (saved_errno == ENOENT)
        {
            printf ("Skipping device %s (%04x:%04x) as %s doesn't exist implying vfio-pci driver not bound to the device\n",
                    new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->group_pathname);
        }
        else if (saved_errno == EACCES)
        {
            printf ("Skipping device %s (%04x:%04x) as %s doesn't have read/write permission\n",
                    new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->group_pathname);
        }
        else
        {
            printf ("Skipping device %s (%04x:%04x) as %s : %s\n",
                    new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->group_pathname,
                    strerror (saved_errno));
        }
        return;
    }

    printf ("Opening device %s (%04x:%04x) with IOMMU group %s\n",
            new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->iommu_group);
    if (secondary_process)
    {
        /* In a secondary process find the group FD which was opened in the primary process */
        new_device->group_fd =  (find_fd_from_primary_process (new_device->group_pathname));
        if (new_device->group_fd == -1)
        {
            printf ("  Secondary process failed to find open fd for %s\n", new_device->group_pathname);
            return;
        }
    }
    else
    {
        /* In the primary process need to open the IOMMU group */
        errno = 0;
        new_device->group_fd = open (new_device->group_pathname, O_RDWR);
        saved_errno = errno;
        if (new_device->group_fd == -1)
        {
            if ((saved_errno == EPERM) && (vfio_devices->iommu_type == VFIO_NOIOMMU_IOMMU))
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
                        new_device->group_pathname, executable_path);
            }
            else
            {
                printf ("open (%s) failed : %s\n", new_device->group_pathname, strerror (errno));
            }
            return;
        }
    }

    /* Get the status of the group and check that viable */
    memset (&new_device->group_status, 0, sizeof (new_device->group_status));
    new_device->group_status.argsz = sizeof (new_device->group_status);
    rc = ioctl (new_device->group_fd, VFIO_GROUP_GET_STATUS, &new_device->group_status);
    if (rc != 0)
    {
        printf ("FIO_GROUP_GET_STATUS failed : %s\n", strerror (-rc));
        return;
    }

    if ((new_device->group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0)
    {
        printf ("group is not viable (ie, not all devices bound for vfio)\n");
        return;
    }

    /* Need to add the group to a container before further IOCTLs are possible */
    if ((new_device->group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) == 0)
    {
        rc = ioctl (new_device->group_fd, VFIO_GROUP_SET_CONTAINER, &vfio_devices->container_fd);
        if (rc != 0)
        {
            printf ("VFIO_GROUP_SET_CONTAINER failed : %s\n", strerror (-rc));
            return;
        }
    }

    if ((vfio_devices->num_devices == 0) && (!secondary_process))
    {
        /* In the primary process set the IOMMU type used */
        rc = ioctl (vfio_devices->container_fd, VFIO_SET_IOMMU, vfio_devices->iommu_type);
        if (rc != 0)
        {
            printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (-rc));
            return;
        }
    }

    if (vfio_devices->num_devices == 0)
    {
        /* When using a type 1 IOMMU, get the capabilities */
        vfio_devices->iommu_info = NULL;
        if (vfio_devices->iommu_type == VFIO_TYPE1_IOMMU)
        {
            if (!get_type1_iommu_capabilities (vfio_devices))
            {
                exit (EXIT_FAILURE);
            }
        }
    }

    /* Open the device */
    new_device->device_fd = ioctl (new_device->group_fd, VFIO_GROUP_GET_DEVICE_FD, new_device->device_name);
    if (new_device->device_fd < 0)
    {
        fprintf (stderr, "VFIO_GROUP_GET_DEVICE_FD (%s) failed : %s\n", new_device->device_name, strerror (-new_device->device_fd));
        return;
    }

    if (new_device->dma_capability != VFIO_DEVICE_DMA_CAPABILITY_NONE)
    {
        /* Ensure the VFIO device is enabled as a PCI bus master */
        uint16_t command = vfio_read_pci_config_word (new_device, PCI_COMMAND);
        if ((command & PCI_COMMAND_MASTER) == 0)
        {
            printf ("Enabling bus master for %s\n", new_device->device_name);
            command |= PCI_COMMAND_MASTER;
            vfio_write_pci_config_word (new_device, PCI_COMMAND, command);
        }
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
    vfio_devices->container_fd = -1;
    vfio_devices->cmem_usage = VFIO_CMEM_USAGE_NONE;
    vfio_devices->iova_regions = NULL;
    vfio_devices->num_iova_regions = 0;
    vfio_devices->iova_regions_allocated_length = 0;
#ifdef HAVE_CMEM
    vfio_devices->num_cmem_mappings = 0;
#endif

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
    const int required_fields = PCI_FILL_IDENT | PCI_FILL_IOMMU_GROUP;
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
 * @brief Close all the open VFIO devices
 * @param[in/out] vfio_devices The VFIO devices to close
 */
void close_vfio_devices (vfio_devices_t *const vfio_devices)
{
    int rc;

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

        rc = close (vfio_device->device_fd);
        if (rc != 0)
        {
            fprintf (stderr, "close (%s) failed : %s\n", vfio_device->device_name, strerror (errno));
            exit (EXIT_FAILURE);
        }
        vfio_device->device_fd = -1;

        rc = close (vfio_device->group_fd);
        if (rc != 0)
        {
            fprintf (stderr, "close (%s) failed : %s\n", vfio_device->group_pathname, strerror (errno));
            exit (EXIT_FAILURE);
        }
        vfio_device->group_fd = -1;
    }

    /* Close the VFIO container if it was used */
    if (vfio_devices->container_fd != -1)
    {
        rc = close (vfio_devices->container_fd);
        if (rc != 0)
        {
            fprintf (stderr, "close (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
            exit (EXIT_FAILURE);
        }
        vfio_devices->container_fd = -1;
    }

#ifdef HAVE_CMEM
    /* Close the cmem driver if it has been opened */
    if (vfio_devices->cmem_usage == VFIO_CMEM_USAGE_DRIVER_OPEN)
    {
        /* Return ignored as the function returns a fixed value of zero */
        (void) cmem_drv_close ();
    }
#endif

    free (vfio_devices->iommu_info);
    vfio_devices->iommu_info = NULL;

    /* Cleanup the PCI access, if was used */
    if (vfio_devices->pacc != NULL)
    {
        pci_cleanup (vfio_devices->pacc);
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
    const int requested_fields = PCI_FILL_IDENT | PCI_FILL_IOMMU_GROUP | PCI_FILL_PHYS_SLOT;
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
                iommu_group = NULL;
                if ((known_fields & PCI_FILL_IOMMU_GROUP) != 0)
                {
                    iommu_group = pci_get_string_property (dev, PCI_FILL_IOMMU_GROUP);
                }
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
 * @brief Allocate a buffer, and create a DMA mapping for the allocated memory.
 * @param[in/out] vfio_device The VFIO device to create the DMA mapping for:
 *                a. The VFIO device is used to determine the DMA address capability to select a suitable IOVA value.
 *                b. The underlying IOMMU containing is used to the allocate the IOVA for the mapping.
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
    vfio_devices_t *const vfio_devices = vfio_device->vfio_devices;
    int rc;
    size_t aligned_size = requested_size;

    mapping->vfio_devices = vfio_devices;
    mapping->num_allocated_bytes = 0;
    if (vfio_devices->iommu_type == VFIO_NOIOMMU_IOMMU)
    {
        /* In NOIOMMU mode allocate IOVA using the contiguous physical memory cmem driver.
         * Open the cmem driver before first use. */
        mapping->buffer.vaddr = NULL;
        if (vfio_devices->cmem_usage == VFIO_CMEM_USAGE_NONE)
        {
#ifdef HAVE_CMEM
            rc = cmem_drv_open ();
            if (rc == 0)
            {
                /* Free any physically contiguous buffers allocated by previous runs using the cmem driver.
                 * Do this after opening the driver as the cmem driver doesn't currently support freeing individual buffers.
                 * This does mean only one process can use the cmem driver at once. */
                rc = cmem_drv_free (0, HOST_BUF_TYPE_DYNAMIC, NULL);
            }
            if (rc == 0)
            {
                vfio_devices->cmem_usage = VFIO_CMEM_USAGE_DRIVER_OPEN;
            }
            else
            {
                printf ("VFIO DMA not supported as failed to open cmem driver\n");
                vfio_devices->cmem_usage = VFIO_CMEM_USAGE_OPEN_FAILED;
            }
#else
            vfio_devices->cmem_usage = VFIO_CMEM_USAGE_SUPPORT_NOT_COMPILED;
            printf ("VFIO DMA not supported as cmem support not compiled in\n");
#endif
        }

#ifdef HAVE_CMEM
        if (vfio_devices->cmem_usage == VFIO_CMEM_USAGE_DRIVER_OPEN)
        {
            const size_t page_size = (size_t) getpagesize ();

            /* Round up the requested size to a multiple of the page size, to ensure the next physical memory allocation
             * starts on a page boundary. Otherwise, the next vaddr will be invalid. */
            aligned_size = (requested_size + (page_size - 1)) & (~(page_size - 1));

            /* cmem driver is open, so attempt the allocation and use the allocated physical memory address as the IOVA
             * to be used for DMA. */
            create_vfio_buffer (&mapping->buffer, aligned_size, VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY, NULL);
            mapping->iova = mapping->buffer.cmem_host_buf_desc.physAddr;
            if (mapping->buffer.vaddr != NULL)
            {
                memset (mapping->buffer.vaddr, 0, mapping->buffer.size);
                mapping->vfio_devices->num_cmem_mappings++;
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
        bool alignment_found = false;

        /* Increase the requested size to be aligned to the smallest page size supported by the IOMMU */
        for (uint64_t page_size = 1; (!alignment_found) && (page_size != 0); page_size <<= 1)
        {
            if ((vfio_devices->iommu_info->iova_pgsizes & page_size) == page_size)
            {
                aligned_size = (requested_size + (page_size - 1)) & (~(page_size - 1));
                alignment_found = true;
            }
        }

        allocate_iova_region (vfio_device, aligned_size, &region);
        if (region.allocated)
        {
            mapping->iova = region.start;

                    /* Create the buffer in the local process. As don't yet have multi-process support uses the PID to make the name unique */
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
                rc = ioctl (vfio_devices->container_fd, VFIO_IOMMU_MAP_DMA, &dma_map);
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
 * @brief Free a DMA mapping, and the associated process virtual memory
 * @param[in/out] mapping The DMA mapping to free.
 */
void free_vfio_dma_mapping (vfio_dma_mapping_t *const mapping)
{
    vfio_devices_t *const vfio_devices = mapping->vfio_devices;
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
        if (vfio_devices->iommu_type == VFIO_NOIOMMU_IOMMU)
        {
            /* Using NOIOMMU so just free the buffer */
            free_vfio_buffer (&mapping->buffer);
        }
        else
        {
            /* Using IOMMU so free the IOMMU DMA mapping and then the buffer */
            rc = ioctl (vfio_devices->container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
            if (rc == 0)
            {
                vfio_iova_region_t free_region =
                {
                    .start = mapping->iova,
                    .end = mapping->iova + (mapping->buffer.size - 1),
                    .allocated = false
                };

                update_iova_regions (vfio_devices, &free_region);
                free_vfio_buffer (&mapping->buffer);
            }
            else
            {
                printf ("VFIO_IOMMU_UNMAP_DMA of size %zu failed : %s\n", mapping->buffer.size, strerror (-rc));
            }
        }

#ifdef HAVE_CMEM
        if (vfio_devices->num_cmem_mappings > 0)
        {
            vfio_devices->num_cmem_mappings--;
            if (vfio_devices->num_cmem_mappings == 0)
            {
                /* All physically continuous buffers allocated by the program are no longer in use.
                 * Free all physically contiguous buffers allocated by previous runs using the cmem driver.
                 * This is done since:
                 * a. The cmem driver doesn't currently support freeing individual buffers.
                 * b. Allows multiple tests to be performed on the same device, in programs which iterate
                 *    over creating and then freeing mappings multiple times. */
                rc = cmem_drv_free (0, HOST_BUF_TYPE_DYNAMIC, NULL);
            }
        }
#endif
    }
}


/**
 * @brief Read a number of bytes from the PCI config space of a VFIO device
 * @details If an error occurs during the read displays diagnostic information and sets the returned bytes to 0xff.
 *          For simplicity looks up the PCI config region on the device for every call.
 * @param[in] vfio_device Device to read from
 * @param[in] offset Offset into the configuration space to read
 * @param[in] num_bytes The number of bytes to read
 * @param[out] config_bytes The bytes which have been read.
 */
static void vfio_read_pci_config_bytes (const vfio_device_t *const vfio_device,
                                        const uint32_t offset, const size_t num_bytes, void *const config_bytes)
{
    int rc;
    ssize_t num_read;
    struct vfio_region_info region_info =
    {
        .argsz = sizeof (region_info),
        .index = VFIO_PCI_CONFIG_REGION_INDEX
    };

    memset (config_bytes, 0xff, num_bytes);

    rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);
    if (rc != 0)
    {
        printf ("  VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
        return;
    }

    num_read = pread (vfio_device->device_fd, config_bytes, num_bytes, (off_t) (region_info.offset + offset));
    if (num_read != (ssize_t) num_bytes)
    {
        printf ("  PCI config read of %zu bytes from offset %" PRIu32 " only read %zd bytes : %s\n",
                num_bytes, offset, num_read, strerror (errno));
        return;
    }
}


/**
 * @brief Read a word from the PCI config space of a VFIO device
 * @param[in] vfio_device Device to read from
 * @param[in] offset Offset in the configuration space to read from
 * @return The word read from PCI config space, or all ones in the event of an error.
 */
uint16_t vfio_read_pci_config_word (const vfio_device_t *const vfio_device, const uint32_t offset)
{
    uint16_t config_word;

    vfio_read_pci_config_bytes (vfio_device, offset, sizeof (config_word), &config_word);

    return config_word;
}


/**
 * @brief Read a long word from the PCI config space of a VFIO device
 * @param[in] vfio_device Device to read from
 * @param[in] offset Offset in the configuration space to read from
 * @return The long word read from PCI config space, or all ones in the event of an error.
 */
uint32_t vfio_read_pci_config_long (const vfio_device_t *const vfio_device, const uint32_t offset)
{
    uint32_t config_long;

    vfio_read_pci_config_bytes (vfio_device, offset, sizeof (config_long), &config_long);

    return config_long;
}


/**
 * @brief Write a number of bytes from the PCI config space of a VFIO device
 * @details If an error occurs during the read displays diagnostic information.
 *          For simplicity looks up the PCI config region on the device for every call.
 * @param[in] vfio_device Device to write to
 * @param[in] offset Offset into the configuration space to write
 * @param[in] num_bytes The number of bytes to write
 * @param[in] config_bytes The bytes to write.
 */
static void vfio_write_pci_config_bytes (const vfio_device_t *const vfio_device,
                                         const uint32_t offset, const size_t num_bytes, const void *const config_bytes)
{
    int rc;
    ssize_t num_written;
    struct vfio_region_info region_info =
    {
        .argsz = sizeof (region_info),
        .index = VFIO_PCI_CONFIG_REGION_INDEX
    };

    rc = ioctl (vfio_device->device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);
    if (rc != 0)
    {
        printf ("  VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
        return;
    }

    num_written = pwrite (vfio_device->device_fd, config_bytes, num_bytes, (off_t) (region_info.offset + offset));
    if (num_written != (ssize_t) num_bytes)
    {
        printf ("  PCI config write of %zu bytes to offset %" PRIu32 " only wrote %zd bytes : %s\n",
                num_bytes, offset, num_written, strerror (errno));
        return;
    }
}


/**
 * @brief Write a word to the PCI config space of a VFIO device
 * @param[in] vfio_device Device to write to
 * @param[in] offset Offset in the configuration space to write to
 * @param[in] config_word Word value to write
 */
void vfio_write_pci_config_word (const vfio_device_t *const vfio_device, const uint32_t offset, const uint16_t config_word)
{
    vfio_write_pci_config_bytes (vfio_device, offset, sizeof (config_word), &config_word);
}


/**
 * @brief Write a long word to the PCI config space of a VFIO device
 * @param[in] vfio_device Device to write to
 * @param[in] offset Offset in the configuration space to write to
 * @param[in] config_long Long word value to write
 */
void vfio_write_pci_config_long (const vfio_device_t *const vfio_device, const uint32_t offset, const uint32_t config_long)
{
    vfio_write_pci_config_bytes (vfio_device, offset, sizeof (config_long), &config_long);
}


/**
 * @brief Display the PCI control word for a VFIO device, for diagnostics
 * @param[in] vfio_device Device to display the PCI control word for
 */
void vfio_display_pci_command (const vfio_device_t *const vfio_device)
{
    const uint16_t command = vfio_read_pci_config_word (vfio_device, PCI_COMMAND);

    printf ("    control: I/O%s Mem%s BusMaster%s\n",
            (command & PCI_COMMAND_IO) ? "+" : "-",
            (command & PCI_COMMAND_MEMORY) ? "+" : "-",
            (command & PCI_COMMAND_MASTER) ? "+" : "-");
}


/**
 * @brief A debugging aid for testing multiprocess VFIO support, by display the file descriptors for the VFIO devices
 * @param[in] vfio_devices The VFIO devices to display the file descriptors for
 */
void vfio_display_fds (const vfio_devices_t *const vfio_devices)
{
    printf ("container_fd=%d\n", vfio_devices->container_fd);
    for (uint32_t device_index = 0; device_index < vfio_devices->num_devices; device_index++)
    {
        const vfio_device_t *const vfio_device = &vfio_devices->devices[device_index];

        printf ("  %s : group_fd=%d device_fd=%d\n", vfio_device->device_name, vfio_device->group_fd, vfio_device->device_fd);
    }
}


/**
 * @details Called in the VFIO primary process to launch secondary process(s) which can use the VFIO devices and VFIO container
 *          opened by the primary process. This is because VFIO devices can only be opened by one process.
 *          This function uses UNIX domain sockets to pass the file descriptors for the VFIO devices and VFIO container
 *          to the launched secondary processes.
 * @param[in/out] vfio_devices The opened VFIO devices to pass to the secondary processes.
 * @param[in] num_processes The number of secondary processes to launch
 * @param[in/out] processes The secondary processes to launch.
 *                          On entry the executable and argv fields define the processes to launch.
 *                          On return the other fields are populated.
 */
void vfio_launch_secondary_processes (vfio_devices_t *const vfio_devices,
                                      const uint32_t num_processes, vfio_secondary_process_t processes[const num_processes])
{
    pid_t pid;

    /* Cleanup the PCI access, to stop any open file descriptors being passed to the secondary processes */
    if (vfio_devices->pacc != NULL)
    {
        pci_cleanup (vfio_devices->pacc);
        vfio_devices->pacc = NULL;
    }

    for (uint32_t process_index = 0; process_index < num_processes; process_index++)
    {
        vfio_secondary_process_t *const process = &processes[process_index];

        pid = fork ();
        if (pid == 0)
        {
            /* In child */
            (void) execv (process->executable, process->argv);

            /* An error has occurred if execv returns */
            fprintf (stderr, "execv (%s) failed : %s\n", process->executable, strerror (errno));
            exit (EXIT_FAILURE);
        }
        else
        {
            /* In parent */
            if (pid <= 0)
            {
                fprintf (stderr, "fork failed : %s\n", strerror (errno));
                exit (EXIT_FAILURE);
            }
            process->pid = pid;
            process->reaped = false;
        }
    }
}


/**
 * @brief Called on the VFIO primary process to wait for the secondary processes to exit
 * @param[in] num_processes The number of secondary processes
 * @param[in/out] processes The secondary processes, launched by a previous call to vfio_launch_secondary_processes()
 */
void vfio_await_secondary_processes (const uint32_t num_processes, vfio_secondary_process_t processes[const num_processes])
{
    uint32_t num_active_processes = num_processes;
    int rc;
    siginfo_t info;
    uint32_t process_index;

    while (num_active_processes > 0)
    {
        rc = waitid (P_ALL, 0, &info, WEXITED);
        if (rc == 0)
        {
            for (process_index = 0; process_index < num_processes; process_index++)
            {
                vfio_secondary_process_t *const process = &processes[process_index];

                if (!process->reaped && (info.si_pid == process->pid))
                {
                    switch (info.si_code)
                    {
                    case CLD_EXITED:
                        if (info.si_status != EXIT_SUCCESS)
                        {
                            printf ("Secondary %s exited with status %d\n", process->executable, info.si_status);
                        }
                        break;

                    case CLD_KILLED:
                    case CLD_DUMPED:
                        printf ("Secondary %s killed with signal %d\n", process->executable, info.si_status);
                    }
                    process->reaped = true;
                    num_active_processes--;
                }
            }
        }

    }
}
