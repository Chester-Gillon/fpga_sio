/*
 * @file display_identified_pcie_fpga_designs.c
 * @date 22 Oct 2023
 * @author Chester Gillon
 * @brief Display the FPGA designs with a PCIe interface in the PC which are known by the identify_pcie_fpga_design library
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"
#include "xilinx_axi_stream_switch.h"
#include "xilinx_cms.h"
#include "cmac_axi4_lite_registers.h"
#include "generic_pci_access.h"

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


/**
 * @brief Display information about Xilinx AXI4-Stream Switch in an identified design.
 * @details
 *   Displays the enabled routes from master to slave ports.
 *   Since all the ports are disabled at reset, displays a specific message when all ports are disabled.
 * @param[in] design The identified design containing the AXI4-Stream Switch
 */
static void display_axi_switch (const fpga_design_t *const design)
{
    uint32_t master_port;
    bool enabled_ports[XILINX_AXI_STREAM_SWITCH_MAX_PORTS];
    uint32_t slave_ports[XILINX_AXI_STREAM_SWITCH_MAX_PORTS];
    uint32_t num_enabled_ports = 0;

    for (master_port = 0; master_port < design->axi_switch_num_master_ports; master_port++)
    {
        enabled_ports[master_port] =
                xilinx_axi_switch_get_selected_slave (design->axi_switch_regs, master_port, &slave_ports[master_port]);
        if (enabled_ports[master_port])
        {
            num_enabled_ports++;
        }
    }

    if (num_enabled_ports > 0)
    {
        printf ("  Enabled AXI4-Stream Switch route(s):\n");
        for (master_port = 0; master_port < design->axi_switch_num_master_ports; master_port++)
        {
            if (enabled_ports[master_port])
            {
                printf ("    Master %2u -> Slave %2u\n", master_port, slave_ports[master_port]);
            }
        }
    }
    else
    {
        printf ("  All %u master ports in AXI4-Stream Switch are disabled\n", design->axi_switch_num_master_ports);
    }
}


/**
 * @brief Display information about Xilinx Card Management Solution Subsystem in an identified design
 * @param[in] design The identified design containing the CMS Subsystem
 */
static void display_cms (const fpga_design_t *const design)
{
    xilinx_cms_context_t context;

    if (cms_initialise_access (&context, design->vfio_device, design->cms_subsystem_bar_index, design->cms_subsystem_base_offset))
    {
        cms_display_configuration (&context);

        for (uint32_t cage_select = 0; cage_select < cms_num_qsfp_modules[context.software_profile]; cage_select++)
        {
            cms_qsfp_low_speed_io_read_data_t low_speed_io;

            if (cms_read_qsfp_module_low_speed_io (&context, cage_select, &low_speed_io))
            {
                printf ("  QSFP %u : %s\n", cage_select, low_speed_io.qsfp_int_l ? "Interrupt Clear" : "Interrupt Set");
                printf ("  QSFP %u : %s\n", cage_select, low_speed_io.qsfp_modprs_l ? "Module not Present" : "Module Present");
                printf ("  QSFP %u : %s\n", cage_select, low_speed_io.qsfp_modsel_l ? "Module not Selected" : "Module Selected");
                printf ("  QSFP %u : %s\n", cage_select, low_speed_io.qsfp_lpmode ? "Low Power Mode" : "High Power Mode");
                printf ("  QSFP %u : %s\n", cage_select, low_speed_io.qsfp_reset_l ? "Reset Clear" : "Reset Active");
            }
        }
    }
}


/**
 * @brief Display information about the CMAC ports in an identified design
 * @param[in] design The identified design containing the CMS Subsystem
 */
static void display_cmac_ports (const fpga_design_t *const design)
{
    char peripheral_name[80];
    const char *const core_mode_names[] =
    {
        "CAUI10",
        "CAUI4",
        "Runtime Switchable CAUI10",
        "Runtime Switchable CAUI4"
    };

    for (uint32_t port_index = 0; port_index < design->num_cmac_ports; port_index++)
    {
        const uint8_t *const cmac_regs = design->cmac_ports[port_index].cmac_regs;

        if (cmac_regs != NULL)
        {
            const uint32_t core_mode_reg = read_reg32 (cmac_regs, CORE_MODE_REG_OFFSET);
            const uint32_t core_mode = generic_pci_access_extract_field (core_mode_reg, CORE_MODE_REG_MASK);
            const uint32_t core_version_reg = read_reg32 (cmac_regs, CORE_VERSION_REG_OFFSET);
            const uint32_t core_version_minor = generic_pci_access_extract_field (core_version_reg, CORE_VERSION_REG_MINOR_MASK);
            const uint32_t core_version_major = generic_pci_access_extract_field (core_version_reg, CORE_VERSION_REG_MAJOR_MASK);

            snprintf (peripheral_name, sizeof (peripheral_name), "CMAC port %u", port_index);
            display_design_present_peripheral (design, peripheral_name, cmac_regs);
            printf ("    Core mode: %s\n", core_mode_names[core_mode]);
            printf ("    Core version: %u.%u\n", core_version_major, core_version_minor);
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
        printf ("  PCI device %s rev %02x IOMMU group %s", design->vfio_device->device_name, design->vfio_device->pci_revision_id,
                design->vfio_device->group->iommu_group_name);
        if (design->vfio_device->pci_physical_slot != NULL)
        {
            printf ("  physical slot %s\n", design->vfio_device->pci_physical_slot);
        }
        printf ("\n");
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
        display_design_present_peripheral (design, "SYSMON", design->sysmon_regs);
        display_design_present_peripheral (design, "IIC", design->iic_regs);
        display_design_present_peripheral (design, "bit-banged I2C GPIO", design->bit_banged_i2c_gpio_regs);
        if (design->axi_switch_regs != NULL)
        {
            display_axi_switch (design);
        }
        if (design->num_cmac_ports > 0)
        {
            display_cmac_ports (design);
        }
        if (design->cms_subsystem_present)
        {
            display_cms (design);
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
