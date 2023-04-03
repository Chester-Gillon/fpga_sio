/*
 * @file time_memmapped_libpciaccess.c
 * @date 3 Apr 2023
 * @author Chester Gillon
 * @brief Program to time accessing the FPGA memmapped block RAM, using libpciaccess to access the device
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <pciaccess.h>

#include "vfio_access.h" /* Just to allocate buffers from the heap */
#include "transfer_timing.h"
#include "fpga_sio_pci_ids.h"


/**
 * @brief Test the shared memory, using the CPU to access the entire shared memory.
 * @details Writes a test pattern to the entire shared memory, and then reads back and checks the test pattern to verify the
 *          shared memory contains the expected data.
 *          The shared memory is accessed via memcpy() and doesn't attempt to optimise the transfer in terms
 *          transactions over the PCIe bus.
 * @param[in] device Used to obtain the size of the BAR being tested
 * @param[in] bar_index Which BAR in device containing the memory is being tested.
 * @param[in/out] shared_memory The mapped shared memory to test.
 * @param[in] mapping_description Describes how the shared memory is mapped
 * @param[in] flush_wc_buffer If true flushes the posted write queue before stopping the write timing
 */
static void test_memory_mapped_bar (struct pci_device *const device,
                                    const unsigned bar_index,
                                    void *const shared_memory,
                                    const char *mapping_description,
                                    const bool flush_wc_buffer)
{
    const struct pci_mem_region *const region = &device->regions[bar_index];
    const size_t shared_memory_size_words = region->size / sizeof (uint32_t);
    uint32_t host_test_pattern;
    uint32_t card_test_pattern;
    bool success;
    char timing_description[128];
    transfer_timing_t host_to_card_timing;
    transfer_timing_t card_to_host_timing;
    vfio_buffer_t h2c_buffer;
    vfio_buffer_t c2h_buffer;

    /* Allocate host buffers on the heap */
    create_vfio_buffer (&h2c_buffer, region->size, VFIO_BUFFER_ALLOCATION_HEAP, NULL);
    create_vfio_buffer (&c2h_buffer, region->size, VFIO_BUFFER_ALLOCATION_HEAP, NULL);
    if ((h2c_buffer.vaddr == NULL) || (c2h_buffer.vaddr == NULL))
    {
        return;
    }
    uint32_t *host_words = h2c_buffer.vaddr;
    uint32_t *card_words = c2h_buffer.vaddr;

    printf ("Testing BAR %u memory size 0x%zx for domain=%04x bus=%02x dev=%02x func=%02x\n  vendor_id=%04x (%s) device_id=%04x (%s) subvendor_id=%04x subdevice_id=%04x\n",
            bar_index, region->size,
            device->domain, device->bus, device->dev, device->func,
            device->vendor_id, pci_device_get_vendor_name (device),
            device->device_id, pci_device_get_device_name (device),
            device->subvendor_id, device->subdevice_id);

    snprintf (timing_description, sizeof (timing_description), "host-to-card PIO mapped with %s", mapping_description);
    initialise_transfer_timing (&host_to_card_timing, timing_description, region->size);
    snprintf (timing_description, sizeof (timing_description), "card-to-host PIO mapped with %s", mapping_description);
    initialise_transfer_timing (&card_to_host_timing, timing_description, region->size);

    /* Start the test pattern at which is at the start of the shared memory */
    memcpy (&host_test_pattern, shared_memory, sizeof (host_test_pattern));
    linear_congruential_generator (&host_test_pattern);

    /* Perform a number of test iterations to get multiple timing measurements */
    success = true;
    for (size_t total_bytes = 0; total_bytes < 0x4000000; total_bytes += region->size)
    {
        /* Fill the host buffer with a test pattern to write to the NVRAM contents */
        card_test_pattern = host_test_pattern;
        for (size_t word_index = 0; word_index < shared_memory_size_words; word_index++)
        {
            host_words[word_index] = host_test_pattern;
            linear_congruential_generator (&host_test_pattern);
        }

        /* Use the CPU to copy the test pattern to the shared memory */
        transfer_time_start (&host_to_card_timing);
        memcpy (shared_memory, host_words, region->size);
        if (flush_wc_buffer)
        {
            /* Flush the post write queue, to avoid report a higher transfer rate than actually achieved by the device.
             *
             * See the "What happens if you read from write-combined memory?" section from
             * https://fgiesen.wordpress.com/2013/01/29/write-combining-is-not-your-friend/ says:
             */
            uint32_t *const shared_memory_word = shared_memory;
            (void) __atomic_load_n (shared_memory_word, __ATOMIC_ACQUIRE);
        }
        transfer_time_stop (&host_to_card_timing);

        /* Use the CPU to copy the test pattern from the shared memory at a time */
        transfer_time_start (&card_to_host_timing);
        memcpy (card_words, shared_memory, region->size);
        transfer_time_stop (&card_to_host_timing);

        /* Verify the test pattern */
        for (size_t word_offset = 0; success && (word_offset < shared_memory_size_words); word_offset++)
        {
            if (card_words[word_offset] != card_test_pattern)
            {
                printf ("NVRAM word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32 "\n",
                        word_offset, card_words[word_offset], card_test_pattern);
                success = false;
            }
            linear_congruential_generator (&card_test_pattern);
        }
    }

    if (success)
    {
        printf ("Test pattern pass\n");
    }

    display_transfer_timing_statistics (&host_to_card_timing);
    display_transfer_timing_statistics (&card_to_host_timing);

    free_vfio_buffer (&h2c_buffer);
    free_vfio_buffer (&c2h_buffer);
}


int main (int argc, char *argv[])
{
    int rc;

    struct
    {
        unsigned map_flags;
        const char *description;
        bool flush_wc_buffer;
    }
    shared_memory_map_options[] =
    {
        {
            .map_flags = PCI_DEV_MAP_FLAG_WRITABLE,
            .description = "uncached-minus",
            .flush_wc_buffer = false
        },
        {
            .map_flags = PCI_DEV_MAP_FLAG_WRITABLE | PCI_DEV_MAP_FLAG_WRITE_COMBINE,
            .description = "write-combining",
            .flush_wc_buffer = false
        },
        {
            .map_flags = PCI_DEV_MAP_FLAG_WRITABLE | PCI_DEV_MAP_FLAG_WRITE_COMBINE,
            .description = "write-combining (flush posted writes)",
            .flush_wc_buffer = true
        }
    };
    const int num_shared_memory_map_options = sizeof (shared_memory_map_options) / sizeof (shared_memory_map_options[0]);

    struct pci_id_match match =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = PCI_MATCH_ANY,
        .subvendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subdevice_id = PCI_MATCH_ANY,
        .device_class = 0,
        .device_class_mask = 0
    };

    rc = pci_system_init ();
    if (rc != 0)
    {
        fprintf (stderr, "pci_system_init failed\n");
        exit (EXIT_FAILURE);
    }

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
                /* Repeat the test with different options for mapping the shared memory */
                for (int option_index = 0; option_index < num_shared_memory_map_options; option_index++)
                {
                    const unsigned map_flags = shared_memory_map_options[option_index].map_flags;

                    for (unsigned bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
                    {
                        const struct pci_mem_region *const region = &device->regions[bar_index];
                        void *addr = NULL;

                        if (region->size > 0)
                        {
                            if (((map_flags & PCI_DEV_MAP_FLAG_WRITE_COMBINE) != 0) && (!region->is_prefetchable))
                            {
                                /* Skip test of write-combine mapping when the region isn't prefetchable */
                                continue;
                            }

                            /* Map the entire BAR */
                            rc = pci_device_map_range (device, region->base_addr, region->size, map_flags, &addr);
                            if (rc != 0)
                            {
                                fprintf (stderr, "pci_device_map_region failed:\n%s", strerror (rc));
                                exit (EXIT_FAILURE);
                            }

                            test_memory_mapped_bar (device, bar_index, addr, shared_memory_map_options[option_index].description,
                                    shared_memory_map_options[option_index].flush_wc_buffer);

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
            }
            break;
        }
        device = pci_device_next (device_iterator);
    }

    return EXIT_SUCCESS;
}
