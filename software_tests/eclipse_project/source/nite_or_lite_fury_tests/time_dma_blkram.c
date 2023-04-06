/*
 * @file time_dma_blkram.c
 * @date 3 Apr 2023
 * @author Chester Gillon
 * @brief Perform tests which time the access to blkram using DMA.
 * @details This program is based upon nite_or_lite_fury_tests/test_ddr.c, with changes to memory size / BARs / device IDs.
 */

#include "vfio_access.h"
#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"
#include "fpga_sio_pci_ids.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <unistd.h>


/* The total amount of BLKRAM addressable by DMA. Sizes set to maximise BLKRAM usage in FPGA */
#define BLKRAM_0_SIZE_BYTES (1024 * 1024)
#define BLKRAM_1_SIZE_BYTES ( 128 * 1024)
#define BLKRAM_TOTAL_SIZE_BYTES (BLKRAM_0_SIZE_BYTES + BLKRAM_1_SIZE_BYTES)


#define DMA_BRIDGE_BAR 0


/* Command line argument which sets the VFIO buffer allocation type */
static vfio_buffer_allocation_type_t arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;


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
    const char *const optstring = "b:c:h:?";
    int option;
    char junk;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
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

        case '?':
        default:
            printf ("Usage %s [-b heap|shared_memory|huge_pages] [-c c2h_channel_id] [-h h2c_channel_id]\n", argv[0]);
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
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_context;
    x2x_transfer_context_t c2h_context;
    transfer_timing_t h2c_timing;
    transfer_timing_t c2h_timing;
    bool success;

    /* Filters for the FGPA devices tested */
    const vfio_pci_device_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FGPA_SIO_SUBDEVICE_ID_DMA_BLKRAM,
            .enable_bus_master = true
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    /* Process matching FPGA devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        vfio_display_pci_command (vfio_device);
        printf ("Testing dma_blkram device with memory size 0x%x for PCI device %s IOMMU group %s h2c_chan %u c2h chan %u\n",
                BLKRAM_TOTAL_SIZE_BYTES, vfio_device->device_name, vfio_device->iommu_group,
                arg_h2c_channel_id, arg_c2h_channel_id);

        /* Create read/write mapping of a single page for DMA descriptors */
        allocate_vfio_dma_mapping (&vfio_devices, &descriptors_mapping, page_size,
                VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

        /* Read mapping used by device to transfer a region of host memory to the entire BLKRAM contents */
        allocate_vfio_dma_mapping (&vfio_devices, &h2c_data_mapping, BLKRAM_TOTAL_SIZE_BYTES, VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

        /* Write mapping used by device to transfer the entire BLKRAM contents to host memory. */
        allocate_vfio_dma_mapping (&vfio_devices, &c2h_data_mapping, BLKRAM_TOTAL_SIZE_BYTES, VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

        if ((descriptors_mapping.buffer.vaddr != NULL) &&
            (h2c_data_mapping.buffer.vaddr    != NULL) &&
            (c2h_data_mapping.buffer.vaddr    != NULL) &&
             initialise_x2x_transfer_context (&h2c_context, vfio_device, DMA_BRIDGE_BAR,
                    DMA_SUBMODULE_H2C_CHANNELS, arg_h2c_channel_id, 0, &descriptors_mapping, &h2c_data_mapping) &&
             initialise_x2x_transfer_context (&c2h_context, vfio_device, DMA_BRIDGE_BAR,
                    DMA_SUBMODULE_C2H_CHANNELS, arg_c2h_channel_id, 0, &descriptors_mapping, &c2h_data_mapping))
        {
            uint32_t *host_words = h2c_data_mapping.buffer.vaddr;
            uint32_t *card_words = c2h_data_mapping.buffer.vaddr;
            const size_t blkram_size_words = BLKRAM_TOTAL_SIZE_BYTES / sizeof (uint32_t);
            uint32_t host_test_pattern = 0;
            uint32_t card_test_pattern = 0;

            success = true;
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
            for (size_t total_words = 0; success && (total_words < 0x100000000UL); total_words += blkram_size_words)
            {
                /* Fill the host buffer with a test pattern to write to the BLKRAM contents */
                card_test_pattern = host_test_pattern;
                for (size_t word_index = 0; word_index < blkram_size_words; word_index++)
                {
                    host_words[word_index] = host_test_pattern;
                    linear_congruential_generator (&host_test_pattern);
                }

                /* DMA the test pattern to the entire BLKRAM contents */
                transfer_time_start (&h2c_timing);
                success = x2x_start_transfer (&h2c_context);
                if (success)
                {
                    while (!x2x_poll_transfer_completion (&h2c_context))
                    {
                    }
                    transfer_time_stop (&h2c_timing);
                }

                /* DMA the entire BLKRAM contents to host memory, and verify the contents */
                transfer_time_start (&c2h_timing);
                success = x2x_start_transfer (&c2h_context);
                if (success)
                {
                    while (!x2x_poll_transfer_completion (&c2h_context))
                    {
                    }
                    transfer_time_stop (&c2h_timing);
                }
                for (size_t word_index = 0; success && (word_index < blkram_size_words); word_index++)
                {
                    if (card_words[word_index] != card_test_pattern)
                    {
                        printf ("BLKRAM word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32 "\n",
                                word_index, card_words[word_index], card_test_pattern);
                        success = false;
                    }
                    linear_congruential_generator (&card_test_pattern);
                }
            }

            if (success)
            {
                printf ("Test pattern pass\n");
            }

            display_transfer_timing_statistics (&h2c_timing);
            display_transfer_timing_statistics (&c2h_timing);
        }

        free_vfio_dma_mapping (&vfio_devices, &c2h_data_mapping);
        free_vfio_dma_mapping (&vfio_devices, &h2c_data_mapping);
        free_vfio_dma_mapping (&vfio_devices, &descriptors_mapping);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
