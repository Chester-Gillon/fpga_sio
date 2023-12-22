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

#include "vfio_access.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>

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
 * @param[in/out] dev The device to test
 */
static void test_memmapped_device (vfio_device_t *const dev)
{
    char date_time_text[LAST_ACCESSED_TEXT_LEN];
    struct timeval now;

    /* Indicate the date/time expected to be set in the last accessed text, and possibly initialised text */
    (void) gettimeofday (&now, NULL);
    (void) ctime_r (&now.tv_sec, date_time_text);
    printf ("Now: %s\n", date_time_text);

    printf ("Testing device %s in IOMMU group %s\n", dev->device_name, dev->iommu_group);

    /* Test all possible BARs */
    for (uint32_t bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        map_vfio_device_bar_before_use (dev, bar_index);

        const struct vfio_region_info *const region_info = &dev->regions_info[bar_index];
        memmapped_data_t *const mapping = (memmapped_data_t *) dev->mapped_bars[bar_index];
        const char *const initialised_text_prefix = initialised_text_prefixes[bar_index];

        if (mapping != NULL)
        {
            printf ("BAR %u\n", bar_index);

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
                const uint8_t *const memory_bytes = dev->mapped_bars[bar_index];
                struct timespec start_time;
                struct timespec end_time;

                clock_gettime (CLOCK_MONOTONIC, &start_time);
                for (__u64 byte_index = 0; byte_index < region_info->size; byte_index++)
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

                if (num_zero_bytes == region_info->size)
                {
                    printf ("  Uninitialised memory region of %llu bytes all zeros\n", region_info->size);
                }
                else
                {
                    printf ("  Uninitialised memory region of %llu contains %" PRIu64 " zero bytes and %" PRIu64 " 0xff bytes\n",
                            region_info->size, num_zero_bytes, num_all_ones_bytes);
                }
                printf ("  Total time for byte reads from memory region = %" PRIi64 " ns, or average of %" PRIi64 " ns per byte\n",
                        read_duration_ns, read_duration_ns / (int64_t) region_info->size);

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
        }
    }
}


int main (int argc, char *argv[])
{
    int rc;
    vfio_devices_t vfio_devices;

    /* Filters for the FPGA devices tested */
    const vfio_pci_device_identity_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_MEMMAPPED_BLKRAM,
            .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    /* Attempt to lock all future pages to see if has any effect on PAT mapping of BARs */
    rc = mlockall (MCL_CURRENT | MCL_FUTURE);
    if (rc != 0)
    {
        printf ("mlockall() failed : %s\n", strerror (errno));
    }

    /* If any command line option is specified then causes the device to be reset before use */
    const bool reset_device_before_use = argc > 1;

    /* Open PCI devices supported by the test */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    /* Perform tests on the FPGA devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        if (reset_device_before_use)
        {
            reset_vfio_device (vfio_device);
        }
        test_memmapped_device (vfio_device);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
