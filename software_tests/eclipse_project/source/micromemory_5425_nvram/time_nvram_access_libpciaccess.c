/*
 * @file time_nvram_access_libpciaccess.c
 * @date 26 Mar 2023
 * @author Chester Gillon
 * @brief Program to time transfers in a Micro Memory MM-5425CN NVRAM device, using libpciaccess to access the device
 * @details Compared to time_nvram_access_vfio.c this program:
 *          a. Can map the NVRAM memory window both using uncached-minus or write-combining PAT mappings, to test any performance
 *             differences between the two.
 *             Whereas BARs mapped using vfio always use uncached-minus PAT mappings.
 *          a. Only uses PIO to access to access the NVRAM. While in theory could the cmem_gdb_access module to access
 *             physically contiguous memory to allow the use of DMA this program was only created to allow the different
 *             PAT mappings for PIO to be measured.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <pciaccess.h>

#include "nvram_utils.h"
#include "vfio_access.h" /* Just to allocate buffers from the heap */
#include "transfer_timing.h"


/**
 * @brief Test the NVRAM via the memory mapped window, using the CPU to access the NVRAM by advancing the window through
 *        the entire NVRAM space.
 * @details Writes a test pattern to the entire NVRAM, and then reads back and checks the test pattern to verify the
 *          NVRAM contains the expected data. The error registers on the card are not checked.
 *          The memory mapped window is accessed via memcpy() and doesn't attempt to optimise the transfer in terms
 *          transactions over the PCIe bus.
 * @param[in/out] device Used to obtain the size of the mapped BARs for the NVRAM device
 * @param[in] mapped_bars The mapped BARs for the NVRAM device.
 * @param[in] window_mapping_description Describes how the memory mapped window is mapped
 */
static void test_nvram_via_memory_window (struct pci_device *const device,
                                          void *mapped_bars[const PCI_STD_NUM_BARS],
                                          const char *window_mapping_description)
{
    uint8_t *const csr = mapped_bars[NVRAM_CSR_BAR_INDEX];
    uint8_t *const memory_window = mapped_bars[NVRAM_MEMORY_WINDOW_BAR_INDEX];
    const size_t nvram_size_bytes = get_nvram_size_bytes (csr);
    const size_t memory_window_size_bytes = device->regions[NVRAM_MEMORY_WINDOW_BAR_INDEX].size;
    const size_t nvram_size_words = nvram_size_bytes / sizeof (uint32_t);
    const size_t memory_window_size_words = memory_window_size_bytes / sizeof (uint32_t);
    const size_t num_nvram_windows = nvram_size_bytes / memory_window_size_bytes;
    uint32_t host_test_pattern;
    uint32_t card_test_pattern;
    bool success;
    char timing_description[128];
    transfer_timing_t host_to_card_timing;
    transfer_timing_t card_to_host_timing;
    vfio_buffer_t h2c_buffer;
    vfio_buffer_t c2h_buffer;

    initialise_nvram_device (csr);

    /* Allocate host buffers on the heap */
    create_vfio_buffer (&h2c_buffer, nvram_size_bytes, VFIO_BUFFER_ALLOCATION_HEAP, NULL);
    create_vfio_buffer (&c2h_buffer, nvram_size_bytes, VFIO_BUFFER_ALLOCATION_HEAP, NULL);
    if ((h2c_buffer.vaddr == NULL) || (c2h_buffer.vaddr == NULL))
    {
        return;
    }
    uint32_t *host_words = h2c_buffer.vaddr;
    uint32_t *card_words = c2h_buffer.vaddr;

    printf ("Testing NVRAM size 0x%zx for domain=%04x bus=%02x dev=%02x func=%02x\n  vendor_id=%04x (%s) device_id=%04x (%s) subvendor_id=%04x subdevice_id=%04x\n",
            nvram_size_bytes,
            device->domain, device->bus, device->dev, device->func,
            device->vendor_id, pci_device_get_vendor_name (device),
            device->device_id, pci_device_get_device_name (device),
            device->subvendor_id, device->subdevice_id);
    if (nvram_size_bytes == 0)
    {
        return;
    }

    snprintf (timing_description, sizeof (timing_description), "host-to-card PIO mapped with %s", window_mapping_description);
    initialise_transfer_timing (&host_to_card_timing, timing_description, memory_window_size_bytes);
    snprintf (timing_description, sizeof (timing_description), "card-to-host PIO mapped with %s", window_mapping_description);
    initialise_transfer_timing (&card_to_host_timing, timing_description, memory_window_size_bytes);

    /* As NVRAM access via PIO is relatively slow only time once over the NVRAM, rather than exercising all value of 32-bit words.
     * Start the test pattern by advancing from the value which happens to be at the start of the memory window. */
    memcpy (&host_test_pattern, memory_window, sizeof (host_test_pattern));
    linear_congruential_generator32 (&host_test_pattern);

    /* Fill the host buffer with a test pattern to write to the NVRAM contents */
    card_test_pattern = host_test_pattern;
    for (size_t word_index = 0; word_index < nvram_size_words; word_index++)
    {
        host_words[word_index] = host_test_pattern;
        linear_congruential_generator32 (&host_test_pattern);
    }

    /* Use the CPU to copy the test pattern to the NVRAM one window at a time */
    for (uint8_t window_num = 0; window_num <num_nvram_windows; window_num++)
    {
        transfer_time_start (&host_to_card_timing);
        write_reg8 (csr, WINDOWMAP_WINNUM, window_num);
        memcpy (memory_window, &host_words[window_num * memory_window_size_words], memory_window_size_bytes);
        transfer_time_stop (&host_to_card_timing);
    }

    /* Use the CPU to copy the test pattern from the NVRAM one window at a time */
    for (uint8_t window_num = 0; window_num <num_nvram_windows; window_num++)
    {
        transfer_time_start (&card_to_host_timing);
        write_reg8 (csr, WINDOWMAP_WINNUM, window_num);
        memcpy (&card_words[window_num * memory_window_size_words], memory_window, memory_window_size_bytes);
        transfer_time_stop (&card_to_host_timing);
    }

    /* Verify the test pattern */
    success = true;
    for (size_t word_offset = 0; success && (word_offset < nvram_size_words); word_offset++)
    {
        if (card_words[word_offset] != card_test_pattern)
        {
            printf ("NVRAM word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32 "\n",
                    word_offset, card_words[word_offset], card_test_pattern);
            success = false;
        }
        linear_congruential_generator32 (&card_test_pattern);
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
    void *mapped_bars[PCI_STD_NUM_BARS] = {NULL};

    struct
    {
        unsigned map_flags;
        const char *description;
    }
    memory_window_map_options[] =
    {
        {
            .map_flags = PCI_DEV_MAP_FLAG_WRITABLE,
            .description = "uncached-minus"
        },
        {
            .map_flags = PCI_DEV_MAP_FLAG_WRITABLE | PCI_DEV_MAP_FLAG_WRITE_COMBINE,
            .description = "write-combining"
        }
    };
    const int num_memory_window_map_options = sizeof (memory_window_map_options) / sizeof (memory_window_map_options[0]);

    struct pci_id_match match =
    {
        .vendor_id = NVRAM_VENDOR_ID,
        .device_id = NVRAM_DEVICE_ID,
        .subvendor_id = PCI_MATCH_ANY,
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

    /* Process any Micro Memory devices found */
    struct pci_device_iterator *const device_iterator = pci_id_match_iterator_create (&match);
    struct pci_device *device;

    device = pci_device_next (device_iterator);
    while (device != NULL)
    {
        rc = pci_device_probe (device);
        if (rc == 0)
        {
            if ((device->regions[NVRAM_CSR_BAR_INDEX].size > 0) && (device->regions[NVRAM_MEMORY_WINDOW_BAR_INDEX].size > 0))
            {
                /* Map the CSR BAR to control the device. This is non-prefetchable */
                rc = pci_device_map_range (device,
                        device->regions[NVRAM_CSR_BAR_INDEX].base_addr, device->regions[NVRAM_CSR_BAR_INDEX].size,
                        PCI_DEV_MAP_FLAG_WRITABLE, &mapped_bars[NVRAM_CSR_BAR_INDEX]);
                if (rc != 0)
                {
                    fprintf (stderr, "pci_device_map_region for NVRAM_CSR_BAR_INDEX failed:\n%s", strerror (rc));
                    exit (EXIT_FAILURE);
                }

                /* Repeat the test with different options for mapping the memory window */
                for (int option_index = 0; option_index < num_memory_window_map_options; option_index++)
                {
                    /* Map the entire memory window for testing NVRAM access using the CPU.
                     * The BAR is prefetchable */
                    rc = pci_device_map_range (device,
                            device->regions[NVRAM_MEMORY_WINDOW_BAR_INDEX].base_addr,
                            device->regions[NVRAM_MEMORY_WINDOW_BAR_INDEX].size,
                            memory_window_map_options[option_index].map_flags,
                            &mapped_bars[NVRAM_MEMORY_WINDOW_BAR_INDEX]);
                    if (rc != 0)
                    {
                        fprintf (stderr, "pci_device_map_region for NVRAM_CSR_BAR_INDEX failed:\n%s", strerror (rc));
                        exit (EXIT_FAILURE);
                    }

                    test_nvram_via_memory_window (device, mapped_bars, memory_window_map_options[option_index].description);

                    /* Unmap the memory window BAR */
                    rc = pci_device_unmap_range (device, mapped_bars[NVRAM_MEMORY_WINDOW_BAR_INDEX],
                            device->regions[NVRAM_MEMORY_WINDOW_BAR_INDEX].size);
                    if (rc != 0)
                    {
                        fprintf (stderr, "pci_device_unmap_range failed:\n%s", strerror (rc));
                        exit (EXIT_FAILURE);
                    }
                    mapped_bars[NVRAM_MEMORY_WINDOW_BAR_INDEX] = NULL;
                }

                /* Unmap the CSR bar */
                rc = pci_device_unmap_range (device, mapped_bars[NVRAM_CSR_BAR_INDEX],
                        device->regions[NVRAM_CSR_BAR_INDEX].size);
                if (rc != 0)
                {
                    fprintf (stderr, "pci_device_unmap_range failed:\n%s", strerror (rc));
                    exit (EXIT_FAILURE);
                }
                mapped_bars[NVRAM_CSR_BAR_INDEX] = NULL;
            }
        }

        device = pci_device_next (device_iterator);
    }

    return EXIT_SUCCESS;
}
