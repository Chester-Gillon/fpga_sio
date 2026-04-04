/*
 * @file mrmac_register_access.c
 * @date 30 Mar 2026
 * @author Chester Gillon
 * @brief Implements functions to access the MRMAC Configuration registers, Status registers, and Statistics counters.
 */

#include "mrmac_register_access.h"
#include "mrmac_axi4_lite_registers.h"
#include "transfer_timing.h"

#include <string.h>
#include <inttypes.h>
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


/* Define the register offsets and names for the statistics counters in one MRMAC port.
 * MRMAC_STAT_COMMON_COUNTER_DEF and MRMAC_STAT_PER_PORT_COUNTER_DEF are used to set the name to the same as that of the
 * register offsets without the prefix and suffix. Where the register offsets names were copied from the mrmac-registers-v3-0.xlsx.
 * This avoids the need to populate the longer descriptive names for the statistics counters which are in the
 * mrmac-registers-v3-0.xlsx.
 *
 * MRMAC_STAT_COMMON_COUNTER_DEF is used when all ports implement a statistics counter, and the definitions for all port offsets
 * can be formed by token pasting.
 *
 * MRMAC_STAT_PER_PORT_COUNTER_DEF is used when not all ports implement a statistics counter and macro arguments need to be
 * supplied for each port giving either:
 * a. The definition of the offset for an implemented statistics counter for the port.
 * b. 0 if the port doesn't implement the statistics counter. */
#define STRINGIFY_HELPER(X) #X
#define STRINGIFY(X) STRINGIFY_HELPER(X)

#define MRMAC_STAT_COMMON_COUNTER_DEF(name_param) [MRMAC_STAT_##name_param] = \
    {.name = STRINGIFY(name_param), \
     .lsb_offsets = {MRMAC_STAT_##name_param##_0_LSB, MRMAC_STAT_##name_param##_1_LSB, \
                     MRMAC_STAT_##name_param##_2_LSB, MRMAC_STAT_##name_param##_3_LSB}}

#define MRMAC_STAT_PER_PORT_COUNTER_DEF(name_param,port0_offset,port1_offset,port2_offset,port3_offset) [MRMAC_STAT_##name_param] = \
        {.name = STRINGIFY(name_param), \
         .lsb_offsets = {port0_offset, port1_offset, port2_offset, port3_offset}}

const mrmac_statistics_counter_definition_t mrmac_statistics_counter_definitions[MRMAC_STAT_ARRAY_SIZE] =
{
    /* All transmit statistics counters are common across all ports */
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_CYCLE_COUNT),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_FRAME_ERROR),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_TOTAL_PACKETS),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_TOTAL_GOOD_PACKETS),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_TOTAL_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_TOTAL_GOOD_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_64_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_65_127_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_128_255_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_256_511_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_512_1023_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_1024_1518_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_1519_1522_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_1523_1548_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_1549_2047_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_2048_4095_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_4096_8191_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_8192_9215_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_LARGE),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PACKET_SMALL),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_BAD_FCS),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_UNICAST),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_MULTICAST),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_BROADCAST),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_VLAN),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PAUSE),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_USER_PAUSE),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_TSN_PREEMPTED_PKT),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_TSN_FRAGMENT),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_PCS_BAD_CODE),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_CL82_49_CONVERT_ERR),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_ECC_ERR0),
    MRMAC_STAT_COMMON_COUNTER_DEF(TX_ECC_ERR1),

    /* The receive statistics counters which are per lane or per FEC slice are not implemented on all ports */
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_CYCLE_COUNT),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_0, MRMAC_STAT_RX_BIP_ERR_0_0_LSB, 0, MRMAC_STAT_RX_BIP_ERR_2_0_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_1, MRMAC_STAT_RX_BIP_ERR_0_1_LSB, 0, MRMAC_STAT_RX_BIP_ERR_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_2, MRMAC_STAT_RX_BIP_ERR_0_2_LSB, 0, MRMAC_STAT_RX_BIP_ERR_2_2_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_3, MRMAC_STAT_RX_BIP_ERR_0_3_LSB, 0, MRMAC_STAT_RX_BIP_ERR_2_3_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_4, MRMAC_STAT_RX_BIP_ERR_0_4_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_5, MRMAC_STAT_RX_BIP_ERR_0_5_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_6, MRMAC_STAT_RX_BIP_ERR_0_6_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_7, MRMAC_STAT_RX_BIP_ERR_0_7_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_8, MRMAC_STAT_RX_BIP_ERR_0_8_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_9, MRMAC_STAT_RX_BIP_ERR_0_9_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_10, MRMAC_STAT_RX_BIP_ERR_0_10_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_11, MRMAC_STAT_RX_BIP_ERR_0_11_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_12, MRMAC_STAT_RX_BIP_ERR_0_12_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_13, MRMAC_STAT_RX_BIP_ERR_0_13_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_14, MRMAC_STAT_RX_BIP_ERR_0_14_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_15, MRMAC_STAT_RX_BIP_ERR_0_15_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_16, MRMAC_STAT_RX_BIP_ERR_0_16_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_17, MRMAC_STAT_RX_BIP_ERR_0_17_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_18, MRMAC_STAT_RX_BIP_ERR_0_18_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_BIP_ERR_19, MRMAC_STAT_RX_BIP_ERR_0_19_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_0, MRMAC_STAT_RX_FRAMING_ERR_0_0_LSB, MRMAC_STAT_RX_FRAMING_ERR_1_LSB,
                                                      MRMAC_STAT_RX_FRAMING_ERR_2_0_LSB, MRMAC_STAT_RX_FRAMING_ERR_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_1, MRMAC_STAT_RX_FRAMING_ERR_0_1_LSB, 0, MRMAC_STAT_RX_FRAMING_ERR_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_2, MRMAC_STAT_RX_FRAMING_ERR_0_2_LSB, 0, MRMAC_STAT_RX_FRAMING_ERR_2_2_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_3, MRMAC_STAT_RX_FRAMING_ERR_0_3_LSB, 0, MRMAC_STAT_RX_FRAMING_ERR_2_3_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_4, MRMAC_STAT_RX_FRAMING_ERR_0_4_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_5, MRMAC_STAT_RX_FRAMING_ERR_0_5_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_6, MRMAC_STAT_RX_FRAMING_ERR_0_6_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_7, MRMAC_STAT_RX_FRAMING_ERR_0_7_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_8, MRMAC_STAT_RX_FRAMING_ERR_0_8_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_9, MRMAC_STAT_RX_FRAMING_ERR_0_9_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_10, MRMAC_STAT_RX_FRAMING_ERR_0_10_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_11, MRMAC_STAT_RX_FRAMING_ERR_0_11_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_12, MRMAC_STAT_RX_FRAMING_ERR_0_12_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_13, MRMAC_STAT_RX_FRAMING_ERR_0_13_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_14, MRMAC_STAT_RX_FRAMING_ERR_0_14_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_15, MRMAC_STAT_RX_FRAMING_ERR_0_15_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_16, MRMAC_STAT_RX_FRAMING_ERR_0_16_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_17, MRMAC_STAT_RX_FRAMING_ERR_0_17_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_18, MRMAC_STAT_RX_FRAMING_ERR_0_18_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FRAMING_ERR_19, MRMAC_STAT_RX_FRAMING_ERR_0_19_LSB, 0, 0, 0),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_BAD_CODE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PCS_BAD_CODE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_INVALID_START),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CW_0, MRMAC_STAT_RX_FEC_CW_0_0_LSB, MRMAC_STAT_RX_FEC_CW_1_LSB,
                                                 MRMAC_STAT_RX_FEC_CW_2_0_LSB, MRMAC_STAT_RX_FEC_CW_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CW_1, MRMAC_STAT_RX_FEC_CW_0_1_LSB, 0, MRMAC_STAT_RX_FEC_CW_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CW_2, MRMAC_STAT_RX_FEC_CW_0_2_LSB, 0, MRMAC_STAT_RX_FEC_CW_2_2_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CW_3, MRMAC_STAT_RX_FEC_CW_0_3_LSB, 0, MRMAC_STAT_RX_FEC_CW_2_3_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CORRECTED_CW_0, MRMAC_STAT_RX_FEC_CORRECTED_CW_0_0_LSB,
                                                           MRMAC_STAT_RX_FEC_CORRECTED_CW_1_LSB,
                                                           MRMAC_STAT_RX_FEC_CORRECTED_CW_2_0_LSB,
                                                           MRMAC_STAT_RX_FEC_CORRECTED_CW_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CORRECTED_CW_1, MRMAC_STAT_RX_FEC_CORRECTED_CW_0_1_LSB, 0,
                                                           MRMAC_STAT_RX_FEC_CORRECTED_CW_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CORRECTED_CW_2, MRMAC_STAT_RX_FEC_CORRECTED_CW_0_2_LSB, 0,
                                                           MRMAC_STAT_RX_FEC_CORRECTED_CW_2_2_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_CORRECTED_CW_3, MRMAC_STAT_RX_FEC_CORRECTED_CW_0_3_LSB, 0,
                                                           MRMAC_STAT_RX_FEC_CORRECTED_CW_2_3_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_UNCORRECTED_CW_0, MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_0_LSB,
                                                             MRMAC_STAT_RX_FEC_UNCORRECTED_CW_1_LSB,
                                                             MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_0_LSB,
                                                             MRMAC_STAT_RX_FEC_UNCORRECTED_CW_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_UNCORRECTED_CW_1, MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_1_LSB, 0,
                                                             MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_UNCORRECTED_CW_2, MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_2_LSB, 0,
                                                             MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_2_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_UNCORRECTED_CW_3, MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_3_LSB, 0,
                                                             MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_3_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_0TO1_0, MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_0_LSB,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_1_LSB,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_2_0_LSB,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_0TO1_1, MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_1_LSB, 0,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_0TO1_2, MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_2_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_0TO1_3, MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_3_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_1TO0_0, MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_0_LSB,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_1_LSB,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_2_0_LSB,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_1TO0_1, MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_1_LSB, 0,
                                                           MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_1TO0_2, MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_2_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_BIT_ERR_1TO0_3, MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_3_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_ERR_COUNT_0, MRMAC_STAT_RX_FEC_ERR_COUNT_0_0_LSB, MRMAC_STAT_RX_FEC_ERR_COUNT_1_LSB,
                                                        MRMAC_STAT_RX_FEC_ERR_COUNT_2_0_LSB, MRMAC_STAT_RX_FEC_ERR_COUNT_3_LSB),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_ERR_COUNT_1, MRMAC_STAT_RX_FEC_ERR_COUNT_0_1_LSB, 0,
                                                        MRMAC_STAT_RX_FEC_ERR_COUNT_2_1_LSB, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_ERR_COUNT_2, MRMAC_STAT_RX_FEC_ERR_COUNT_0_2_LSB, 0, 0, 0),
    MRMAC_STAT_PER_PORT_COUNTER_DEF(RX_FEC_ERR_COUNT_3, MRMAC_STAT_RX_FEC_ERR_COUNT_0_3_LSB, 0, 0, 0),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TOTAL_PACKETS),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TOTAL_GOOD_PACKETS),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TOTAL_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TOTAL_GOOD_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_64_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_65_127_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_128_255_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_256_511_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_512_1023_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_1024_1518_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_1519_1522_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_1523_1548_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_1549_2047_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_2048_4095_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_4096_8191_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_8192_9215_BYTES),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_LARGE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_SMALL),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_UNDERSIZE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_FRAGMENT),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_OVERSIZE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TOOLONG),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_JABBER),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_BAD_FCS),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PACKET_BAD_FCS),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_STOMPED_FCS),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_UNICAST),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_MULTICAST),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_BROADCAST),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_VLAN),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_PAUSE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_USER_PAUSE),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_INRANGEERR),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TRUNCATED),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TEST_PATTERN_MISMATCH),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_CL49_82_CONVERT_ERR),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TSN_PREEMPTED_PKT),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_TSN_FRAGMENT),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_ECC_ERR0),
    MRMAC_STAT_COMMON_COUNTER_DEF(RX_ECC_ERR1)
};


/* The monotonic time of the last sample tick for the statistics counters in each MRMAC port */
static int64_t port_last_sample_tick_times_ns[NUM_MRMAC_PORTS];


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

            /* Display FEC status if the FEC mode is other than disabled (zero) */
            if (port_fec_mode != 0)
            {
                const uint32_t stat_rx_fec_rt_status_reg = read_reg32 (port_regs, MRMAC_STAT_RX_FEC_RT_STATUS_REG_OFFSET);
                const uint32_t stat_tx_fec_rt_status_reg = read_reg32 (port_regs, MRMAC_STAT_TX_FEC_RT_STATUS_REG_OFFSET);

                printf ("    RX FEC realtime status: 0x%08X\n", stat_rx_fec_rt_status_reg);
                printf ("      Slice FEC aligned: %u\n",
                        vfio_extract_field_u32 (stat_rx_fec_rt_status_reg, MRMAC_STAT_RX_FEC_ALIGNED));
                printf ("      Slice FEC symbol error: %u\n",
                        vfio_extract_field_u32 (stat_rx_fec_rt_status_reg, MRMAC_STAT_RX_FEC_HI_SER));
                printf ("      Slice FEC lane lock: %u\n",
                        vfio_extract_field_u32 (stat_rx_fec_rt_status_reg, MRMAC_STAT_RX_FEC_LANE_LOCK));

                printf ("    TX FEC realtime status: 0x%08X\n", stat_tx_fec_rt_status_reg);
                printf ("      FEC PCS aligned: %u\n",
                        vfio_extract_field_u32 (stat_tx_fec_rt_status_reg, MRMAC_STAT_TX_FEC_PCS_LANE_ALIGN));
                printf ("      FEC PCS block lock: %u\n",
                        vfio_extract_field_u32 (stat_tx_fec_rt_status_reg, MRMAC_STAT_TX_FEC_PCS_BLOCK_LOCK));
                printf ("      FEC PCS alignment marker lock: %u\n",
                        vfio_extract_field_u32 (stat_tx_fec_rt_status_reg, MRMAC_STAT_TX_FEC_PCS_AM_LOCK));
            }
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


/**
 * @brief Snapshot the statistic counters for one MRMAC port
 * @details This uses the MRMAC tick mechanism which snapshots the internal counters. The tick mechanism resets
 *          the internal counters to zero for the specific port. I.e.:
 *          a. Each call to this will reset the internal statistics counts for the next sampling interval.
 *          b. If multiple threads or processes call this function for the same port the counts may under-read.
 *
 *          Have split the API into mrmac_snapshot_port_statistics() and mrmac_read_port_statistics() functions to allow counters
 *          for multiple MRMAC ports to be snapshot'ed as close togther as possible, before later reading the values.
 * @param[in,out] design Contains the design with the MRMAC
 * @param[in] Which MRMAC port to snapshot the statistic counters for
 * @param[in,out] stats The statistics counters
 */
void mrmac_snapshot_port_statistics (fpga_design_t *const design, const uint32_t port_num, mrmac_port_statistics_t *const stats)
{
    uint8_t *const port_regs = &design->mrmac.regs[port_num * MRMAC_PORT_REGS_FRAME_SIZE];

    stats->design = design;
    stats->port_num = port_num;

    /* Ensure the port mode register has the required fields:
     * a. Disable counter extend, since the statistics counter extension port isn't used.
     * b. Enable tick register mode, since the pm_tick pin isn't used.
     *
     * Only write if the current mode register doesn't have the expected value, since a configuration register which preserves
     * its contents across resets. */
    const uint32_t current_mode_reg = read_reg32 (port_regs, MRMAC_MODE_REG_OFFSET);
    uint32_t required_mode_reg;

    required_mode_reg = current_mode_reg;
    vfio_update_field_u32 (&required_mode_reg, MRMAC_CTL_COUNTER_EXTEND, 0);
    vfio_update_field_u32 (&required_mode_reg, MRMAC_TICK_REG_MODE_SEL, 1);
    if (current_mode_reg != required_mode_reg)
    {
        write_reg32 (port_regs, MRMAC_MODE_REG_OFFSET, required_mode_reg);
    }

    /* Clear the statistics ready indication from any previous sample */
    const uint32_t statistics_ready_reg = read_reg32 (port_regs, MRMAC_STAT_STATISTICS_READY_OFFSET);
    if (statistics_ready_reg != 0)
    {
        write_reg32 (port_regs, MRMAC_STAT_STATISTICS_READY_OFFSET, statistics_ready_reg);
    }

    /* Snapshot the statistics counters */
    stats->this_sample_tick_time_ns = get_monotonic_time ();
    write_reg32 (port_regs, MRMAC_TICK_REG_OFFSET, MRMAC_TICK_REG);
}


/**
 * @brief Read the statistics counters for one MRMAC port
 * @details Reads the values of which were snapshot'ed by the previous call to mrmac_snapshot_port_statistics()
 * @param[in,out] stats On return the statistics counters read from the port
 */
void mrmac_read_port_statistics (mrmac_port_statistics_t *const stats)
{
    uint8_t *const port_regs = &stats->design->mrmac.regs[stats->port_num * MRMAC_PORT_REGS_FRAME_SIZE];
    uint32_t statistics_ready_reg;

    /* Wait, with a timeout, for the statistics to be ready to read */
    const int64_t timeout = stats->this_sample_tick_time_ns + 100000000; /* 100 milliseconds */
    int64_t now;
    bool timed_out = false;
    bool ready = false;
    do
    {
        statistics_ready_reg = read_reg32 (port_regs, MRMAC_STAT_STATISTICS_READY_OFFSET);
        now = get_monotonic_time ();
        if ((statistics_ready_reg & MRMAC_STAT_STATISTICS_READY) == MRMAC_STAT_STATISTICS_READY)
        {
            ready = true;
        }
        else
        {
            if (now > timeout)
            {
                timed_out = true;
                printf ("Timed out waiting for statistic counters to be ready\n");
            }
        }
    } while (!timed_out && !ready);
    stats->ready_duration_ns = now - stats->this_sample_tick_time_ns;

    /* Store the counter values which have been snapshot'ed */
    for (uint32_t counter_index = 0; counter_index < MRMAC_STAT_ARRAY_SIZE; counter_index++)
    {
        const uint32_t lsb_offset = mrmac_statistics_counter_definitions[counter_index].lsb_offsets[stats->port_num];

        if (lsb_offset != 0)
        {
            /* Read an implemented counter */
            const uint32_t counter_lsb_register = read_reg32 (stats->design->mrmac.regs, lsb_offset);
            const uint32_t counter_msb_register = read_reg32 (stats->design->mrmac.regs, lsb_offset + 4);

            stats->counter_values[counter_index] = (((uint64_t) counter_msb_register & 0xffff) << 32) | counter_lsb_register;
        }
        else
        {
            /* Counter is not implemented for the port */
            stats->counter_values[counter_index] = 0;
        }
    }

    /* Record the duration between samples on the same port */
    if (port_last_sample_tick_times_ns[stats->port_num] != 0)
    {
        stats->sample_duration_ns = stats->this_sample_tick_time_ns - port_last_sample_tick_times_ns[stats->port_num];
        stats->sample_duration_valid = true;
    }
    else
    {
        stats->sample_duration_ns = 0;
        stats->sample_duration_valid = false;
    }
    port_last_sample_tick_times_ns[stats->port_num] = stats->this_sample_tick_time_ns;
}


/**
 * @brief Display the statistic counters for one MRMAC port
 * @param[in] stats The statistic counters to display
 */
void mrmac_display_port_statistics (const mrmac_port_statistics_t *const stats)
{
    uint32_t counter_index;

    /* Find the maximum length of all statistic counter names, to format the output */
    int max_name_len = 0;
    for (counter_index = 0; counter_index < MRMAC_STAT_ARRAY_SIZE; counter_index++)
    {
        const size_t name_len = strlen (mrmac_statistics_counter_definitions[counter_index].name);

        if (name_len > max_name_len)
        {
            max_name_len = (int) name_len;
        }
    }

    /* Only display counters with non-zero values:
     * a. For a more compact display.
     * b. To ignore non-implemented counters for a port, for which mrmac_read_port_statistics() stores a zero. */
    printf ("%s port %u statistics", fpga_design_names[stats->design->design_id], stats->port_num);
    if (stats->sample_duration_valid)
    {
        printf (" (over %.3f secs)", (double) stats->sample_duration_ns / 1E9);
    }
    printf (":\n");
    for (counter_index = 0; counter_index < MRMAC_STAT_ARRAY_SIZE; counter_index++)
    {
        const uint64_t counter_value = stats->counter_values[counter_index];

        if (counter_value != 0)
        {
            printf ("  %*s: %15" PRIu64 "%s\n", -max_name_len, mrmac_statistics_counter_definitions[counter_index].name,
                    counter_value, (counter_value == MRMAC_STAT_SATURATED_VALUE) ? " (saturated)" : "");
        }
    }
}
