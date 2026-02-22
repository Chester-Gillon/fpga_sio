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
#include "vfio_bitops.h"
#include "qdma_transfers.h"
#include "mrmac_axi4_lite_registers.h"

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
        printf ("  DMA bridge bar %u memory base offset 0x%zx size 0x%zx\n",
                design->dma_bridge_bar, design->dma_bridge_memory_base_address, design->dma_bridge_memory_size_bytes);
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
 * @brief Display information about a "QDMA Subsystem" in an identified design
 * @param[in] design The identified design containing the QDMA Subsystem
 */
static void display_qdma (const fpga_design_t *const design)
{
    qdma_device_context_t qdma_device;

    const bool device_supported = qdma_identify_device (&qdma_device, design->vfio_device, design->qdma_bridge_bar,
            design->qdma_memory_base_address, design->qdma_memory_size_bytes);

    /* Display the version information if found, even if the IP isn't supported */
    if (qdma_device.version_info.ip_type != QDMA_NONE_IP)
    {
        if (design->qdma_memory_size_bytes > 0)
        {
            printf ("  QDMA bar %u memory base offset 0x%zx size 0x%zx\n",
                    design->qdma_bridge_bar, design->qdma_memory_base_address, design->qdma_memory_size_bytes);
        }
        printf ("  rtl_version                      : %s\n", qdma_device.version_info.qdma_rtl_version_str);
        printf ("  vivado_release                   : %s\n", qdma_device.version_info.qdma_vivado_release_id_str);
        printf ("  ip_type                          : %s\n", qdma_device.version_info.qdma_ip_type_str);
        printf ("  device_type                      : %s\n", qdma_device.version_info.qdma_device_type_str);

        if (device_supported)
        {
            /* Display the information for a supported device */
            printf ("  Number of PFs supported          : %u\n", qdma_device.dev_cap.num_pfs);
            printf ("  Total number of queues supported : %u\n", qdma_device.dev_cap.num_qs);
            printf ("  MM channels                      : %u\n", qdma_device.dev_cap.mm_channel_max);
            printf ("  FLR Present                      : %s\n", qdma_device.dev_cap.flr_present ? "yes":"no");
            printf ("  ST enabled                       : %s\n", qdma_device.dev_cap.st_en ? "yes":"no");
            printf ("  MM enabled                       : %s\n", qdma_device.dev_cap.mm_en ? "yes":"no");
            printf ("  Mailbox enabled                  : %s\n", qdma_device.dev_cap.mailbox_en ? "yes":"no");
            printf ("  MM completion enabled            : %s\n", qdma_device.dev_cap.mm_cmpt_en ? "yes":"no");
            printf ("  Debug Mode enabled               : %s\n", qdma_device.dev_cap.debug_mode ? "yes":"no");
            printf ("  Desc Engine Mode                 : %s\n", qdma_desc_eng_mode_names[qdma_device.dev_cap.desc_eng_mode]);
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
 * @brief Display the 96-bit DNA value which uniquely identifies a UltraScale or UltraScale+ plus device
 * @param[in] design The identified design containing the UltraScale DNA.
 */
static void display_ultrascale_dna (const fpga_design_t *const design)
{
    uint32_t dna_words[3];

    for (uint32_t dna_word = 0; dna_word < (sizeof (dna_words) / sizeof (dna_words[0])); dna_word++)
    {
        dna_words[dna_word] = read_reg32 (design->ultrascale_dna_regs, dna_word * sizeof (uint32_t));
    }

    /* Order of the words matches that read by JTAG as displayed in the Vivado hardware manager */
    printf ("  UltraScale DNA: %08X%08X%08X\n", dna_words[0], dna_words[1], dna_words[2]);
}


/**
 * @brief Display information about the CMAC ports in an identified design
 * @param[in] design The identified design containing the CMAC ports
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
            const uint32_t core_mode = vfio_extract_field_u32 (core_mode_reg, CORE_MODE_REG_MASK);
            const uint32_t core_version_reg = read_reg32 (cmac_regs, CORE_VERSION_REG_OFFSET);
            const uint32_t core_version_minor = vfio_extract_field_u32 (core_version_reg, CORE_VERSION_REG_MINOR_MASK);
            const uint32_t core_version_major = vfio_extract_field_u32 (core_version_reg, CORE_VERSION_REG_MAJOR_MASK);

            snprintf (peripheral_name, sizeof (peripheral_name), "CMAC port %u", port_index);
            display_design_present_peripheral (design, peripheral_name, cmac_regs);
            printf ("    Core mode: %s\n", core_mode_names[core_mode]);
            printf ("    Core version: %u.%u\n", core_version_major, core_version_minor);
        }
    }
}


/**
 * @brief Display information about the MRMAC ports in an identified design
 * @details
 *   The information displayed is:
 *   1. The hardware revision of the core. The documentation gives only one expected value. I.e. doesn't appear to have been multiple
 *      revisions released.
 *   2. Port configuration for the following, for checking against how the MRMAC should be been configured:
 *      - Data rate
 *      - AXI4-stream mode
 *      - GT quad operating mode
 *      - FEC mode
 *   3. The current Tx and Rx realtime status, for checking for link errors.
 *      @todo Consider also reporting, and then clearing, the latched status. When do that also try and check if the latched status
 *            is lost when VFIO generates a PCIe hot-reset on opening or closing the device.
 *            E.g. could remove and then-reinsert the QSFP+ modules while the device is closed and see if receive errors remain latched.
 * @param[in] design The identified design containing the MRMAC ports
 */
static void display_mrmac_ports (const fpga_design_t *const design)
{
    const char *const port_data_rate_names[] =
    {
        [MRMAC_CTL_DATA_RATE_10GE ] = "10GE",
        [MRMAC_CTL_DATA_RATE_25GE ] = "25GE",
        [MRMAC_CTL_DATA_RATE_40GE ] = "40GE",
        [MRMAC_CTL_DATA_RATE_50GE ] = "50GE",
        [MRMAC_CTL_DATA_RATE_100GE] = "100GE"
    };

    /* Lookup for PG314 "Table 4: MRMAC AXI4-Stream Modes":
     * - Outer index is Port Data Rate field
     * - Inner index is Port AXI4-Stream Mode field */
    const char *const axi4_stream_mode_names[][8] =
    {
        [MRMAC_CTL_DATA_RATE_100GE] =
        {
            [0] = "Low Latency, 256 bits, Non-Segmented",
            [5] = "Independent, 384 bits, Non-Segmented",
            [7] = "Independent, 384 bits, Segmented"
        },
        [MRMAC_CTL_DATA_RATE_40GE] =
        {
            [0] = "Low Latency, 128 bits, Non-Segmented",
            [5] = "Independent, 256 bits, Non-Segmented"
        },
        [MRMAC_CTL_DATA_RATE_50GE] =
        {
            [0] = "Low Latency, 128 bits, Non-Segmented",
            [5] = "Independent, 256 bits, Non-Segmented"
        },
        [MRMAC_CTL_DATA_RATE_25GE] =
        {
            [0] = "Low Latency, 64 bits, Non-Segmented",
            [1] = "Independent, 64 bits, Non-Segmented",
            [5] = "Independent, 128 bits, Non-Segmented"
        },
        [MRMAC_CTL_DATA_RATE_10GE] =
        {
            [0] = "Low Latency, 32 bits, Non-Segmented",
            [1] = "Independent, 32 bits, Non-Segmented"
        }
    };

    /* Lookup for PG314 "Table 7: GT Quad Operating Modes":
     * - Outer index is Port Data Rate Field
     * - Inner index is Port Serdes width
     *
     * For some combinations of Port Data Rate and Port Serdes, PG314 gives multiple possible MRMAC Operating Modes.
     * "OR" is used to indicate when multiple modes are possible, and this lookup table can't indicate the actual mode. */
    const char *const gt_quad_operating_mode_names[][8] =
    {
        [MRMAC_CTL_DATA_RATE_10GE] =
        {
            [0] = "10GE Narrow, 10.3125 Gb/s, 16 bits, 644.5313 MHz, NRZ",
            [4] = "10GE Wide, 10.3125 Gb/s, 32 bits, 322.2656 MHz, NRZ"
        },
        [MRMAC_CTL_DATA_RATE_25GE] =
        {
            [2] = "25GE Narrow, 25.78125 Gb/s, 40 bits, 644.5313 MHz, NRZ",
            [6] = "25GE Wide, 25.78125 Gb/s, 80 bits, 322.2656 MHz, NRZ"
        },
        [MRMAC_CTL_DATA_RATE_40GE] =
        {
            [0] = "40GE XLAUI-4 Narrow, 10.3125 Gb/s, 16 bits, 644.5313 MHz, NRZ",
            [4] = "40GE XLAUI-4 Wide, 10.3125 Gb/s, 32 bits, 322.2656 MHz, NRZ"
        },
        [MRMAC_CTL_DATA_RATE_50GE] =
        {
            [2] = "50GE 50GAUI-2 (KP4 FEC) Narrow, 26.5625 Gb/s , 40 bits, 664.062  MHz, NRZ\n    OR "
                  "50GE LAUI-2 Consortium Narrow,  25.78125 Gb/s, 40 bits, 644.5313 MHz, NRZ",
            [3] = "50GE 50LAUI-1 Narrow          , 51.5625 Gb/s, 80 bits, 644.531 MHz , PAM4\n    OR "
                  "50GE 50GAUI-1 (KP4 FEC) Narrow, 53.125 Gb/s,  80 bits, 664.0625 MHz, PAM4",
            [6] = "50GE 50GAUI-2 (KP4 FEC) Wide, 26.5625 Gb/s,   80 bits, 332.0312 MHz, NRZ\n    OR "
                  "50GE LAUI-2 Consortium Wide,  25.78125 Gb/s,  80 bits, 322.2656 MHz, NRZ\n    OR "
                  "50GE 50GAUI-1 Wide,           53.125 Gb/s,   160 bits, 664.0625 MHz, PAM4"
        },
        [MRMAC_CTL_DATA_RATE_100GE] =
        {
            [2] = "100GE CAUI-4 Narrow,               25.78125 Gb/s, 40 bits, 644.5313 MHz, NRZ\n    OR "
                  "100GE 100GAUI-4 (KP4 FEC) Narrow,  26.5625 Gb/s,  40 bits, 664.0625 MHz, NRZ\n    OR "
                  "100GE 100GAUI-1 Narrow,           106.25 Gb/s,   160 bits, 664.0625 MHz, PAM4",
            [3] = "100GE 100GAUI-2 (KP4 FEC) Narrow, 53.125 Gb/s,  80 bits, 664.0625 MHz, PAM4\n    OR "
                  "100GE CAUI-2 Narrow,              51.5625 Gb/s, 80 bits, 644.531 MHz,  PAM4",
            [6] = "100GE CAUI-4 Wide,                 25.78125 Gb/s, 80 bits, 322.2656 MHz,  NRZ\n    OR "
                  "100GE CAUI-4 (Overclocking) Wide,  28.21 Gb/s,    80 bits, 352.625 MHz,   NRZ\n    OR "
                  "100GE 100GAUI-4 (KP4 FEC) Wide,    26.5625 Gb/s,  80 bits, 332.0312 MHz,  NRZ\n    OR "
                  "100GE 100GAUI-2 Wide,              53.125 Gb/s,  160 bits, 332.03125 MHz, PAM4\n    OR "
                  "100GE 100GAUI-2 (Overclocking),    56.42 Gb/s,   160 bits, 352.625 MHz,   PAM4\n    OR "
                  "100GE 100GAUI-1 Wide,             106.25 Gb/s,   160 bits, 332.03125 MHz, PAM4"
        }
    };

    /* Lookup for PG314 Table 6: FEC Operating Modes:
     * - Outer index is Port Data Rate Field
     * - Inner index is Port Serdes width
     *
     * 32GFEC entries are not used, since no support for the Flex Interface. */
    const char *const fec_operating_mode_names[][16] =
    {
        [MRMAC_CTL_DATA_RATE_100GE] =
        {
            [ 0] = "FEC Disabled",
            [ 8] = "IEEE 802.3 RS(528,514) FEC",
            [10] = "IEEE P802.3cd/D3.5 CL91 RS(544,514) FEC",
            [11] = "ITU-T FlexEO RS(544,514) FEC"
        },
        [MRMAC_CTL_DATA_RATE_50GE] =
        {
            [ 0] = "FEC Disabled",
            [ 4] = "25G/50G Ethernet Consortium RS(528,517) FEC",
            [ 5] = "IEEE 802.3 CL134 RS(544,514) FEC",
            [15] = "IEEE 802.3 CL74 FEC"
        },
        [MRMAC_CTL_DATA_RATE_40GE] =
        {
            [ 0] = "FEC Disabled",
            [13] = "IEEE 802.3 CL74 FEC",
        },
        [MRMAC_CTL_DATA_RATE_25GE] =
        {
            [ 0] = "FEC Disabled",
            [ 2] = "25G/50G Ethernet Consortium RS(528,514) FEC",
            [ 3] = "IEEE 802.3 CL108 RS(528,514) FEC",
            [14] = "IEEE 802.3 CL74 FEC"
        },
        [MRMAC_CTL_DATA_RATE_10GE] =
        {
            [ 0] = "FEC Disabled",
            [12] = "IEEE 802.3 CL74 FEC"
        }
    };

    const uint32_t configuration_revision = read_reg32 (design->mrmac.regs, MRMAC_CONFIGURATION_REVISION_REG_OFFSET);

    printf ("  MRMAC configuration revision: 0x%08X\n", configuration_revision);
    for (uint32_t port_num = 0; port_num < NUM_MRMAC_PORTS; port_num++)
    {
        if (design->mrmac.used_ports[port_num])
        {
            const uint8_t *const port_regs = &design->mrmac.regs[port_num * MRMAC_PORT_REGS_FRAME_SIZE];

            const uint32_t mode_reg = read_reg32 (port_regs, MRMAC_MODE_REG_OFFSET);
            const uint32_t port_data_rate = vfio_extract_field_u32 (mode_reg, MRMAC_CTL_DATA_RATE_MASK);
            const uint32_t port_serdes_width = vfio_extract_field_u32 (mode_reg, MRMAC_CTL_SERDES_WIDTH_MASK);
            const uint32_t port_axi4_stream_mode = vfio_extract_field_u32 (mode_reg, MRMAC_CTL_AXIS_CFG_MASK);

            const uint32_t configuration_rx_mtu_reg = read_reg32 (port_regs, MRMAC_CONFIGURATION_RX_MTU_OFFSET);
            const uint32_t rx_min_packet_len = vfio_extract_field_u32 (configuration_rx_mtu_reg, MRMAC_CTL_RX_MIN_PACKET_LEN_MASK);
            const uint32_t rx_max_packet_len = vfio_extract_field_u32 (configuration_rx_mtu_reg, MRMAC_CTL_RX_MAX_PACKET_LEN_MASK);

            const uint32_t fec_configuration_reg1 = read_reg32 (port_regs, MRMAC_FEC_CONFIGURATION_REG1_OFFSET);
            const uint32_t port_fec_mode = vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_FEC_MODE_MASK);

            const uint32_t stat_tx_rt_status_reg1 = read_reg32 (port_regs, MRMAC_STAT_TX_RT_STATUS_REG1_OFFET);

            const uint32_t stat_rx_rt_status_reg1 = read_reg32 (port_regs, MRMAC_STAT_RX_RT_STATUS_REG1_OFFSET);

            printf ("  Port %u:\n", port_num);
            printf ("    Data rate: ");
            if ((port_data_rate < VFIO_NELEMENTS (port_data_rate_names)) && (port_data_rate_names[port_data_rate] != NULL))
            {
                printf ("%s\n", port_data_rate_names[port_data_rate]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_data_rate);
            }
            printf ("    GT Quad Operating mode: ");
            if ((port_data_rate < VFIO_NELEMENTS (gt_quad_operating_mode_names)) &&
                (gt_quad_operating_mode_names[port_data_rate][port_serdes_width] != NULL))
            {
                printf ("%s\n", gt_quad_operating_mode_names[port_data_rate][port_serdes_width]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_serdes_width);
            }
            printf ("    AXI4-Stream Mode: ");
            if ((port_data_rate < VFIO_NELEMENTS (axi4_stream_mode_names)) &&
                    (axi4_stream_mode_names[port_data_rate][port_axi4_stream_mode] != NULL))
            {
                printf ("%s\n", axi4_stream_mode_names[port_data_rate][port_axi4_stream_mode]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_axi4_stream_mode);
            }

            printf ("    Rx min packet len: %u\n", rx_min_packet_len);
            printf ("    Rx max packet len: %u\n", rx_max_packet_len);

            printf ("    FEC Operating Mode: ");
            if ((port_data_rate < VFIO_NELEMENTS (fec_operating_mode_names)) &&
                    (fec_operating_mode_names[port_data_rate][port_fec_mode] != NULL))
            {
                printf ("%s\n", fec_operating_mode_names[port_data_rate][port_fec_mode]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_fec_mode);
            }

            printf ("    TX realtime status: 0x%08X\n", stat_tx_rt_status_reg1);
            printf ("      Port TX local fault            : %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_LOCAL_FAULT_MASK));
            printf ("      Port TX axis underflow         : %u \n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_AXIS_UNF_MASK));
            printf ("      Port TX axis error             : %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_AXIS_ERR_MASK));
            printf ("      Port TX flexif error           : %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_FLEXIF_ERR_MASK));
            printf ("      Port TX pcs bad code           : %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_PCS_BAD_CODE_MASK));
            printf ("      Port TX CL82 CL49 convert error: %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_CL82_49_CONVERT_ERR_MASK));
            printf ("      Port TX flex fifo overflow     : %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_FLEX_FIFO_OVF_MASK));
            printf ("      Port TX flex fifo underflow    : %u\n",
                    vfio_extract_field_u32 (stat_tx_rt_status_reg1, MRMAC_STAT_TX_FLEX_FIFO_UDF_MASK));

            printf ("    RX realtime status: 0x%08X\n", stat_rx_rt_status_reg1);
            printf ("      Port RX Status: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_STATUS_MASK));
            printf ("      Port RX Block Lock: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_BLOCK_LOCK_MASK));
            printf ("      Port RX aligned: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_ALIGNED_MASK));
            printf ("      Port RX misaligned: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_MISALIGNED_MASK));
            printf ("      Port RX aligned error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_ALIGNED_ERR_MASK));
            printf ("      Port RX High BER: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_HI_BER_MASK));
            printf ("      Port RX remote fault: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_REMOTE_FAULT_MASK));
            printf ("      Port RX local fault: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_LOCAL_FAULT_MASK));
            printf ("      Port RX internal local fault: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_INTERNAL_LOCAL_FAULT_MASK));
            printf ("      Port RX received local fault: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_RECEIVED_LOCAL_FAULT_MASK));
            printf ("      Port RX bad code: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_BAD_CODEMASK));
            printf ("      Port RX bad preamble: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_BAD_PREAMBLE_MASK));
            printf ("      Port RX bad SFD: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_BAD_SFD_MASK));
            printf ("      Port RX got signal ordered set: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_GOT_SIGNAL_OS_MASK));
            printf ("      Port RX flex if error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_FLEXIF_ERR_MASK));
            printf ("      Port RX Framing Error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_FRAMING_ERR_MASK));
            printf ("      Port RX Synced: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_SYNCED_MASK));
            printf ("      Port RX Synced Error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_SYNCED_ERR_MASK));
            printf ("      Port RX BIP Error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_BIP_ERR_MASK));
            printf ("      Port RX CL49_82 convert error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_CL49_82_CONVERT_ERR_MASK));
            printf ("      Port RX pcs bad code: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_PCS_BAD_CODE_MASK));
            printf ("      Port RX AXIS fifo overflow: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_AXIS_FIFO_OVERFLOW_MASK));
            printf ("      Port RX AXIS error: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MCMAC_STAT_RX_AXIS_ERR_MASK));
            printf ("      Port RX invalid start: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_INVALID_START_MASK));
            printf ("      Port RX flex fifo overflow: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_FLEX_FIFO_OVF_MASK));
            printf ("      Port RX flex fifo underflow: %u\n",
                    vfio_extract_field_u32 (stat_rx_rt_status_reg1, MRMAC_STAT_RX_FLEX_FIFO_UDF_MASK));
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
        if (design->qdma_present)
        {
            display_qdma (design);
        }
        if (design->user_access != NULL)
        {
            const uint32_t user_access = read_reg32 (design->user_access, 0);
            char formatted_timestamp[USER_ACCESS_TIMESTAMP_LEN];

            format_user_access_timestamp (user_access, formatted_timestamp);
            printf ("  User access build timestamp : %08X - %s\n", user_access, formatted_timestamp);
        }
        if (design->ultrascale_dna_regs != NULL)
        {
            display_ultrascale_dna (design);
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
        if (design->mrmac.regs != NULL)
        {
            display_mrmac_ports (design);
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
