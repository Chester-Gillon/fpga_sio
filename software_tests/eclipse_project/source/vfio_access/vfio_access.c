/*
 * @file vfio_access.c
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Implement a library to allow access to device using VFIO
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
 * @brief Open an VFIO device, and map all its memory BARs
 * @param[in/out] vfio_devices The list of vfio devices to append the opened device to.
 *                             If this function is successful vfio_devices->num_devices is incremented
 * @param[in] pci_dev The PCI device to open using VFIO
 */
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev)
{
    int rc;
    int saved_errno;
    int api_version;
    void *addr;
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
        /* Set the IOMMU type used. As this is done on the IOMMU container, only performed when the first device is opened */
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

    /* Get the device information. As this program is written for a PCI device which has fixed enumerations for regions,
     * the only use of the device information is a sanity check that VFIO reports a PCI device. */
    memset (&new_device->device_info, 0, sizeof (new_device->device_info));
    new_device->device_info.argsz = sizeof (new_device->device_info);
    rc = ioctl (new_device->device_fd, VFIO_DEVICE_GET_INFO, &new_device->device_info);
    if (rc != 0)
    {
        printf ("VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
        return;
    }

    if ((new_device->device_info.flags & VFIO_DEVICE_FLAGS_PCI) == 0)
    {
        printf ("VFIO_DEVICE_GET_INFO flags don't report a PCI device\n");
        return;
    }

    /* Map all possible BARs */
    for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        struct vfio_region_info *const region_info = &new_device->regions_info[bar_index];

        /* Get region information for PCI BAR, to determine if an implemented BAR which can be mapped */
        memset (region_info, 0, sizeof (*region_info));
        region_info->argsz = sizeof (*region_info);
        region_info->index = bar_index;
        rc = ioctl (new_device->device_fd, VFIO_DEVICE_GET_REGION_INFO, region_info);
        if (rc != 0)
        {
            printf ("VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
            return;
        }


        if ((region_info->size > 0) && ((region_info->flags & VFIO_REGION_INFO_FLAG_MMAP) != 0))
        {
            /* Map the entire BAR */
            addr = mmap (NULL, region_info->size, PROT_READ | PROT_WRITE, MAP_SHARED, new_device->device_fd, region_info->offset);
            if (addr == MAP_FAILED)
            {
                printf ("mmap() failed : %s\n", strerror (errno));
                return;
            }
            new_device->mapped_bars[bar_index] = addr;
        }
        else
        {
            new_device->mapped_bars[bar_index] = NULL;
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
 * @brief Scan the PCI bus, attempting to open all devices using VFIO which match the filter.
 * @details If an error occurs attempting to open the VFIO device then a message is output to the console and the
 *          offending device isn't returned in vfio_devices.
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
