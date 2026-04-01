/*
 * @file mrmac_register_access.c
 * @date 30 Mar 2026
 * @author Chester Gillon
 * @brief Implements functions to access the MRMAC Configuration registers, Status registers, and Statistics counters.
 */

#include "mrmac_register_access.h"
#include "mrmac_axi4_lite_registers.h"

#include <stdio.h>

#include <time.h>


/* Names for MRMAC_CTL_DATA_RATE_MASK */
const char *const mrmac_port_data_rate_names[] =
{
    [MRMAC_CTL_DATA_RATE_10GE ] = "10GE",
    [MRMAC_CTL_DATA_RATE_25GE ] = "25GE",
    [MRMAC_CTL_DATA_RATE_40GE ] = "40GE",
    [MRMAC_CTL_DATA_RATE_50GE ] = "50GE",
    [MRMAC_CTL_DATA_RATE_100GE] = "100GE"
};
const uint32_t mrmac_num_port_data_rate_names = VFIO_NELEMENTS (mrmac_port_data_rate_names);


/* Lookup for PG314 "Table 4: MRMAC AXI4-Stream Modes":
 * - Outer index is Port Data Rate field
 * - Inner index is Port AXI4-Stream Mode field */
const char *const mrmac_axi4_stream_mode_names[][MRMAC_CTL_DATA_RATE_ARRAY_SIZE] =
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
const uint32_t mrmac_num_axi4_stream_mode_names = VFIO_NELEMENTS (mrmac_axi4_stream_mode_names);


/* Lookup for PG314 "Table 7: GT Quad Operating Modes":
 * - Outer index is Port Data Rate Field
 * - Inner index is Port Serdes width
 *
 * For some combinations of Port Data Rate and Port Serdes, PG314 gives multiple possible MRMAC Operating Modes.
 * "OR" is used to indicate when multiple modes are possible, and this lookup table can't indicate the actual mode. */
const char *const mrmac_gt_quad_operating_mode_names[][MRMAC_CTL_DATA_RATE_ARRAY_SIZE] =
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
const uint32_t mrmac_num_gt_quad_operating_mode_names = VFIO_NELEMENTS (mrmac_gt_quad_operating_mode_names);


/* Lookup for PG314 Table 6: FEC Operating Modes:
 * - Outer index is Port Data Rate Field
 * - Inner index is Port Serdes width
 *
 * 32GFEC entries are not used, since no support for the Flex Interface. */
const char *const mrmac_fec_operating_mode_names[][MRMAC_CTL_FEC_MODE_ARRAY_SIZE] =
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
const uint32_t mrmac_num_fec_operating_mode_names = VFIO_NELEMENTS (mrmac_fec_operating_mode_names);


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
void display_mrmac_ports (const fpga_design_t *const design)
{
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
            if ((port_data_rate < mrmac_num_port_data_rate_names) && (mrmac_port_data_rate_names[port_data_rate] != NULL))
            {
                printf ("%s\n", mrmac_port_data_rate_names[port_data_rate]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_data_rate);
            }
            printf ("    GT Quad Operating mode: ");
            if ((port_data_rate < mrmac_num_gt_quad_operating_mode_names) &&
                (mrmac_gt_quad_operating_mode_names[port_data_rate][port_serdes_width] != NULL))
            {
                printf ("%s\n", mrmac_gt_quad_operating_mode_names[port_data_rate][port_serdes_width]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_serdes_width);
            }
            printf ("    AXI4-Stream Mode: ");
            if ((port_data_rate < mrmac_num_axi4_stream_mode_names) &&
                    (mrmac_axi4_stream_mode_names[port_data_rate][port_axi4_stream_mode] != NULL))
            {
                printf ("%s\n", mrmac_axi4_stream_mode_names[port_data_rate][port_axi4_stream_mode]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_axi4_stream_mode);
            }

            printf ("    Rx min packet len: %u\n", rx_min_packet_len);
            printf ("    Rx max packet len: %u\n", rx_max_packet_len);

            printf ("    FEC Operating Mode: ");
            if ((port_data_rate < mrmac_num_fec_operating_mode_names) &&
                    (mrmac_fec_operating_mode_names[port_data_rate][port_fec_mode] != NULL))
            {
                printf ("%s\n", mrmac_fec_operating_mode_names[port_data_rate][port_fec_mode]);
            }
            else
            {
                printf ("Unknown (0x%x)\n", port_fec_mode);
            }
            printf ("    fec_configuration_reg1: 0x%08X\n"
                    "      ctl_fec_mode=%u\n"
                    "      ctl_rx_fec_bypass_indication=%u ctl_rx_fec_bypass_correction=%u\n"
                    "      ctl_rx_fec_transcode_clause49=%u\n"
                    "      ctl_rx_fec_alignment_bypass=%u ctl_tx_fec_transcode_bypass=%u\n"
                    "      ctl_rx_fec_transcode_bypass=%u ctl_rx_fec_cdc_bypass=%u\n"
                    "      ctl_rx_fec_errind_mode=%u ctl_tx_fec_four_lane_pmd=%u\n",
                    fec_configuration_reg1,
                    port_fec_mode,
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_BYPASS_INDICATION),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_BYPASS_CORRECTION),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_TRANSCODE_CLAUSE49),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_ALIGNMENT_BYPASS),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_TX_FEC_TRANSCODE_BYPASS),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_TRANSCODE_BYPASS),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_CDC_BYPASS),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_RX_FEC_ERRIND_MODE),
                    vfio_extract_field_u32 (fec_configuration_reg1, MRMAC_CTL_TX_FEC_FOUR_LANE_PMD));

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


/**
 * @brief Initialise an iterator for MRMAC ports
 * @param[out] iterator The initialised iterator
 * @param[in] designs The designs to iterate over
 * @param[in] port_num_filter Optional port number filter
 * @param[in] port_num_filter_specified If true, the iterator will only return port number which are present and match port_num_filter
 */
void mrmac_port_iterator_initialise (mrmac_port_iterator_t *const iterator, fpga_designs_t *const designs,
                                     const uint32_t port_num_filter, const bool port_num_filter_specified)
{
    iterator->designs = designs;
    iterator->current_design_index = 0;
    iterator->current_port_index = 0;
    iterator->port_num_filter = port_num_filter;
    iterator->port_num_filter_specified = port_num_filter_specified;
}


/**
 * @brief Get the next MRMAC port to processed by an iterator
 * @param[in,out] iterator The iterator to advance
 * @param[out] port_num The next port number to be processed
 * @return If non-NULL the design containing the next MRMAC port to process.
 *         If NULL the iterator is complete.
 */
fpga_design_t *mrmac_port_iterator_next (mrmac_port_iterator_t *const iterator, uint32_t *const port_num)
{
    fpga_design_t *available_design = NULL;

    *port_num = 0;
    while ((available_design == NULL) && (iterator->current_design_index < iterator->designs->num_identified_designs))
    {
        if (iterator->designs->designs[iterator->current_design_index].mrmac.regs != NULL)
        {
            const bool port_num_wanted = (!iterator->port_num_filter_specified) ||
                    (iterator->port_num_filter_specified && (iterator->current_port_index == iterator->port_num_filter));

            if (iterator->designs->designs[iterator->current_design_index].mrmac.used_ports[iterator->current_port_index] &&
                    port_num_wanted)
            {
                /* Have found a wanted MRMAC port to return */
                available_design = &iterator->designs->designs[iterator->current_design_index];
                *port_num = iterator->current_port_index;
            }

            /* Advance the iterator to the next possible port / design */
            iterator->current_port_index++;
            if (iterator->current_port_index == NUM_MRMAC_PORTS)
            {
                iterator->current_port_index = 0;
                iterator->current_design_index++;
            }
        }
        else
        {
            /* Skip design with no MRMAC */
            iterator->current_design_index++;
        }
    }

    return available_design;
}


/**
 * @brief Reset a MRMAC port
 * @details This asserts all reset bits for the port, as is intended following a configuration change rather than recovering
 *          from a specific error.
 * @param[in,out] design Contains the design with the MRMAC
 * @param[in] Which MRMAC port to reset.
 */
void mrmac_reset_port (fpga_design_t *const design, const uint32_t port_num)
{
    uint8_t *const port_regs = &design->mrmac.regs[port_num * MRMAC_PORT_REGS_FRAME_SIZE];
    const uint32_t all_reset_bits = MRMAC_RX_SERDES_RESET | MRMAC_TX_SERDES_RESET | MRMAC_RX_RESET | MRMAC_TX_RESET |
            MRMAC_RX_FLEXIF_RESET | MRMAC_RX_AXI_RESET | MRMAC_TX_AXI_RESET;

    /* Apply reset for 100 microseconds, in the absence of any minimum reset duration in the MRMAC documentation */
    const struct timespec reset_duration =
    {
        .tv_sec = 0,
        .tv_nsec = 100000
    };

    write_reg32 (port_regs, MRMAC_RESET_REG_OFFSET, all_reset_bits);
    clock_nanosleep (CLOCK_MONOTONIC, 0, &reset_duration, NULL);
    write_reg32 (port_regs, MRMAC_RESET_REG_OFFSET, 0);
}
