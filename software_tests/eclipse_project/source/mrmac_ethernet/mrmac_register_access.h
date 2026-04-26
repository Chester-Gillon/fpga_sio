/*
 * @file mrmac_register_access.h
 * @date 30 Mar 2026
 * @author Chester Gillon
 * @brief Provides functions to access the MRMAC Configuration registers, Status registers, and Statistics counters.
 */

#ifndef MRMAC_REGISTER_ACCESS_H_
#define MRMAC_REGISTER_ACCESS_H_

#include "mrmac_axi4_lite_registers.h"
#include "identify_pcie_fpga_design.h"


/* The statistics counters maintained by the MRMAC for one port.
 * This list was taken from port 0. Other ports may not have some of the counters implemented. */
typedef enum
{
    MRMAC_STAT_TX_CYCLE_COUNT,
    MRMAC_STAT_TX_FRAME_ERROR,
    MRMAC_STAT_TX_TOTAL_PACKETS,
    MRMAC_STAT_TX_TOTAL_GOOD_PACKETS,
    MRMAC_STAT_TX_TOTAL_BYTES,
    MRMAC_STAT_TX_TOTAL_GOOD_BYTES,
    MRMAC_STAT_TX_PACKET_64_BYTES,
    MRMAC_STAT_TX_PACKET_65_127_BYTES,
    MRMAC_STAT_TX_PACKET_128_255_BYTES,
    MRMAC_STAT_TX_PACKET_256_511_BYTES,
    MRMAC_STAT_TX_PACKET_512_1023_BYTES,
    MRMAC_STAT_TX_PACKET_1024_1518_BYTES,
    MRMAC_STAT_TX_PACKET_1519_1522_BYTES,
    MRMAC_STAT_TX_PACKET_1523_1548_BYTES,
    MRMAC_STAT_TX_PACKET_1549_2047_BYTES,
    MRMAC_STAT_TX_PACKET_2048_4095_BYTES,
    MRMAC_STAT_TX_PACKET_4096_8191_BYTES,
    MRMAC_STAT_TX_PACKET_8192_9215_BYTES,
    MRMAC_STAT_TX_PACKET_LARGE,
    MRMAC_STAT_TX_PACKET_SMALL,
    MRMAC_STAT_TX_BAD_FCS,
    MRMAC_STAT_TX_UNICAST,
    MRMAC_STAT_TX_MULTICAST,
    MRMAC_STAT_TX_BROADCAST,
    MRMAC_STAT_TX_VLAN,
    MRMAC_STAT_TX_PAUSE,
    MRMAC_STAT_TX_USER_PAUSE,
    MRMAC_STAT_TX_TSN_PREEMPTED_PKT,
    MRMAC_STAT_TX_TSN_FRAGMENT,
    MRMAC_STAT_TX_PCS_BAD_CODE,
    MRMAC_STAT_TX_CL82_49_CONVERT_ERR,
    MRMAC_STAT_TX_ECC_ERR0,
    MRMAC_STAT_TX_ECC_ERR1,

    MRMAC_STAT_RX_CYCLE_COUNT,
    MRMAC_STAT_RX_BIP_ERR_0,
    MRMAC_STAT_RX_BIP_ERR_1,
    MRMAC_STAT_RX_BIP_ERR_2,
    MRMAC_STAT_RX_BIP_ERR_3,
    MRMAC_STAT_RX_BIP_ERR_4,
    MRMAC_STAT_RX_BIP_ERR_5,
    MRMAC_STAT_RX_BIP_ERR_6,
    MRMAC_STAT_RX_BIP_ERR_7,
    MRMAC_STAT_RX_BIP_ERR_8,
    MRMAC_STAT_RX_BIP_ERR_9,
    MRMAC_STAT_RX_BIP_ERR_10,
    MRMAC_STAT_RX_BIP_ERR_11,
    MRMAC_STAT_RX_BIP_ERR_12,
    MRMAC_STAT_RX_BIP_ERR_13,
    MRMAC_STAT_RX_BIP_ERR_14,
    MRMAC_STAT_RX_BIP_ERR_15,
    MRMAC_STAT_RX_BIP_ERR_16,
    MRMAC_STAT_RX_BIP_ERR_17,
    MRMAC_STAT_RX_BIP_ERR_18,
    MRMAC_STAT_RX_BIP_ERR_19,
    MRMAC_STAT_RX_FRAMING_ERR_0,
    MRMAC_STAT_RX_FRAMING_ERR_1,
    MRMAC_STAT_RX_FRAMING_ERR_2,
    MRMAC_STAT_RX_FRAMING_ERR_3,
    MRMAC_STAT_RX_FRAMING_ERR_4,
    MRMAC_STAT_RX_FRAMING_ERR_5,
    MRMAC_STAT_RX_FRAMING_ERR_6,
    MRMAC_STAT_RX_FRAMING_ERR_7,
    MRMAC_STAT_RX_FRAMING_ERR_8,
    MRMAC_STAT_RX_FRAMING_ERR_9,
    MRMAC_STAT_RX_FRAMING_ERR_10,
    MRMAC_STAT_RX_FRAMING_ERR_11,
    MRMAC_STAT_RX_FRAMING_ERR_12,
    MRMAC_STAT_RX_FRAMING_ERR_13,
    MRMAC_STAT_RX_FRAMING_ERR_14,
    MRMAC_STAT_RX_FRAMING_ERR_15,
    MRMAC_STAT_RX_FRAMING_ERR_16,
    MRMAC_STAT_RX_FRAMING_ERR_17,
    MRMAC_STAT_RX_FRAMING_ERR_18,
    MRMAC_STAT_RX_FRAMING_ERR_19,
    MRMAC_STAT_RX_BAD_CODE,
    MRMAC_STAT_RX_PCS_BAD_CODE,
    MRMAC_STAT_RX_INVALID_START,
    MRMAC_STAT_RX_FEC_CW_0,
    MRMAC_STAT_RX_FEC_CW_1,
    MRMAC_STAT_RX_FEC_CW_2,
    MRMAC_STAT_RX_FEC_CW_3,
    MRMAC_STAT_RX_FEC_CORRECTED_CW_0,
    MRMAC_STAT_RX_FEC_CORRECTED_CW_1,
    MRMAC_STAT_RX_FEC_CORRECTED_CW_2,
    MRMAC_STAT_RX_FEC_CORRECTED_CW_3,
    MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0,
    MRMAC_STAT_RX_FEC_UNCORRECTED_CW_1,
    MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2,
    MRMAC_STAT_RX_FEC_UNCORRECTED_CW_3,
    MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0,
    MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_1,
    MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_2,
    MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_3,
    MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0,
    MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_1,
    MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_2,
    MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_3,
    MRMAC_STAT_RX_FEC_ERR_COUNT_0,
    MRMAC_STAT_RX_FEC_ERR_COUNT_1,
    MRMAC_STAT_RX_FEC_ERR_COUNT_2,
    MRMAC_STAT_RX_FEC_ERR_COUNT_3,
    MRMAC_STAT_RX_TOTAL_PACKETS,
    MRMAC_STAT_RX_TOTAL_GOOD_PACKETS,
    MRMAC_STAT_RX_TOTAL_BYTES,
    MRMAC_STAT_RX_TOTAL_GOOD_BYTES,
    MRMAC_STAT_RX_PACKET_64_BYTES,
    MRMAC_STAT_RX_PACKET_65_127_BYTES,
    MRMAC_STAT_RX_PACKET_128_255_BYTES,
    MRMAC_STAT_RX_PACKET_256_511_BYTES,
    MRMAC_STAT_RX_PACKET_512_1023_BYTES,
    MRMAC_STAT_RX_PACKET_1024_1518_BYTES,
    MRMAC_STAT_RX_PACKET_1519_1522_BYTES,
    MRMAC_STAT_RX_PACKET_1523_1548_BYTES,
    MRMAC_STAT_RX_PACKET_1549_2047_BYTES,
    MRMAC_STAT_RX_PACKET_2048_4095_BYTES,
    MRMAC_STAT_RX_PACKET_4096_8191_BYTES,
    MRMAC_STAT_RX_PACKET_8192_9215_BYTES,
    MRMAC_STAT_RX_PACKET_LARGE,
    MRMAC_STAT_RX_PACKET_SMALL,
    MRMAC_STAT_RX_UNDERSIZE,
    MRMAC_STAT_RX_FRAGMENT,
    MRMAC_STAT_RX_OVERSIZE,
    MRMAC_STAT_RX_TOOLONG,
    MRMAC_STAT_RX_JABBER,
    MRMAC_STAT_RX_BAD_FCS,
    MRMAC_STAT_RX_PACKET_BAD_FCS,
    MRMAC_STAT_RX_STOMPED_FCS,
    MRMAC_STAT_RX_UNICAST,
    MRMAC_STAT_RX_MULTICAST,
    MRMAC_STAT_RX_BROADCAST,
    MRMAC_STAT_RX_VLAN,
    MRMAC_STAT_RX_PAUSE,
    MRMAC_STAT_RX_USER_PAUSE,
    MRMAC_STAT_RX_INRANGEERR,
    MRMAC_STAT_RX_TRUNCATED,
    MRMAC_STAT_RX_TEST_PATTERN_MISMATCH,
    MRMAC_STAT_RX_CL49_82_CONVERT_ERR,
    MRMAC_STAT_RX_TSN_PREEMPTED_PKT,
    MRMAC_STAT_RX_TSN_FRAGMENT,
    MRMAC_STAT_RX_ECC_ERR0,
    MRMAC_STAT_RX_ECC_ERR1,

    MRMAC_STAT_ARRAY_SIZE
} mrmac_statistic_counters_t;


/* Defines the value at which the MRMAC statistics counters saturate, based upon then having 48 bits.
 * The "Statistics Monitoring" section in PG314 says the counters saturate when full.
 */
#define MRMAC_STAT_NUM_BITS 48u
#define MRMAC_STAT_SATURATED_VALUE ((1ULL << MRMAC_STAT_NUM_BITS) - 1ULL)


/* Defines one statistic counter */
typedef struct
{
    /* Per port offset of the LSB counter register, from the base of the MRMAC registers.
     * If an offset for a port is zero, means the statistics counter is not implemented for the port. */
    uint32_t lsb_offsets[NUM_MRMAC_PORTS];
    /* The display name used for the statistics counter */
    const char *name;
} mrmac_statistics_counter_definition_t;


/* The statistics for one MRMAC port */
typedef struct
{
    /* Which design contains the MRMAC */
    fpga_design_t *design;
    /* Which MRMAC port the statistics are for */
    uint32_t port_num;
    /* The duration is nanoseconds over which the statistics are for.
     * Updated according to the interval between calls to mrmac_snapshot_port_statistics() for each port.
     * This is based upon the time which which the tick mechanism is used to snapshot the counter values. */
    int64_t sample_duration_ns;
    /* Set when sample_duration_ns is valid, which will be after the 2nd and subsequent call to mrmac_snapshot_port_statistics()
     * for a given port. */
    bool sample_duration_valid;
    /* The counter values read from the MRMAC statistic registers. */
    uint64_t counter_values[MRMAC_STAT_ARRAY_SIZE];
    /* The duration in nanoseconds waiting for the statistics counters to be ready after a tick.
     * Maintained for investigating how long takes, since PG314 doesn't indicate how long should take. */
    int64_t ready_duration_ns;
    /* The time at which the statistics were sampled by mrmac_snapshot_port_statistics() */
    int64_t this_sample_tick_time_ns;
} mrmac_port_statistics_t;


/* Specifies an iterator for operating on MRAMC ports.
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
} mrmac_port_iterator_t;


extern const char *const mrmac_port_data_rate_names[];
extern const uint32_t mrmac_num_port_data_rate_names;
extern const char *const mrmac_axi4_stream_mode_names[][MRMAC_CTL_DATA_RATE_ARRAY_SIZE];
extern const uint32_t mrmac_num_axi4_stream_mode_names;
extern const char *const mrmac_gt_quad_operating_mode_names[][MRMAC_CTL_DATA_RATE_ARRAY_SIZE];
extern const uint32_t mrmac_num_gt_quad_operating_mode_names;
extern const char *const mrmac_fec_operating_mode_names[][MRMAC_CTL_FEC_MODE_ARRAY_SIZE];
extern const uint32_t mrmac_num_fec_operating_mode_names;
extern const mrmac_statistics_counter_definition_t mrmac_statistics_counter_definitions[MRMAC_STAT_ARRAY_SIZE];


void display_mrmac_ports (const fpga_design_t *const design);
void mrmac_port_iterator_initialise (mrmac_port_iterator_t *const iterator, fpga_designs_t *const designs,
                                     const uint32_t port_num_filter, const bool port_num_filter_specified);
fpga_design_t *mrmac_port_iterator_next (mrmac_port_iterator_t *const iterator, uint32_t *const port_num);
void mrmac_reset_port (fpga_design_t *const design, const uint32_t port_num);
void mrmac_snapshot_port_statistics (fpga_design_t *const design, const uint32_t port_num, mrmac_port_statistics_t *const stats);
void mrmac_read_port_statistics (mrmac_port_statistics_t *const stats);
void mrmac_display_port_statistics (const mrmac_port_statistics_t *const stats);

#endif /* MRMAC_REGISTER_ACCESS_H_ */
