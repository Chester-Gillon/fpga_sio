/*
 * @file time_nvram_access_vfio.c
 * @date 19 Mar 2023
 * @author Chester Gillon
 * @brief Program to time transfers in a Micro Memory MM-5425CN NVRAM device, using VFIO to access the device
 * @details
 *   Performs timing of the NVRAM access using both DMA and PIO.
 *   Where PIO is performed by the CPU accessing the NVRAM via the memory mapped window.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>

#include "vfio_access.h"
#include "transfer_timing.h"
#include "nvram_utils.h"


/**
 * @brief Test the NVRAM using DMA
 * @details Writes a test pattern to the entire NVRAM, and then reads back and checks the test pattern to verify the
 *          NVRAM contains the expected data. The error registers on the card are not checked.
 * @param[in/out] vfio_device Used to obtain the mapped BARs for the NVRAM device
 * @param[in] h2c_data_mapping The buffer allocated on the host for host-to-card transfers
 * @param[in] c2h_data_mapping The buffer allocated on the host for for card-to-host transfers
 * @param[in] h2c_context The DMA context for host-to-card transfers
 * @param[in] c2h_context The DMA context for card-to-host transfers
 */
static void test_nvram_via_dma (vfio_device_t *const vfio_device,
                                const vfio_dma_mapping_t *const h2c_data_mapping,
                                const vfio_dma_mapping_t *const c2h_data_mapping,
                                nvram_transfer_context_t *const h2c_context,
                                nvram_transfer_context_t *const c2h_context)
{
    uint8_t *const csr = vfio_device->mapped_bars[NVRAM_CSR_BAR_INDEX];
    const size_t nvram_size_bytes = get_nvram_size_bytes (csr);
    const size_t nvram_size_words = nvram_size_bytes / sizeof (uint32_t);
    uint32_t host_test_pattern = 0;
    uint32_t card_test_pattern = 0;
    uint32_t *host_words = h2c_data_mapping->buffer.vaddr;
    uint32_t *card_words = c2h_data_mapping->buffer.vaddr;
    transfer_timing_t host_to_card_timing;
    transfer_timing_t card_to_host_timing;
    bool success;

    initialise_transfer_timing (&host_to_card_timing, "host-to-card DMA", h2c_data_mapping->buffer.size);
    initialise_transfer_timing (&card_to_host_timing, "card-to-host DMA", h2c_data_mapping->buffer.size);

    /* Perform test iterations to exercise all values of 32-bit test words */
    for (size_t total_words = 0; total_words < 0x100000000UL; total_words += nvram_size_words)
    {
        /* Fill the host buffer with a test pattern to write to the NVRAM contents */
        card_test_pattern = host_test_pattern;
        for (size_t word_index = 0; word_index < nvram_size_words; word_index++)
        {
            host_words[word_index] = host_test_pattern;
            linear_congruential_generator (&host_test_pattern);
        }

        /* Use DMA to write the test pattern to the entire NVRAM */
        transfer_time_start (&host_to_card_timing);
        start_nvram_dma_transfer (csr, h2c_context);
        while (!poll_nvram_dma_transfer_completion (h2c_context))
        {
        }
        transfer_time_stop (&host_to_card_timing);

        /* Use DMA to read the test pattern from the entire NVRAM */
        transfer_time_start (&card_to_host_timing);
        start_nvram_dma_transfer (csr, c2h_context);
        while (!poll_nvram_dma_transfer_completion (c2h_context))
        {
        }
        transfer_time_stop (&card_to_host_timing);

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
            linear_congruential_generator (&card_test_pattern);
        }

        if (success)
        {
            printf ("Test pattern pass\n");
        }
    }

    display_transfer_timing_statistics (&host_to_card_timing);
    display_transfer_timing_statistics (&card_to_host_timing);
}


/**
 * @brief Test the NVRAM via the memory mapped window, using the CPU to access the NVRAM by advancing the window through
 *        the entire NVRAM space.
 * @details Writes a test pattern to the entire NVRAM, and then reads back and checks the test pattern to verify the
 *          NVRAM contains the expected data. The error registers on the card are not checked.
 *          The memory mapped window is accessed via memcpy() and doesn't attempt to optimise the transfer in terms
 *          transactions over the PCIe bus.
 * @param[in/out] vfio_device Used to obtain the mapped BARs for the NVRAM device
 * @param[in] h2c_data_mapping Used to obtain the buffer allocated on the host for host-to-card transfers
 * @param[in] c2h_data_mapping Used to obtain the buffer allocated on the host for card-to-host transfers
 */
static void test_nvram_via_memory_window (vfio_device_t *const vfio_device,
                                          const vfio_dma_mapping_t *const h2c_data_mapping,
                                          const vfio_dma_mapping_t *const c2h_data_mapping)
{
    uint8_t *const csr = vfio_device->mapped_bars[NVRAM_CSR_BAR_INDEX];
    uint8_t *const memory_window = vfio_device->mapped_bars[NVRAM_MEMORY_WINDOW_BAR_INDEX];
    const size_t nvram_size_bytes = get_nvram_size_bytes (csr);
    const size_t memory_window_size_bytes = vfio_device->regions_info[NVRAM_MEMORY_WINDOW_BAR_INDEX].size;
    const size_t nvram_size_words = nvram_size_bytes / sizeof (uint32_t);
    const size_t memory_window_size_words = memory_window_size_bytes / sizeof (uint32_t);
    const size_t num_nvram_windows = nvram_size_bytes / memory_window_size_bytes;
    uint32_t *host_words = h2c_data_mapping->buffer.vaddr;
    uint32_t *card_words = c2h_data_mapping->buffer.vaddr;
    uint32_t host_test_pattern;
    uint32_t card_test_pattern;
    bool success;
    transfer_timing_t host_to_card_timing;
    transfer_timing_t card_to_host_timing;

    initialise_transfer_timing (&host_to_card_timing, "host-to-card PIO", memory_window_size_bytes);
    initialise_transfer_timing (&card_to_host_timing, "card-to-host PIO", memory_window_size_bytes);

    /* As NVRAM access via PIO is relatively slow only time once over the NVRAM, rather than exercising all value of 32-bit words.
     * Start the test pattern by advancing from the value which happens to be at the start of the memory window. */
    memcpy (&host_test_pattern, memory_window, sizeof (host_test_pattern));
    linear_congruential_generator (&host_test_pattern);

    /* Fill the host buffer with a test pattern to write to the NVRAM contents */
    card_test_pattern = host_test_pattern;
    for (size_t word_index = 0; word_index < nvram_size_words; word_index++)
    {
        host_words[word_index] = host_test_pattern;
        linear_congruential_generator (&host_test_pattern);
    }

    /* Use the CPU to copy the test pattern to the NVRAM one window at a time */
    for (uint8_t window_num = 0; window_num <num_nvram_windows; window_num++)
    {
        transfer_time_start (&host_to_card_timing);
        write_reg8 (csr, WINDOWMAP_WINNUM, window_num);
        memcpy (memory_window, &host_words[window_num * memory_window_size_words], memory_window_size_bytes);
        transfer_time_stop (&host_to_card_timing);
    }

    /* Used the CPU to copy the test pattern from the NVRAM one window at a time */
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
        linear_congruential_generator (&card_test_pattern);
    }

    if (success)
    {
        printf ("Test pattern pass\n");
    }

    display_transfer_timing_statistics (&host_to_card_timing);
    display_transfer_timing_statistics (&card_to_host_timing);
}


int main (int argc, char *argv[])
{
    const size_t page_size = (size_t) getpagesize ();
    vfio_devices_t vfio_devices;
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    nvram_transfer_context_t h2c_context;
    nvram_transfer_context_t c2h_context;

    const vfio_pci_device_filter_t filter =
    {
        .vendor_id = 0x1332,
        .device_id = 0x5425,
        .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .enable_bus_master = true
    };

    /* Open the Micro Memory devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* Process any Micro Memory devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        map_vfio_device_bar_before_use (vfio_device, NVRAM_CSR_BAR_INDEX);
        map_vfio_device_bar_before_use (vfio_device, NVRAM_MEMORY_WINDOW_BAR_INDEX);
        if (vfio_device->mapped_bars[NVRAM_CSR_BAR_INDEX] != NULL)
        {
            uint8_t *const csr = vfio_device->mapped_bars[NVRAM_CSR_BAR_INDEX];
            const size_t nvram_size_bytes = get_nvram_size_bytes (csr);

            printf ("Testing NVRAM size 0x%zx for PCI device %s IOMMU group %s\n",
                    nvram_size_bytes, vfio_device->device_name, vfio_device->iommu_group);
            if (nvram_size_bytes > 0)
            {
                initialise_nvram_device (csr);

                /* Create read/write mapping of a single page for DMA descriptors */
                allocate_vfio_dma_mapping (&vfio_devices, &descriptors_mapping, page_size,
                        VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

                /* Read mapping used by device to transfer a region of host memory to the entire NVRAM contents */
                allocate_vfio_dma_mapping (&vfio_devices, &h2c_data_mapping, nvram_size_bytes,
                        VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

                /* Write mapping used by device to transfer the entire NVRAM contents to a region of host memory */
                allocate_vfio_dma_mapping (&vfio_devices, &c2h_data_mapping, nvram_size_bytes,
                        VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

                if ((descriptors_mapping.buffer.vaddr != NULL) &&
                    (h2c_data_mapping.buffer.vaddr    != NULL) &&
                    (c2h_data_mapping.buffer.vaddr    != NULL) &&
                    initialise_nvram_transfer_context (&h2c_context, &descriptors_mapping, &h2c_data_mapping, DMA_READ_FROM_HOST) &&
                    initialise_nvram_transfer_context (&c2h_context, &descriptors_mapping, &c2h_data_mapping, DMA_WRITE_TO_HOST))
                {
                    test_nvram_via_dma (vfio_device, &h2c_data_mapping, &c2h_data_mapping, &h2c_context, &c2h_context);

                    if (vfio_device->mapped_bars[NVRAM_MEMORY_WINDOW_BAR_INDEX] != NULL)
                    {
                        test_nvram_via_memory_window (vfio_device, &h2c_data_mapping, &c2h_data_mapping);
                    }
                }

                free_vfio_dma_mapping (&vfio_devices, &c2h_data_mapping);
                free_vfio_dma_mapping (&vfio_devices, &h2c_data_mapping);
                free_vfio_dma_mapping (&vfio_devices, &descriptors_mapping);
            }
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
