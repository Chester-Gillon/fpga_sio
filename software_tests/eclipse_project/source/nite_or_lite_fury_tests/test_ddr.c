/*
 * @file test_ddr.c
 * @date 29 Jan 2023
 * @author Chester Gillon
 * @brief Perform tests of NiteFury or LiteFury DMA using VFIO
 */

#include "vfio_access.h"
#include "fury_utils.h"
#include "xilinx_dma_bridge_transfers.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <unistd.h>


/**
 * @brief A 32-bit Linear congruential generator for creating a pseudo-random test pattern.
 * @details "Numerical Recipes" from https://en.wikipedia.org/wiki/Linear_congruential_generator
 * @param[in/out] seed the LCG value to advance
 */
static inline void linear_congruential_generator (uint32_t *const seed)
{
    *seed = (*seed * 1664525) + 1013904223;
}


/* Command line argument which sets the VFIO buffer allocation type */
static vfio_buffer_allocation_type_t arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;


/* Command line argument which specifies the minimum alignment size for DMA transfers.
 * Can be used to determine if has any effect on the transfer speed for the h2c_data_mapping used
 * to write to the entire DDR memory which requires multiple chained descriptors due to DMA_DESCRIPTOR_MAX_LEN */
static uint32_t arg_min_size_alignment = 0;


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "a:b:?";
    int option;
    char junk;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'a':
            if (sscanf (optarg, "%" SCNu32 "%c", &arg_min_size_alignment, &junk) != 1)
            {
                printf ("Invalid min_size_alignment %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'b':
            if (strcmp (optarg, "heap") == 0)
            {
                arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;
            }
            else if (strcmp (optarg, "shared_memory") == 0)
            {
                arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_SHARED_MEMORY;
            }
            else if (strcmp (optarg, "huge_pages") == 0)
            {
                arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HUGE_PAGES;
            }
            else
            {
                printf ("Invalid buffer allocation type %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case '?':
        default:
            printf ("Usage %s [-a <min_size_alignment] [-b heap|shared_memory|huge_pages]\n", argv[0]);
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


int main (int argc, char *argv[])
{
    const size_t page_size = (size_t) getpagesize ();
    vfio_devices_t vfio_devices;
    uint32_t board_version;
    fury_type_t fury_type;
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_context;
    x2x_transfer_context_t c2h_context;
    bool success;

    /* The DMA/Bridge Subsystem is in configured to have one H2C and one C2H channel */
    const uint32_t h2c_channel_id = 0;
    const uint32_t c2h_channel_id = 0;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    /* Process any NiteFury or LiteFury devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        fury_type = identify_fury (vfio_device, &board_version);
        if (fury_type != DEVICE_OTHER)
        {
            const size_t ddr_size_bytes = fury_ddr_sizes_bytes[fury_type];

            vfio_display_pci_command (vfio_device);
            printf ("Testing %s board version 0x%x with DDR size 0x%zx for PCI device %s IOMMU group %s\n",
                    fury_names[fury_type], board_version, ddr_size_bytes, vfio_device->device_name, vfio_device->iommu_group);

            /* Create read/write mapping of a single page for DMA descriptors */
            allocate_vfio_dma_mapping (&vfio_devices, &descriptors_mapping, page_size,
                    VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

            /* Read mapping used by device to transfer a region of host memory to the entire DDR contents */
            allocate_vfio_dma_mapping (&vfio_devices, &h2c_data_mapping, ddr_size_bytes, VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

            /* Write mapping for a single page used by device to write one page of DDR to host memory */
            allocate_vfio_dma_mapping (&vfio_devices, &c2h_data_mapping, page_size, VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

            if ((descriptors_mapping.buffer.vaddr != NULL) &&
                (h2c_data_mapping.buffer.vaddr    != NULL) &&
                (c2h_data_mapping.buffer.vaddr    != NULL) &&
                 initialise_x2x_transfer_context (&h2c_context, vfio_device, FURY_DMA_BRIDGE_BAR,
                        DMA_SUBMODULE_H2C_CHANNELS, h2c_channel_id, arg_min_size_alignment, &descriptors_mapping, &h2c_data_mapping) &&
                 initialise_x2x_transfer_context (&c2h_context, vfio_device, FURY_DMA_BRIDGE_BAR,
                        DMA_SUBMODULE_C2H_CHANNELS, c2h_channel_id, arg_min_size_alignment, &descriptors_mapping, &c2h_data_mapping))
            {
                uint32_t *host_words = h2c_data_mapping.buffer.vaddr;
                uint32_t *card_words = c2h_data_mapping.buffer.vaddr;
                const size_t ddr_size_words = ddr_size_bytes / sizeof (uint32_t);
                const size_t num_words_per_c2h_xfer = c2h_data_mapping.buffer.size / sizeof (uint32_t);
                uint32_t host_test_pattern = 0;
                uint32_t card_test_pattern = 0;
                struct timespec start_time;
                struct timespec end_time;

                printf ("Size of DMA descriptors used for h2c:");
                for (uint32_t descriptor_index = 0; descriptor_index < h2c_context.num_descriptors; descriptor_index++)
                {
                    printf (" [%" PRIu32 "]=0x%" PRIx32, descriptor_index, h2c_context.descriptors[descriptor_index].len);
                }
                printf ("\n");

                /* Perform test iterations to exercise all values of 32-bit test words */
                for (size_t total_words = 0; total_words < 0x100000000UL; total_words += ddr_size_words)
                {
                    /* Fill the host buffer with a test pattern to write to the DDR contents */
                    card_test_pattern = host_test_pattern;
                    for (size_t word_index = 0; word_index < ddr_size_words; word_index++)
                    {
                        host_words[word_index] = host_test_pattern;
                        linear_congruential_generator (&host_test_pattern);
                    }

                    /* DMA the test pattern to the entire DDR contents */
                    x2x_transfer_set_card_start_address (&h2c_context, 0);
                    clock_gettime (CLOCK_MONOTONIC, &start_time);
                    success = x2x_start_transfer (&h2c_context);
                    if (success)
                    {
                        while (!x2x_poll_transfer_completion (&h2c_context))
                        {
                        }
                        clock_gettime (CLOCK_MONOTONIC, &end_time);
                        const int64_t start_time_ns = (start_time.tv_sec * 1000000000LL) + start_time.tv_nsec;
                        const int64_t end_time_ns = (end_time.tv_sec * 1000000000LL) + end_time.tv_nsec;
                        const int64_t write_duration_ns = end_time_ns - start_time_ns;
                        printf ("Print wrote 0x%" PRIx64 " bytes to card using DMA in %" PRIi64 " ns\n",
                                h2c_data_mapping.buffer.size, write_duration_ns);
                    }

                    /* DMA the contents of the DDR one page at a time from the card to the host, and verify the contents */
                    for (size_t ddr_word_index = 0;
                            success && (ddr_word_index < ddr_size_words);
                            ddr_word_index += num_words_per_c2h_xfer)
                    {
                        const uint32_t ddr_byte_index = ddr_word_index * sizeof (uint32_t);
                        x2x_transfer_set_card_start_address (&c2h_context, ddr_byte_index);
                        success = x2x_start_transfer (&c2h_context);
                        if (success)
                        {
                            while (!x2x_poll_transfer_completion (&c2h_context))
                            {
                            }

                            for (size_t word_offset = 0; success && (word_offset < num_words_per_c2h_xfer); word_offset++)
                            {
                                if (card_words[word_offset] != card_test_pattern)
                                {
                                    printf ("DDR word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32 "\n",
                                            ddr_word_index + word_offset, card_words[word_offset], card_test_pattern);
                                    success = false;
                                }
                                linear_congruential_generator (&card_test_pattern);
                            }
                        }
                    }
                    if (success)
                    {
                        printf ("Test pattern pass\n");
                    }
                }
            }

            free_vfio_dma_mapping (&vfio_devices, &c2h_data_mapping);
            free_vfio_dma_mapping (&vfio_devices, &h2c_data_mapping);
            free_vfio_dma_mapping (&vfio_devices, &descriptors_mapping);
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
