/*
 * @file test_dma_accessible_memory.c
 * @date 29 Jan 2023
 * @author Chester Gillon
 * @brief Perform tests of memory which is accessible via the Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details
 *   Tests involve:
 *   a. Writing and then reading back a pattern to the memory
 *   b. Reporting transfer speeds for the DMA transfer rate
 *   c. Allowing the DMA channels used to be specified (for when the DMA/Bridge has more than one channel configured)
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <unistd.h>


/* Command line argument which sets the VFIO buffer allocation type */
static vfio_buffer_allocation_type_t arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;


/* Command line argument which specifies the minimum alignment size for DMA transfers.
 * Can be used to determine if has any effect on the transfer speed for the h2c_data_mapping used
 * to write to the entire memory which requires multiple chained descriptors due to DMA_DESCRIPTOR_MAX_LEN */
static uint32_t arg_min_size_alignment = 0;


/* Optional command line arguments which can specify the transfer sizes used in each direction to be less than the total memory size.
 * This is to reduce the memory size required for the host buffers. */
static bool arg_h2c_transfer_size_set;
static size_t arg_h2c_transfer_size;
static bool arg_c2h_transfer_size_set;
static size_t arg_c2h_transfer_size;


/* Command line argument which set the DMA channels used. The command line argument passing doesn't verify the
 * channels IDs are supported by the DMA engine, the check is done by initialise_x2x_transfer_context() */
static uint32_t arg_h2c_channel_id;
static uint32_t arg_c2h_channel_id;


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "a:b:c:h:l:m:d:?";
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

        case 'c':
            if (sscanf (optarg, "%u%c", &arg_h2c_channel_id, &junk) != 1)
            {
                fprintf (stderr, "Invalid h2c_channel_id %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'h':
            if (sscanf (optarg, "%u%c", &arg_c2h_channel_id, &junk) != 1)
            {
                fprintf (stderr, "Invalid c2h_channel_id %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'l':
            if (sscanf (optarg, "%zu%c", &arg_c2h_transfer_size, &junk) != 1)
            {
                fprintf (stderr, "Invalid c2h_transfer_size %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_c2h_transfer_size_set = true;
            break;

        case 'm':
            if (sscanf (optarg, "%zu%c", &arg_h2c_transfer_size, &junk) != 1)
            {
                fprintf (stderr, "Invalid arg_h2c_transfer_size %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_h2c_transfer_size_set = true;
            break;

        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case '?':
        default:
            printf ("Usage %s [-a <min_size_alignment>] [-b heap|shared_memory|huge_pages] [-c c2h_channel_id] [-h h2c_channel_id] [-l <c2h_transfer_size>] [-m <h2c_transfer_size>] [-d <pci_device_location>]\n", argv[0]);
            printf ("  -l limits the card-to-host transfer to one page at a time, reducing memory\n");
            printf ("     requirements but increasing transfer overheads.\n");
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


int main (int argc, char *argv[])
{
    const size_t page_size = (size_t) getpagesize ();
    fpga_designs_t designs;
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_context;
    x2x_transfer_context_t c2h_context;
    transfer_timing_t h2c_timing;
    transfer_timing_t c2h_timing;
    bool success;
    x2x_transfer_status_t transfer_status;

    /* Use a single fixed transfer timeout, to stop the test from hanging */
    const int64_t transfer_timeout_secs = 10;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* @todo Force IOVA to start at 4G boundary, as a simple way to avoid allocating reserved regions which are indicated
     *       by VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE.
     *       Attempts to use a reserved region causes VFIO_IOMMU_MAP_DMA to fail with EPERM.
     *       This simple method assumes:
     *       a. The DMA uses 64 bit-addresses
     *       b. Reserved regions are in the first 4GB or at very high addresses.
     *
     *       An improvement to the vfio_access library would be to take note of VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE
     */
    designs.vfio_devices.next_iova = 0x100000000UL;

    /* Process any FPGA designs which have DMA accessible memory */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->dma_bridge_present && (design->dma_bridge_memory_size_bytes > 0))
        {
            printf ("Testing %s design", fpga_design_names[design->design_id]);
            if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
            {
                printf (" version 0x%x", design->board_version);
            }
            printf (" with memory size 0x%zx\n", design->dma_bridge_memory_size_bytes);
            printf ("PCI device %s IOMMU group %s\n", design->vfio_device->device_name, design->vfio_device->iommu_group);

            /* Compute sizes of the individual transfers.
             * Since the xilinx_dma_bridge_transfers API doesn't currently support changing the transfer size,
             * skip the test if the transfer size set from the command line arguments isn't a multiple of the memory size. */
            const size_t num_bytes_per_h2c_xfer =
                    (arg_h2c_transfer_size_set && (arg_h2c_transfer_size < design->dma_bridge_memory_size_bytes)) ?
                            arg_h2c_transfer_size : design->dma_bridge_memory_size_bytes;
            const size_t num_bytes_per_c2h_xfer =
                    (arg_c2h_transfer_size_set && (arg_c2h_transfer_size < design->dma_bridge_memory_size_bytes)) ?
                            arg_c2h_transfer_size : design->dma_bridge_memory_size_bytes;
            if ((design->dma_bridge_memory_size_bytes % num_bytes_per_h2c_xfer) != 0)
            {
                printf ("Skipping test as num_bytes_per_h2c_xfer 0x%zx is not a multiple of the memory size =0x%zx\n",
                        num_bytes_per_h2c_xfer, design->dma_bridge_memory_size_bytes);
                continue;
            }
            else if ((design->dma_bridge_memory_size_bytes % num_bytes_per_c2h_xfer) != 0)
            {
                printf ("Skipping test as num_bytes_per_c2h_xfer 0x%zx is not a multiple of the memory size =0x%zx\n",
                        num_bytes_per_c2h_xfer, design->dma_bridge_memory_size_bytes);
                continue;
            }

            /* Create read/write mapping of a single page for DMA descriptors */
            allocate_vfio_dma_mapping (&designs.vfio_devices, &descriptors_mapping, page_size,
                    VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

            /* Read mapping used by device, with each transfer limited to the maximum of the memory and command line argument */
            allocate_vfio_dma_mapping (&designs.vfio_devices, &h2c_data_mapping, num_bytes_per_h2c_xfer,
                    VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

            /* Write mapping used by device, with each transfer limited to the maximum of the memory and command line argument */
            allocate_vfio_dma_mapping (&designs.vfio_devices, &c2h_data_mapping, num_bytes_per_c2h_xfer,
                    VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

            if ((descriptors_mapping.buffer.vaddr != NULL) &&
                (h2c_data_mapping.buffer.vaddr    != NULL) &&
                (c2h_data_mapping.buffer.vaddr    != NULL) &&
                 initialise_x2x_transfer_context (&h2c_context, design->vfio_device, design->dma_bridge_bar,
                        DMA_SUBMODULE_H2C_CHANNELS, arg_h2c_channel_id, arg_min_size_alignment, &descriptors_mapping, &h2c_data_mapping) &&
                 initialise_x2x_transfer_context (&c2h_context, design->vfio_device, design->dma_bridge_bar,
                        DMA_SUBMODULE_C2H_CHANNELS, arg_c2h_channel_id, arg_min_size_alignment, &descriptors_mapping, &c2h_data_mapping))
            {
                uint32_t *host_words = h2c_data_mapping.buffer.vaddr;
                uint32_t *card_words = c2h_data_mapping.buffer.vaddr;
                const size_t ddr_size_words = design->dma_bridge_memory_size_bytes / sizeof (uint32_t);
                const size_t num_words_per_h2c_xfer = h2c_data_mapping.buffer.size / sizeof (uint32_t);
                const size_t num_words_per_c2h_xfer = c2h_data_mapping.buffer.size / sizeof (uint32_t);
                uint32_t host_test_pattern = 0;
                uint32_t card_test_pattern = 0;

                initialise_transfer_timing (&h2c_timing, "host-to-card DMA", h2c_data_mapping.buffer.size);
                initialise_transfer_timing (&c2h_timing, "card-to-host DMA", c2h_data_mapping.buffer.size);

                printf ("Size of DMA descriptors used for h2c:");
                for (uint32_t descriptor_index = 0; descriptor_index < h2c_context.num_descriptors; descriptor_index++)
                {
                    printf (" [%" PRIu32 "]=0x%" PRIx32, descriptor_index, h2c_context.descriptors[descriptor_index].len);
                }
                printf ("\n");

                printf ("Size of DMA descriptors used for c2h:");
                for (uint32_t descriptor_index = 0; descriptor_index < c2h_context.num_descriptors; descriptor_index++)
                {
                    printf (" [%" PRIu32 "]=0x%" PRIx32, descriptor_index, c2h_context.descriptors[descriptor_index].len);
                }
                printf ("\n");

                /* Perform test iterations to exercise all values of 32-bit test words */
                success = true;
                for (size_t total_words = 0; success && (total_words < 0x100000000UL); total_words += ddr_size_words)
                {
                    /* Write a test pattern, and DMA to the card */
                    card_test_pattern = host_test_pattern;
                    for (size_t ddr_word_index = 0;
                            success && (ddr_word_index < ddr_size_words);
                            ddr_word_index += num_words_per_h2c_xfer)
                    {
                        const size_t ddr_byte_index = ddr_word_index * sizeof (uint32_t);

                        for (size_t word_index = 0; word_index < num_words_per_h2c_xfer; word_index++)
                        {
                            host_words[word_index] = host_test_pattern;
                            linear_congruential_generator (&host_test_pattern);
                        }

                        transfer_time_start (&h2c_timing);
                        x2x_transfer_set_card_start_address (&h2c_context, ddr_byte_index);
                        success = x2x_start_transfer (&h2c_context, transfer_timeout_secs);
                        if (success)
                        {
                            do
                            {
                                transfer_status = x2x_poll_transfer_completion (&h2c_context);
                            } while (transfer_status == X2X_TRANSFER_STATUS_IN_PROGRESS);

                            if (transfer_status == X2X_TRANSFER_STATUS_COMPLETE)
                            {
                                transfer_time_stop (&h2c_timing);
                            }
                            else
                            {
                                success = false;
                                printf ("H2C transfer failed starting at %zu words\n", total_words + ddr_word_index);
                            }
                        }
                    }

                    /* DMA the contents of the memory to the host, and verify the contents */
                    for (size_t ddr_word_index = 0;
                            success && (ddr_word_index < ddr_size_words);
                            ddr_word_index += num_words_per_c2h_xfer)
                    {
                        const size_t ddr_byte_index = ddr_word_index * sizeof (uint32_t);
                        x2x_transfer_set_card_start_address (&c2h_context, ddr_byte_index);
                        transfer_time_start (&c2h_timing);
                        success = x2x_start_transfer (&c2h_context, transfer_timeout_secs);
                        if (success)
                        {
                            do
                            {
                                transfer_status = x2x_poll_transfer_completion (&c2h_context);
                            } while (transfer_status == X2X_TRANSFER_STATUS_IN_PROGRESS);

                            if (transfer_status == X2X_TRANSFER_STATUS_COMPLETE)
                            {
                                transfer_time_stop (&c2h_timing);
                            }
                            else
                            {
                                success = false;
                                printf ("C2H transfer failed starting at %zu words\n", total_words + ddr_word_index);
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
                }

                if (success)
                {
                    printf ("Test pattern pass\n");
                }

                display_transfer_timing_statistics (&h2c_timing);
                display_transfer_timing_statistics (&c2h_timing);
            }

            free_vfio_dma_mapping (&designs.vfio_devices, &c2h_data_mapping);
            free_vfio_dma_mapping (&designs.vfio_devices, &h2c_data_mapping);
            free_vfio_dma_mapping (&designs.vfio_devices, &descriptors_mapping);
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
