/*
 * @file memmapped_persistence_vfio.c
 * @date 31 Dec 2022
 * @author Chester Gillon
 * @brief Perform a test of FPGA memory mapped persistence, using vfio to map the FPGA BARs
 * @details
 *   Where persistence means if the memory in different BARs can maintain it's content between runs of this program
 *   and across reboots of the PC.
 *
 *   Uses libpci to find the IOMMU group of the FPGA device, then uses vfio to operate on the FPGA device.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pci/pci.h>
#include <linux/vfio.h>

#include "fpga_sio_pci_ids.h"


#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


/* Text prefix use to initialise the memory of each BAR */
static const char *const initialised_text_prefixes[PCI_STD_NUM_BARS] =
{
    "This is BAR zero memory initialised at ",
    "This is BAR one memory initialised at ",
    "This is BAR two memory initialised at ",
    "This is BAR three memory initialised at ",
    "This is BAR four memory initialised at ",
    "This is BAR five memory initialised at "
};


/* Structure placed at the start of a memory mapped BAR to provide some data which can be read/written each time this program
 * is run. */
#define INITIALISED_TEXT_LEN 120
#define LAST_ACCESSED_TEXT_LEN 40
typedef struct
{
    /* A string set when this program first accesses the memory.
     * The prefix is used to determine if the BAR has been initialised previously.
     * Contains the date/time the BAR was initialised. */
    char initialised_text[INITIALISED_TEXT_LEN];
    /* Set to the date/time of the last access made to the memory */
    char last_accessed_text[LAST_ACCESSED_TEXT_LEN];
    /* Incremented every time this program accesses the memory */
    uint32_t accessed_count;
} memmapped_data_t;


/**
 * @brief Perform a test of FPGA memory mapped persistence on one PCI device.
 * @param[in] dev The device to test
 * @param[in] iommu_group The IOMMU group of the device, used to map the BARs of the device using vfio
 */
static void test_memmapped_device (const struct pci_dev *const dev, const char *const iommu_group)
{
    int rc;
    int container_fd;
    int group_fd;
    int device_fd;
    int api_version;
    char device_name[64];
    char group_pathname[PATH_MAX];
    struct vfio_group_status group_status;
    struct vfio_device_info device_info;
    struct vfio_region_info region_info;
    void *addr;
    char date_time_text[LAST_ACCESSED_TEXT_LEN];
    struct timeval now;

    /* Indicate the date/time expected to be set in the last accessed text, and possibly initialised text */
    (void) gettimeofday (&now, NULL);
    (void) ctime_r (&now.tv_sec, date_time_text);
    printf ("Now: %s\n", date_time_text);

    snprintf (device_name, sizeof (device_name), "%04x:%02x:%02x.%x", dev->domain, dev->bus, dev->dev, dev->func);
    printf ("Testing device %s in IOMMU group %s\n", device_name, iommu_group);

    /* Open an IOMMU container for the test */
    container_fd = open (VFIO_CONTAINER_PATH, O_RDWR);
    if (container_fd == -1)
    {
        fprintf (stderr, "open (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }

    api_version = ioctl (container_fd, VFIO_GET_API_VERSION);
    if (api_version != VFIO_API_VERSION)
    {
        fprintf (stderr, "Got VFIO_API_VERSION %d, expected %d\n", api_version, VFIO_API_VERSION);
        exit (EXIT_FAILURE);
    }

    /* Open the IOMMU group */
    snprintf (group_pathname, sizeof (group_pathname), "%s%s", VFIO_ROOT_PATH, iommu_group);
    group_fd = open (group_pathname, O_RDWR);
    if (group_fd == -1)
    {
        printf ("open (%s) failed : %s\n", group_pathname, strerror (errno));
        exit (EXIT_FAILURE);
    }

    /* Get the status of the group and check that viable */
    memset (&group_status, 0, sizeof (group_status));
    group_status.argsz = sizeof (group_status);
    rc = ioctl (group_fd, VFIO_GROUP_GET_STATUS, &group_status);
    if (rc != 0)
    {
        printf ("FIO_GROUP_GET_STATUS failed : %s\n", strerror (-rc));
        exit (EXIT_FAILURE);
    }

    if ((group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0)
    {
        printf ("group is not viable (ie, not all devices bound for vfio)\n");
        exit (EXIT_FAILURE);
    }
    /* Need to add the group to a container before further IOCTLs are possible */
    if ((group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) == 0)
    {
        rc = ioctl (group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd);
        if (rc != 0)
        {
            printf ("VFIO_GROUP_SET_CONTAINER failed : %s\n", strerror (-rc));
            exit (EXIT_FAILURE);
        }
    }

    /* Set the IOMMU type used */
    rc = ioctl (container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    if (rc != 0)
    {
        printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (-rc));
        exit (EXIT_FAILURE);
    }

    /* Open the device */
    device_fd = ioctl (group_fd, VFIO_GROUP_GET_DEVICE_FD, device_name);
    if (device_fd < 0)
    {
        fprintf (stderr, "VFIO_GROUP_GET_DEVICE_FD (%s) failed : %s\n", device_name, strerror (-device_fd));
        exit (EXIT_FAILURE);
    }

    /* Get the device information. As this program is written for a PCI device which has fixed enumerations for regions,
     * the only use of the device information is a sanity check that VFIO reports a PCI device. */
    memset (&device_info, 0, sizeof (device_info));
    device_info.argsz = sizeof (device_info);
    rc = ioctl (device_fd, VFIO_DEVICE_GET_INFO, &device_info);
    if (rc != 0)
    {
        printf ("VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
        exit (EXIT_FAILURE);
    }

    if ((device_info.flags & VFIO_DEVICE_FLAGS_PCI) == 0)
    {
        printf ("VFIO_DEVICE_GET_INFO flags don't report a PCI device\n");
        exit (EXIT_FAILURE);
    }

    /* Test all possible BARs */
    for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        const char *const initialised_text_prefix = initialised_text_prefixes[bar_index];

        /* Get region information for PCI BAR, to determine if an implemented BAR which can be mapped */
        memset (&region_info, 0, sizeof (region_info));
        region_info.argsz = sizeof (region_info);
        region_info.index = bar_index;
        rc = ioctl (device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);
        if (rc != 0)
        {
            printf ("VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
            exit (EXIT_FAILURE);
        }

        if ((region_info.size > 0) && ((region_info.flags & VFIO_REGION_INFO_FLAG_MMAP) != 0))
        {
            /* Map the entire BAR */
            printf ("BAR %u\n", bar_index);
            addr = mmap (NULL, region_info.size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, region_info.offset);
            if (addr == MAP_FAILED)
            {
                printf ("mmap() failed : %s\n", strerror (errno));
                exit (EXIT_FAILURE);
            }

            memmapped_data_t *const mapping = addr;

            /* Determine if the memory has already been initialised */
            if (strncmp (mapping->initialised_text, initialised_text_prefix, strlen (initialised_text_prefix)) == 0)
            {
                printf ("  Memory already initialised - existing last_accessed_text=%.*s",
                        LAST_ACCESSED_TEXT_LEN, mapping->last_accessed_text);
            }
            else
            {
                /* The memory doesn't start with the initialised text, determine if:
                 * a. All zeros to see if blkram starts from a known value.
                 * b. All ones to see the effect of a surprise PCIe device removal caused by re-loading the FPGA
                 *    after Linux has booted. */
                pciaddr_t num_zero_bytes = 0;
                pciaddr_t num_all_ones_bytes = 0;
                const uint8_t *const memory_bytes = addr;
                struct timespec start_time;
                struct timespec end_time;

                clock_gettime (CLOCK_MONOTONIC, &start_time);
                for (__u64 byte_index = 0; byte_index < region_info.size; byte_index++)
                {
                    if (memory_bytes[byte_index] == 0)
                    {
                        num_zero_bytes++;
                    }
                    else if (memory_bytes[byte_index] == 0xff)
                    {
                        num_all_ones_bytes++;
                    }
                }
                clock_gettime (CLOCK_MONOTONIC, &end_time);

                const int64_t start_time_ns = (start_time.tv_sec * 1000000000LL) + start_time.tv_nsec;
                const int64_t end_time_ns = (end_time.tv_sec * 1000000000LL) + end_time.tv_nsec;
                const int64_t read_duration_ns = end_time_ns - start_time_ns;

                if (num_zero_bytes == region_info.size)
                {
                    printf ("  Uninitialised memory region of %llu bytes all zeros\n", region_info.size);
                }
                else
                {
                    printf ("  Uninitialised memory region of %llu contains %" PRIu64 " zero bytes and %" PRIu64 " 0xff bytes\n",
                            region_info.size, num_zero_bytes, num_all_ones_bytes);
                }
                printf ("  Total time for byte reads from memory region = %" PRIi64 " ns, or average of %" PRIi64 " ns per byte\n",
                        read_duration_ns, read_duration_ns / (int64_t) region_info.size);

                /* Initialise the memory */
                (void) snprintf (mapping->initialised_text, sizeof (mapping->initialised_text), "%s%s",
                        initialised_text_prefix, date_time_text);
                mapping->accessed_count = 0u;
            }

            /* Update memory to record the access */
            (void) snprintf (mapping->last_accessed_text, sizeof (mapping->last_accessed_text), "%s", date_time_text);
            mapping->accessed_count++;

            /* Display the content of the mapped memory */
            printf ("  initialised_text=%.*s", INITIALISED_TEXT_LEN, mapping->initialised_text);
            printf ("  new last_accessed_text=%.*s", LAST_ACCESSED_TEXT_LEN, mapping->last_accessed_text);
            printf ("  accessed_count=%" PRIu32 "\n", mapping->accessed_count);

            /* Unmap the BAR */
            rc = munmap (addr, region_info.size);
            if (rc != 0)
            {
                printf ("munmap() failed : %s\n", strerror (errno));
                exit (EXIT_FAILURE);
            }
        }
    }

    rc = close (device_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", device_name, strerror (errno));
        exit (EXIT_FAILURE);
    }

    rc = close (group_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", group_pathname, strerror (errno));
        exit (EXIT_FAILURE);
    }

    rc = close (container_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }
}


int main (int argc, char *argv[])
{
    int rc;
    struct pci_access *pacc;
    struct pci_filter filter;
    struct pci_dev *dev;
    int known_fields;
    char *iommu_group;
    u16 subvendor_id;
    u16 subdevice_id;

    /* Attempt to lock all future pages to see if has any effect on PAT mapping of BARs */
    rc = mlockall (MCL_CURRENT | MCL_FUTURE);
    if (rc != 0)
    {
        printf ("mlockall() failed : %s\n", strerror (errno));
    }

    /* Initialise using the defaults */
    pacc = pci_alloc ();
    if (pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (pacc);

    /* Select to filter by vendor only */
    pci_filter_init (pacc, &filter);
    filter.vendor = FPGA_SIO_VENDOR_ID;

    /* Scan the entire bus */
    pci_scan_bus (pacc);

    /* Perform tests on the FPGA devices which have an IOMMU group assigned */
    const int required_fields = PCI_FILL_IDENT | PCI_FILL_IOMMU_GROUP;
    for (dev = pacc->devices; dev != NULL; dev = dev->next)
    {
        if (pci_filter_match (&filter, dev))
        {
            subvendor_id = pci_read_word (dev, PCI_SUBSYSTEM_VENDOR_ID);
            subdevice_id = pci_read_word (dev, PCI_SUBSYSTEM_ID);
            if (subvendor_id == FPGA_SIO_SUBVENDOR_ID)
            {
                switch (subdevice_id)
                {
                case FPGA_SIO_SUBDEVICE_ID_MEMMAPPED_BLKRAM:
                    known_fields = pci_fill_info (dev, required_fields);
                    if ((known_fields & required_fields) == required_fields)
                    {
                        iommu_group = pci_get_string_property (dev, PCI_FILL_IOMMU_GROUP);
                        if (iommu_group != NULL)
                        {
                            test_memmapped_device (dev, iommu_group);
                        }
                    }
                    break;
                }
            }
        }
    }

    pci_cleanup (pacc);

    return EXIT_SUCCESS;
}
