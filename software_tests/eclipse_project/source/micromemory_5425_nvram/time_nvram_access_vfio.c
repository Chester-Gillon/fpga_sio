/*
 * @file time_nvram_access_vfio.c
 * @date 19 Mar 2023
 * @author Chester Gillon
 * @brief Program to time transfers in a Micro Memory MM-5425CN NVRAM device, using VFIO to access the device
 * @details
 *  In the absence of a description of the device registers and DMA controller, used
 *  https://elixir.bootlin.com/linux/v4.18/source/drivers/block/umem.c as a guide.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>

#include "vfio_access.h"
#include "transfer_timing.h"

#include <linux/types.h>
#include "umem.h"


/* BAR indices on the Micro Memory MM-5425CN NVRAM device */
#define CSR_BAR_INDEX           0
#define MEMORY_WINDOW_BAR_INDEX 2


/**
 * @brief Get the size in bytes of the NVRAM device
 * @param[in] csr Mapped to the NVRAM CSR
 * @return Returns the size in bytes decoded from a CSR register, or zero if unrecognised.
 */
static size_t get_nvram_size_bytes (const uint8_t *const csr)
{
    const uint8_t memory_size_reg = read_reg8 (csr, MEMCTRLSTATUS_MEMORY);
    size_t memory_size_bytes;
    const size_t one_mb = 1024 * 1024;

    switch (memory_size_reg)
    {
    case MEM_128_MB:
        memory_size_bytes = 128 * one_mb;
        break;

    case MEM_256_MB:
        memory_size_bytes = 256 * one_mb;
        break;

    case MEM_512_MB:
        memory_size_bytes = 512 * one_mb;
        break;

    case MEM_1_GB:
        memory_size_bytes = 1024 * one_mb;
        break;

    case MEM_2_GB:
        memory_size_bytes = 2048 * one_mb;
        break;

    default:
        memory_size_bytes = 0;
    }

    return memory_size_bytes;
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
    uint8_t *const csr = vfio_device->mapped_bars[CSR_BAR_INDEX];
    uint8_t *const memory_window = vfio_device->mapped_bars[MEMORY_WINDOW_BAR_INDEX];
    const size_t nvram_size_bytes = get_nvram_size_bytes (csr);
    const size_t memory_window_size_bytes = vfio_device->regions_info[MEMORY_WINDOW_BAR_INDEX].size;
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
    card_test_pattern = host_test_pattern;

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

        map_vfio_device_bar_before_use (vfio_device, CSR_BAR_INDEX);
        map_vfio_device_bar_before_use (vfio_device, MEMORY_WINDOW_BAR_INDEX);
        if (vfio_device->mapped_bars[CSR_BAR_INDEX] != NULL)
        {
            uint8_t *const csr = vfio_device->mapped_bars[CSR_BAR_INDEX];
            const size_t nvram_size_bytes = get_nvram_size_bytes (csr);

            printf ("Testing NVRAM size 0x%zx for PCI device %s IOMMU group %s\n",
                    nvram_size_bytes, vfio_device->device_name, vfio_device->iommu_group);
            if (nvram_size_bytes > 0)
            {
                /* Ensure ECC is enabled */
                if (read_reg8 (csr, MEMCTRLCMD_ERRCTRL) != EDC_STORE_CORRECT)
                {
                    printf ("Enabled ECC for NVRAM\n");
                    write_reg8 (csr, MEMCTRLCMD_ERRCTRL, EDC_STORE_CORRECT);
                }

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
                    (c2h_data_mapping.buffer.vaddr    != NULL))
                {
                    if (vfio_device->mapped_bars[MEMORY_WINDOW_BAR_INDEX] != NULL)
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
