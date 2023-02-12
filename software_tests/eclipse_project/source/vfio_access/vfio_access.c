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
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>


/*
 * Paths for the VFIO character devices
 */
#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


/**
 * @brief Create a memory buffer to be used for VFIO
 * @param[out] buffer The created memory buffer, which has been mapped into the virtual address space
 * @param[in] size The size in bytes of the buffer to create
 * @param[in] buffer_allocation How to allocate the buffer
 * @param[in] name_suffix For VFIO_BUFFER_ALLOCATION_SHARED_MEMORY a suffix used to create a unique name
 */
static void create_vfio_buffer (vfio_buffer_t *const buffer,
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

        rc = posix_fallocate (buffer->fd, 0, buffer->size);
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
            printf ("shm_open(%s) failed : %s\n", buffer->pathname, strerror (errno));
            return;
        }
        break;
    }
}


/**
 * @brief Release the resources for a memory buffer used for VFIO
 * @param[in/out] buffer The memory buffer to release
 */
static void free_vfio_buffer (vfio_buffer_t *const buffer)
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
void map_vfio_device_bar_before_use (vfio_device_t *const vfio_device, const int bar_index)
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
            addr = mmap (NULL, region_info->size, PROT_READ | PROT_WRITE, MAP_SHARED, vfio_device->device_fd, region_info->offset);
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
 * @brief Open an VFIO device, without mapping it's memory BARs.
 * @param[in/out] vfio_devices The list of vfio devices to append the opened device to.
 *                             If this function is successful vfio_devices->num_devices is incremented
 * @param[in] pci_dev The PCI device to open using VFIO
 */
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev)
{
    int rc;
    int saved_errno;
    int api_version;
    vfio_device_t *const new_device = &vfio_devices->devices[vfio_devices->num_devices];

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

    /* Sanity check that the IOMMU group file exists and the effective user ID has read/write permission before attempting
     * to probe the device. This checks that bind_xilinx_devices_to_vfio.sh script has been run to bind the vfio-pci driver
     * (which creates the IOMMU group file) and has given the user permission. */
    snprintf (new_device->group_pathname, sizeof (new_device->group_pathname), "%s%s",
            VFIO_ROOT_PATH, new_device->iommu_group);
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

    /* For the first VFIO device open a VFIO container, which is also used for subsequent devices */
    if (vfio_devices->num_devices == 0)
    {
        vfio_devices->container_fd = open (VFIO_CONTAINER_PATH, O_RDWR);
        if (vfio_devices->container_fd == -1)
        {
            fprintf (stderr, "open (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
            exit (EXIT_FAILURE);
        }

        api_version = ioctl (vfio_devices->container_fd, VFIO_GET_API_VERSION);
        if (api_version != VFIO_API_VERSION)
        {
            fprintf (stderr, "Got VFIO_API_VERSION %d, expected %d\n", api_version, VFIO_API_VERSION);
            exit (EXIT_FAILURE);
        }
    }

    /* Open the IOMMU group */
    printf ("Opening device %s (%04x:%04x) with IOMMU group %s\n",
            new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->iommu_group);
    snprintf (new_device->group_pathname, sizeof (new_device->group_pathname), "%s%s", VFIO_ROOT_PATH, new_device->iommu_group);
    new_device->group_fd = open (new_device->group_pathname, O_RDWR);
    if (new_device->group_fd == -1)
    {
        printf ("open (%s) failed : %s\n", new_device->group_pathname, strerror (errno));
        return;
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

    if (vfio_devices->num_devices == 0)
    {
        /* Set the IOMMU type used. As this is done on the IOMMU container, only performed when the first device is opened.
         * Uses the fixed VFIO_TYPE1_IOMMU, as DPDK does for x86.
         *
         * While support for VFIO_TYPE1v2_IOMMU and VFIO_TYPE1_NESTING_IOMMU was available on the Intel Xeon W system tested,
         * not sure of the benefits of using a different IOMMU type. */
        rc = ioctl (vfio_devices->container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
        if (rc != 0)
        {
            printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (-rc));
            return;
        }
    }

    /* Open the device */
    new_device->device_fd = ioctl (new_device->group_fd, VFIO_GROUP_GET_DEVICE_FD, new_device->device_name);
    if (new_device->device_fd < 0)
    {
        fprintf (stderr, "VFIO_GROUP_GET_DEVICE_FD (%s) failed : %s\n", new_device->device_name, strerror (-new_device->device_fd));
        return;
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
 * @brief Scan the PCI bus, attempting to open all devices using VFIO which match the filter.
 * @details If an error occurs attempting to open the VFIO device then a message is output to the console and the
 *          offending device isn't returned in vfio_devices.
 *          The memory BARs of the VFIO devices are not mapped.
 * @param[out] vfio_devices The list of opened VFIO devices
 * @param[in] num_filters The number of PCI device filters
 * @param[in] filters The filters for PCI devices to open
 */
void open_vfio_devices_matching_filter (vfio_devices_t *const vfio_devices,
                                        const size_t num_filters, const vfio_pci_device_filter_t filters[const num_filters])
{
    struct pci_dev *dev;
    int known_fields;
    u16 subsystem_vendor_id;
    u16 subsystem_device_id;
    bool pci_device_matches_filter;

    memset (vfio_devices, 0, sizeof (*vfio_devices));

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
            pci_device_matches_filter = false;
            for (size_t filter_index = 0; (!pci_device_matches_filter) && (filter_index < num_filters); filter_index++)
            {
                const vfio_pci_device_filter_t *const filter = &filters[filter_index];

                pci_device_matches_filter = pci_filter_id_match (dev->vendor_id, filter->vendor_id) &&
                        pci_filter_id_match (dev->device_id , filter->device_id);
                if (pci_device_matches_filter)
                {
                    subsystem_vendor_id = pci_read_word (dev, PCI_SUBSYSTEM_VENDOR_ID);
                    subsystem_device_id = pci_read_word (dev, PCI_SUBSYSTEM_ID);
                    pci_device_matches_filter = pci_filter_id_match (subsystem_vendor_id, filter->subsystem_vendor_id) &&
                            pci_filter_id_match (subsystem_device_id, filter->subsystem_device_id);
                }
            }

            if (pci_device_matches_filter)
            {
                open_vfio_device (vfio_devices, dev);
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

    rc = close (vfio_devices->container_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }
    vfio_devices->container_fd = -1;

    /* Cleanup the PCI access, if was used */
    if (vfio_devices->pacc != NULL)
    {
        pci_cleanup (vfio_devices->pacc);
    }
}


/**
 * @brief Allocate a buffer, and create a DMA mapping for the allocated memory.
 * @param[in/out] vfio_devices The VFIO devices context used to create the DMA mapping using the IOMMU container.
 *                             Used to allocate the iova address used for the mapping.
 * @param[out] mapping Contains the process memory and associated DMA mapping which has been allocated.
 *                     On failure, mapping->buffer.vaddr is NULL
 * @param[in] size The size in bytes to allocate
 * @param[in] permission Bitwise OR VFIO_DMA_MAP_FLAG_READ / VFIO_DMA_MAP_FLAG_WRITE flags to define
 *                       the device access to the DMA mapping.
 * @param[in] buffer_allocation Controls how the buffer for the process is allocated
 */
void allocate_vfio_dma_mapping (vfio_devices_t *const vfio_devices,
                                vfio_dma_mapping_t *const mapping,
                                const size_t size, const uint32_t permission,
                                const vfio_buffer_allocation_type_t buffer_allocation)
{
    int rc;
    struct vfio_iommu_type1_dma_map dma_map;
    char name_suffix[PATH_MAX];

    /* @todo For simplicity assume an incrementing IOVA for each allocation, without regard to any container constraints.
     *       If this attempts to allocate an invalid iova VFIO_IOMMU_MAP_DMA will fail with EPERM
     *       Consider making use of VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE to find a valid iova range. */
    mapping->iova = vfio_devices->next_iova;

    /* Create the buffer in the local process. As don't yet have multi-process support uses the PID to make the name unique */
    snprintf (name_suffix, sizeof (name_suffix), "pid-%d_iova-%" PRIu64, getpid(), mapping->iova);
    create_vfio_buffer (&mapping->buffer, size, buffer_allocation, name_suffix);

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
        if (rc == 0)
        {
            vfio_devices->next_iova += mapping->buffer.size;
        }
        else
        {
            printf ("VFIO_IOMMU_MAP_DMA of size %zu failed : %s\n", mapping->buffer.size, strerror (-rc));
            free (mapping->buffer.vaddr);
            mapping->buffer.vaddr = NULL;
        }
    }
    else
    {
        mapping->buffer.vaddr = NULL;
        printf ("Failed to allocate %zu bytes for VFIO DMA mapping\n", size);
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
    const size_t cache_line_size = 64;

    mapping->num_allocated_bytes = ((mapping->num_allocated_bytes + (cache_line_size - 1)) / cache_line_size) * cache_line_size;
}


/**
 * @brief Free a DMA mapping, and the associated process virtual memory
 * @param[in] vfio_devices The VFIO devices context containing the IOMMU container to free the mapping for
 * @param[in/out] mapping The DMA mapping to free.
 */
void free_vfio_dma_mapping (const vfio_devices_t *const vfio_devices, vfio_dma_mapping_t *const mapping)
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
        rc = ioctl (vfio_devices->container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
        if (rc == 0)
        {
            free_vfio_buffer (&mapping->buffer);
        }
        else
        {
            printf ("VFIO_IOMMU_UNMAP_DMA of size %zu failed : %s\n", mapping->buffer.size, strerror (-rc));
        }
    }
}
