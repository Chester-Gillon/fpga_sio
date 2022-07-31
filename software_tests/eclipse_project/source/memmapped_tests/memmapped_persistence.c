/*
 * @file memmapped_persistence.c
 * @date Sep 15, 2021
 * @author Chester Gillon
 * @brief Perform a test of FPGA memory mapped persistence
 * @details Where persistence means if the memory in different BARs can maintain it's content between runs of this program
 *          and across reboots of the PC.
 *
 *          Did try and add code to parse /proc/self/pagemap to resolve the virtual address of the BAR mapping
 *          to the physical address. However:
 *          a. When tested with a 3.10.33-rt32.33.el6rt.x86_64 Kernel from Scientific Linux 6.6 the reported
 *             physical address appeared "random". I.e. a non-zero value which seemed to be RAM.
 *          b. When tested with a 4.18.0-372.16.1.el8_6.x86_64 Kernel from AlmaLinux 8.6 the reported physical
 *             address was zero.
 *
 *          https://unix.stackexchange.com/questions/284017/pagemap-on-memory-mapped-devices-not-working
 *          explains that for memory mapped devices the mapping doesn't have a struct page associated with
 *          them, and so the pagemap interface can't report the physical address.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <time.h>
#include <sys/time.h>
#include <pciaccess.h>

#include "fpga_sio_pci_ids.h"


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


int main (int argc, char *argv[])
{
    int rc;
    void *addr;

    rc = pci_system_init ();
    if (rc != 0)
    {
        fprintf (stderr, "pci_system_init failed\n");
        exit (EXIT_FAILURE);
    }

    struct pci_id_match match =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = PCI_MATCH_ANY,
        .subvendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subdevice_id = PCI_MATCH_ANY,
        .device_class = 0,
        .device_class_mask = 0
    };

    char date_time_text[LAST_ACCESSED_TEXT_LEN];
    struct timeval now;

    /* Indicate the date/time expected to be set in the last accessed text, and possibly initialised text */
    (void) gettimeofday (&now, NULL);
    (void) ctime_r (&now.tv_sec, date_time_text);
    printf ("Now: %s\n", date_time_text);

    struct pci_device_iterator *const device_iterator = pci_id_match_iterator_create (&match);
    struct pci_device *device;

    device = pci_device_next (device_iterator);
    while (device != NULL)
    {
        switch (device->subdevice_id)
        {
        case FPGA_SIO_SUBDEVICE_ID_MEMMAPPED_BLKRAM:
            rc = pci_device_probe (device);
            if (rc == 0)
            {
                for (unsigned bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
                {
                    const struct pci_mem_region *const region = &device->regions[bar_index];
                    const char *const initialised_text_prefix = initialised_text_prefixes[bar_index];

                    if (region->size > 0)
                    {
                        printf ("BAR %u\n", bar_index);

                        /* Map the entire BAR */
                        addr = NULL;
                        rc = pci_device_map_range (device, region->base_addr, region->size,
                                PCI_DEV_MAP_FLAG_WRITABLE | PCI_DEV_MAP_FLAG_WRITE_COMBINE, &addr);
                        if (rc != 0)
                        {
                            fprintf (stderr, "pci_device_map_region failed:\n%s", strerror (rc));
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

                            for (pciaddr_t byte_index = 0; byte_index < region->size; byte_index++)
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

                            if (num_zero_bytes == region->size)
                            {
                                printf ("  Uninitialised memory region of %" PRIu64 " bytes all zeros\n", region->size);
                            }
                            else
                            {
                                printf ("  Uninitialised memory region of %" PRIu64 " contains %" PRIu64 " zero bytes and %" PRIu64 " 0xff bytes\n",
                                        region->size, num_zero_bytes, num_all_ones_bytes);
                            }

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
                        rc = pci_device_unmap_range (device, addr, region->size);
                        if (rc != 0)
                        {
                            fprintf (stderr, "pci_device_unmap_range failed:\n%s", strerror (rc));
                            exit (EXIT_FAILURE);
                        }
                    }
                }
            }
            break;
        }
        device = pci_device_next (device_iterator);
    }
    return EXIT_SUCCESS;
}
