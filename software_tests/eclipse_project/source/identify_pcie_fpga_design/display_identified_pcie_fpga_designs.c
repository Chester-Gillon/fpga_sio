/*
 * @file display_identified_pcie_fpga_designs.c
 * @date 22 Oct 2023
 * @author Chester Gillon
 * @brief Display the FPGA designs with a PCIe interface in the PC which are known by the identify_pcie_fpga_design library
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <unistd.h>


/**
 * @brief Parse the command line arguments
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "d:";
    int option;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case '?':
        default:
            printf ("Usage %s -d <pci_device_location>\n", argv[0]);
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


/**
 * @brief Display information about a peripheral which is present in an identified design
 * @param[in] design The identified design to check for the peripheral
 * @param[in] peripheral_name The name of the peripheral
 * @param[in] peripheral_mapped_base If non-NULL the mapped base of the peripheral which is present in the design.
 */
static void display_design_present_peripheral (const fpga_design_t *const design,
                                               const char *const peripheral_name, const uint8_t *const peripheral_mapped_base)
{
    if (peripheral_mapped_base != NULL)
    {
        /* The peripheral is present since its registers are mapped.
         * Search to find the offset into which BAR the registers are mapped to. */
        bool found_bar;
        uint32_t bar_number;
        ptrdiff_t bar_offset;

        found_bar = false;
        bar_number = 0;
        bar_offset = 0;
        while (!found_bar && (bar_number < PCI_STD_NUM_BARS))
        {
            if (design->vfio_device->mapped_bars[bar_number] != NULL)
            {
                const uint8_t *const mapped_bar_start = design->vfio_device->mapped_bars[bar_number];
                const uint8_t *const mapped_bar_end = &mapped_bar_start[design->vfio_device->regions_info[bar_number].size];

                if ((peripheral_mapped_base >= mapped_bar_start) && (peripheral_mapped_base < mapped_bar_end))
                {
                    bar_offset = peripheral_mapped_base - mapped_bar_start;
                    found_bar = true;
                }
            }

            if (!found_bar)
            {
                bar_number++;
            }
        }

        if (found_bar)
        {
            printf ("  %s registers at bar %u offset 0x%lx\n", peripheral_name, bar_number, bar_offset);
        }
        else
        {
            printf ("  %s register at mapped address %p (unable to identify bar)\n", peripheral_name, peripheral_mapped_base);
        }
    }
}


/**
 * @brief Display information about a Xilinx "DMA/Bridge Subsystem for PCI Express" in an identified design
 * @details
 *   Displays the:
 *   1. If the DMA bridge is configured as memory mapped or AXI streams.
 *   2. If memory mapped, the amount of card memory (defined in the identify_pcie_fpga_design library as
 *      not specified in any DMA brige register).
 *   3. Alignment requirements of the DMA engine for each channel.
 *      This is reported since:
 *      a. PG195 doesn't seem to define which configuration parameters change the alignment requirements.
 *      b. Current tests have left the alignment requirements at one byte, and xilinx_dma_bridge_transfers
 *         doesn't check the alignment of addresses used for transfers.
 * @param[in] design The identified design containing the DMA bridge
 */
static void display_dma_bridge (const fpga_design_t *const design)
{
    typedef enum
    {
        CHANNEL_DIR_H2C,
        CHANNEL_DIR_C2H,
        CHANNEL_DIR_ARRAY_SIZE
    } channel_dir_t;
    const char *const channel_dir_names[CHANNEL_DIR_ARRAY_SIZE] =
    {
        [CHANNEL_DIR_H2C] = "H2C",
        [CHANNEL_DIR_C2H] = "C2H"
    };
    channel_dir_t channel_dir;
    uint32_t channel_id;
    uint32_t num_channels[CHANNEL_DIR_ARRAY_SIZE];
    x2x_transfer_context_t transfers[CHANNEL_DIR_ARRAY_SIZE][X2X_MAX_CHANNELS] = {0};

    x2x_get_num_channels (design->vfio_device, design->dma_bridge_bar, design->dma_bridge_memory_size_bytes,
            &num_channels[CHANNEL_DIR_H2C], &num_channels[CHANNEL_DIR_C2H],
            transfers[CHANNEL_DIR_H2C], transfers[CHANNEL_DIR_C2H]);
    if (design->dma_bridge_memory_size_bytes > 0)
    {
        printf ("  DMA bridge bar %u memory size 0x%zx\n", design->dma_bridge_bar, design->dma_bridge_memory_size_bytes);
    }
    else
    {
        printf ("  DMA bridge bar %u AXI Stream\n", design->dma_bridge_bar);
    }

    printf ("  Channel ID  addr_alignment  len_granularity  num_address_bits\n");
    for (channel_dir = 0; channel_dir < CHANNEL_DIR_ARRAY_SIZE; channel_dir++)
    {
        for (channel_id = 0; channel_id < num_channels[channel_dir]; channel_id++)
        {
            const x2x_transfer_context_t *const transfer = &transfers[channel_dir][channel_id];

            printf ("       %s %u  %14u  %15u  %16u\n",
                    channel_dir_names[channel_dir], channel_id,
                    transfer->addr_alignment, transfer->len_granularity, transfer->num_address_bits);
        }
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Display the identified designs */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        const fpga_design_t *const design = &designs.designs[design_index];

        printf ("\nDesign %s", fpga_design_names[design->design_id]);
        if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
        {
            printf (" version 0x%x", design->board_version);
        }
        printf (":\n");
        printf ("  PCI device %s IOMMU group %s\n", design->vfio_device->device_name, design->vfio_device->group->iommu_group_name);
        if (design->dma_bridge_present)
        {
            display_dma_bridge (design);
        }
        if (design->user_access != NULL)
        {
            const uint32_t user_access = read_reg32 (design->user_access, 0);
            char formatted_timestamp[USER_ACCESS_TIMESTAMP_LEN];

            format_user_access_timestamp (user_access, formatted_timestamp);
            printf ("  User access build timestamp : %08X - %s\n", user_access, formatted_timestamp);
        }

        display_design_present_peripheral (design, "Quad SPI", design->quad_spi_regs);
        display_design_present_peripheral (design, "XADC", design->xadc_regs);
        display_design_present_peripheral (design, "IIC", design->iic_regs);
        display_design_present_peripheral (design, "bit-banged I2C GPIO", design->bit_banged_i2c_gpio_regs);
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
