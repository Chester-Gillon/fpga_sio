/*
 * @file cmac_register_access.h
 * @date 25 Apr 2026
 * @author Chester Gillon
 * @brief Provides functions to access the CMAC Configuration registers, Status registers, and Statistics counters.
 */

#ifndef CMAC_REGISTER_ACCESS_H_
#define CMAC_REGISTER_ACCESS_H_

#include "identify_pcie_fpga_design.h"


/* The statistics counters maintained by the CMAC for one port. */
typedef enum
{
    CMAC_STAT_CYCLE_COUNT,

    CMAC_STAT_TX_FRAME_ERROR,
    CMAC_STAT_TX_TOTAL_PACKETS,
    CMAC_STAT_TX_TOTAL_GOOD_PACKETS,
    CMAC_STAT_TX_TOTAL_BYTES,
    CMAC_STAT_TX_TOTAL_GOOD_BYTES,
    CMAC_STAT_TX_PACKET_64_BYTES,
    CMAC_STAT_TX_PACKET_65_127_BYTES,
    CMAC_STAT_TX_PACKET_128_255_BYTES,
    CMAC_STAT_TX_PACKET_256_511_BYTES,
    CMAC_STAT_TX_PACKET_512_1023_BYTES,
    CMAC_STAT_TX_PACKET_1024_1518_BYTES,
    CMAC_STAT_TX_PACKET_1519_1522_BYTES,
    CMAC_STAT_TX_PACKET_1523_1548_BYTES,
    CMAC_STAT_TX_PACKET_1549_2047_BYTES,
    CMAC_STAT_TX_PACKET_2048_4095_BYTES,
    CMAC_STAT_TX_PACKET_4096_8191_BYTES,
    CMAC_STAT_TX_PACKET_8192_9215_BYTES,
    CMAC_STAT_TX_PACKET_LARGE,
    CMAC_STAT_TX_PACKET_SMALL,
    CMAC_STAT_TX_BAD_FCS,
    CMAC_STAT_TX_UNICAST,
    CMAC_STAT_TX_MULTICAST,
    CMAC_STAT_TX_BROADCAST,
    CMAC_STAT_TX_VLAN,
    CMAC_STAT_TX_PAUSE,
    CMAC_STAT_TX_USER_PAUSE,

    CMAC_STAT_RX_BIP_ERR_0,
    CMAC_STAT_RX_BIP_ERR_1,
    CMAC_STAT_RX_BIP_ERR_2,
    CMAC_STAT_RX_BIP_ERR_3,
    CMAC_STAT_RX_BIP_ERR_4,
    CMAC_STAT_RX_BIP_ERR_5,
    CMAC_STAT_RX_BIP_ERR_6,
    CMAC_STAT_RX_BIP_ERR_7,
    CMAC_STAT_RX_BIP_ERR_8,
    CMAC_STAT_RX_BIP_ERR_9,
    CMAC_STAT_RX_BIP_ERR_10,
    CMAC_STAT_RX_BIP_ERR_11,
    CMAC_STAT_RX_BIP_ERR_12,
    CMAC_STAT_RX_BIP_ERR_13,
    CMAC_STAT_RX_BIP_ERR_14,
    CMAC_STAT_RX_BIP_ERR_15,
    CMAC_STAT_RX_BIP_ERR_16,
    CMAC_STAT_RX_BIP_ERR_17,
    CMAC_STAT_RX_BIP_ERR_18,
    CMAC_STAT_RX_BIP_ERR_19,
    CMAC_STAT_RX_FRAMING_ERR_0,
    CMAC_STAT_RX_FRAMING_ERR_1,
    CMAC_STAT_RX_FRAMING_ERR_2,
    CMAC_STAT_RX_FRAMING_ERR_3,
    CMAC_STAT_RX_FRAMING_ERR_4,
    CMAC_STAT_RX_FRAMING_ERR_5,
    CMAC_STAT_RX_FRAMING_ERR_6,
    CMAC_STAT_RX_FRAMING_ERR_7,
    CMAC_STAT_RX_FRAMING_ERR_8,
    CMAC_STAT_RX_FRAMING_ERR_9,
    CMAC_STAT_RX_FRAMING_ERR_10,
    CMAC_STAT_RX_FRAMING_ERR_11,
    CMAC_STAT_RX_FRAMING_ERR_12,
    CMAC_STAT_RX_FRAMING_ERR_13,
    CMAC_STAT_RX_FRAMING_ERR_14,
    CMAC_STAT_RX_FRAMING_ERR_15,
    CMAC_STAT_RX_FRAMING_ERR_16,
    CMAC_STAT_RX_FRAMING_ERR_17,
    CMAC_STAT_RX_FRAMING_ERR_18,
    CMAC_STAT_RX_FRAMING_ERR_19,
    CMAC_STAT_RX_BAD_CODE,
    CMAC_STAT_RX_TOTAL_PACKETS,
    CMAC_STAT_RX_TOTAL_GOOD_PACKETS,
    CMAC_STAT_RX_TOTAL_BYTES,
    CMAC_STAT_RX_TOTAL_GOOD_BYTES,
    CMAC_STAT_RX_PACKET_64_BYTES,
    CMAC_STAT_RX_PACKET_65_127_BYTES,
    CMAC_STAT_RX_PACKET_128_255_BYTES,
    CMAC_STAT_RX_PACKET_256_511_BYTES,
    CMAC_STAT_RX_PACKET_512_1023_BYTES,
    CMAC_STAT_RX_PACKET_1024_1518_BYTES,
    CMAC_STAT_RX_PACKET_1519_1522_BYTES,
    CMAC_STAT_RX_PACKET_1523_1548_BYTES,
    CMAC_STAT_RX_PACKET_1549_2047_BYTES,
    CMAC_STAT_RX_PACKET_2048_4095_BYTES,
    CMAC_STAT_RX_PACKET_4096_8191_BYTES,
    CMAC_STAT_RX_PACKET_8192_9215_BYTES,
    CMAC_STAT_RX_PACKET_LARGE,
    CMAC_STAT_RX_PACKET_SMALL,
    CMAC_STAT_RX_UNDERSIZE,
    CMAC_STAT_RX_FRAGMENT,
    CMAC_STAT_RX_OVERSIZE,
    CMAC_STAT_RX_TOOLONG,
    CMAC_STAT_RX_JABBER,
    CMAC_STAT_RX_BAD_FCS,
    CMAC_STAT_RX_PACKET_BAD_FCS,
    CMAC_STAT_RX_STOMPED_FCS,
    CMAC_STAT_RX_UNICAST,
    CMAC_STAT_RX_MULTICAST,
    CMAC_STAT_RX_BROADCAST,
    CMAC_STAT_RX_VLAN,
    CMAC_STAT_RX_PAUSE,
    CMAC_STAT_RX_USER_PAUSE,
    CMAC_STAT_RX_INRANGEERR,
    CMAC_STAT_RX_TRUNCATED,

    CMAC_STAT_OTN_TX_JABBER,
    CMAC_STAT_OTN_TX_OVERSIZE,
    CMAC_STAT_OTN_TX_UNDERSIZE,
    CMAC_STAT_OTN_TX_TOOLONG,
    CMAC_STAT_OTN_TX_FRAGMENT,
    CMAC_STAT_OTN_TX_PACKET_BAD_FCS,
    CMAC_STAT_OTN_TX_STOMPED_FCS,
    CMAC_STAT_OTN_TX_BAD_CODE,

    CMAC_STAT_RX_RSFEC_CORRECTED_CW_INC,
    CMAC_STAT_RX_RSFEC_UNCORRECTED_CW_INC,
    CMAC_STAT_RX_RSFEC_ERR_COUNT0_INC,
    CMAC_STAT_RX_RSFEC_ERR_COUNT1_INC,
    CMAC_STAT_RX_RSFEC_ERR_COUNT2_INC,
    CMAC_STAT_RX_RSFEC_ERR_COUNT3_INC,
    CMAC_STAT_RX_RSFEC_CW_INC,

    CMAC_STAT_ARRAY_SIZE
} cmac_statistic_counters_t;


/* Defines the value at which the CMAC statistics counters saturate, based upon then having 48 bits.
 * The "Status and Statistics Register Space" section in PG203 says the counters saturate to 1s. */
#define CMAC_STAT_NUM_BITS 48u
#define CMAC_STAT_SATURATED_VALUE ((1ULL << CMAC_STAT_NUM_BITS) - 1ULL)


/* Defines one statistic counter */
typedef struct
{
    /* Offsets for the LSB and MSB counter registers, from the base of the CMAC registers. */
    uint32_t lsb_offset;
    uint32_t msb_offset;
    /* The display name used for the statistics counter */
    const char *name;
} cmac_statistics_counter_definition_t;


/* The statistics for one CMAC port */
typedef struct
{
    /* Which design contains the CMAC */
    fpga_design_t *design;
    /* Which CMAC port the statistics are for */
    uint32_t port_num;
    /* The duration is nanoseconds over which the statistics are for.
     * Updated according to the interval between calls to cmac_snapshot_port_statistics() for each port.
     * This is based upon the time which which the tick mechanism is used to snapshot the counter values. */
    int64_t sample_duration_ns;
    /* Set when sample_duration_ns is valid, which will be after the 2nd and subsequent call to cmac_snapshot_port_statistics()
     * for a given port. */
    bool sample_duration_valid;
    /* The counter values read from the CMAC statistic registers. */
    uint64_t counter_values[CMAC_STAT_ARRAY_SIZE];
    /* The time at which the statistics were sampled by cmac_snapshot_port_statistics() */
    int64_t this_sample_tick_time_ns;
} cmac_port_statistics_t;


/* Specifies an iterator for operating on CMAC ports.
 * No device filter, since can be handled via vfio_add_pci_device_location_filter() */
typedef struct
{
    /* The opened FPGA designs */
    fpga_designs_t *designs;
    /* Current design index for the iterator */
    uint32_t current_design_index;
    /* Current port on current_design_index for the iterator */
    uint32_t current_port_index;
    /* Optional filter for only a single port */
    uint32_t port_num_filter;
    bool port_num_filter_specified;
} cmac_port_iterator_t;


extern const cmac_statistics_counter_definition_t cmac_statistics_counter_definitions[CMAC_STAT_ARRAY_SIZE];


void display_cmac_ports (const fpga_design_t *const design);
void cmac_snapshot_port_statistics (fpga_design_t *const design, const uint32_t port_num, cmac_port_statistics_t *const stats);
void cmac_read_port_statistics (cmac_port_statistics_t *const stats);
void cmac_display_port_statistics (const cmac_port_statistics_t *const stats);
void cmac_port_iterator_initialise (cmac_port_iterator_t *const iterator, fpga_designs_t *const designs,
                                    const uint32_t port_num_filter, const bool port_num_filter_specified);
fpga_design_t *cmac_port_iterator_next (cmac_port_iterator_t *const iterator, uint32_t *const port_num);
void cmac_reset_port (fpga_design_t *const design, const uint32_t port_num);


#endif /* CMAC_REGISTER_ACCESS_H_ */
