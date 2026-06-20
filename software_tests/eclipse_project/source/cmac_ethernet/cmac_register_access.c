/*
 * @file cmac_register_access.c
 * @date 25 Apr 2026
 * @author Chester Gillon
 * @brief Implements functions to access the CMAC Configuration registers, Status registers, and Statistics counters.
 */

#include "vfio_bitops.h"
#include "cmac_register_access.h"
#include "transfer_timing.h"
#include "cmac_axi4_lite_registers.h"

#include <string.h>
#include <stdio.h>
#include <time.h>


#define STRINGIFY_HELPER(X) #X
#define STRINGIFY(X) STRINGIFY_HELPER(X)

/* Define the register offsets and names for the statistics counters in one CMACC port.
 * CMAC_STAT_COUNTER_DEF is used to set the name to the same as that of the register offsets without the prefix and suffix. */
#define CMAC_STAT_COUNTER_DEF(name_param) [CMAC_STAT_##name_param] = \
    {.name = STRINGIFY(name_param), \
     .lsb_offset = STAT_##name_param##_LSB_OFFSET, \
     .msb_offset = STAT_##name_param##_MSB_OFFSET}
const cmac_statistics_counter_definition_t cmac_statistics_counter_definitions[CMAC_STAT_ARRAY_SIZE] =
{
    CMAC_STAT_COUNTER_DEF (CYCLE_COUNT),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_0),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_1),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_2),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_3),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_4),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_5),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_6),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_7),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_8),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_9),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_10),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_11),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_12),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_13),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_14),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_15),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_16),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_17),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_18),
    CMAC_STAT_COUNTER_DEF (RX_BIP_ERR_19),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_0),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_1),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_2),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_3),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_4),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_5),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_6),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_7),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_8),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_9),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_10),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_11),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_12),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_13),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_14),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_15),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_16),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_17),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_18),
    CMAC_STAT_COUNTER_DEF (RX_FRAMING_ERR_19),
    CMAC_STAT_COUNTER_DEF (RX_BAD_CODE),
    CMAC_STAT_COUNTER_DEF (TX_FRAME_ERROR),
    CMAC_STAT_COUNTER_DEF (TX_TOTAL_PACKETS),
    CMAC_STAT_COUNTER_DEF (TX_TOTAL_GOOD_PACKETS),
    CMAC_STAT_COUNTER_DEF (TX_TOTAL_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_TOTAL_GOOD_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_64_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_65_127_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_128_255_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_256_511_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_512_1023_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_1024_1518_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_1519_1522_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_1523_1548_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_1549_2047_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_2048_4095_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_4096_8191_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_8192_9215_BYTES),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_LARGE),
    CMAC_STAT_COUNTER_DEF (TX_PACKET_SMALL),
    CMAC_STAT_COUNTER_DEF (TX_BAD_FCS),
    CMAC_STAT_COUNTER_DEF (TX_UNICAST),
    CMAC_STAT_COUNTER_DEF (TX_MULTICAST),
    CMAC_STAT_COUNTER_DEF (TX_BROADCAST),
    CMAC_STAT_COUNTER_DEF (TX_VLAN),
    CMAC_STAT_COUNTER_DEF (TX_PAUSE),
    CMAC_STAT_COUNTER_DEF (TX_USER_PAUSE),
    CMAC_STAT_COUNTER_DEF (RX_TOTAL_PACKETS),
    CMAC_STAT_COUNTER_DEF (RX_TOTAL_GOOD_PACKETS),
    CMAC_STAT_COUNTER_DEF (RX_TOTAL_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_TOTAL_GOOD_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_64_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_65_127_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_128_255_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_256_511_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_512_1023_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_1024_1518_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_1519_1522_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_1523_1548_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_1549_2047_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_2048_4095_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_4096_8191_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_8192_9215_BYTES),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_LARGE),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_SMALL),
    CMAC_STAT_COUNTER_DEF (RX_UNDERSIZE),
    CMAC_STAT_COUNTER_DEF (RX_FRAGMENT),
    CMAC_STAT_COUNTER_DEF (RX_OVERSIZE),
    CMAC_STAT_COUNTER_DEF (RX_TOOLONG),
    CMAC_STAT_COUNTER_DEF (RX_JABBER),
    CMAC_STAT_COUNTER_DEF (RX_BAD_FCS),
    CMAC_STAT_COUNTER_DEF (RX_PACKET_BAD_FCS),
    CMAC_STAT_COUNTER_DEF (RX_STOMPED_FCS),
    CMAC_STAT_COUNTER_DEF (RX_UNICAST),
    CMAC_STAT_COUNTER_DEF (RX_MULTICAST),
    CMAC_STAT_COUNTER_DEF (RX_BROADCAST),
    CMAC_STAT_COUNTER_DEF (RX_VLAN),
    CMAC_STAT_COUNTER_DEF (RX_PAUSE),
    CMAC_STAT_COUNTER_DEF (RX_USER_PAUSE),
    CMAC_STAT_COUNTER_DEF (RX_INRANGEERR),
    CMAC_STAT_COUNTER_DEF (RX_TRUNCATED),
    CMAC_STAT_COUNTER_DEF (OTN_TX_JABBER),
    CMAC_STAT_COUNTER_DEF (OTN_TX_OVERSIZE),
    CMAC_STAT_COUNTER_DEF (OTN_TX_UNDERSIZE),
    CMAC_STAT_COUNTER_DEF (OTN_TX_TOOLONG),
    CMAC_STAT_COUNTER_DEF (OTN_TX_FRAGMENT),
    CMAC_STAT_COUNTER_DEF (OTN_TX_PACKET_BAD_FCS),
    CMAC_STAT_COUNTER_DEF (OTN_TX_STOMPED_FCS),
    CMAC_STAT_COUNTER_DEF (OTN_TX_BAD_CODE),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_CORRECTED_CW_INC),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_UNCORRECTED_CW_INC),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_ERR_COUNT0_INC),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_ERR_COUNT1_INC),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_ERR_COUNT2_INC),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_ERR_COUNT3_INC),
    CMAC_STAT_COUNTER_DEF (RX_RSFEC_CW_INC)
};


/* The monotonic time of the last sample tick for the statistics counters in each CMAC port */
static int64_t port_last_sample_tick_times_ns[MAX_VFIO_DEVICES][MAX_CMAC_PORTS_PER_DESIGN];


/**
 * @brief Display a CMAC status register
 * @details
 *   The status registers have some sticky bits which latch high or low after an error condition.
 *   The status register is read twice, and if the two reads are different that means there has been an error condition which
 *   has been cleared.
 * @param[in] port_def The CMAC port to display the status register for
 * @param[in] reg_offset The status register offset to display
 * @param[in] reg_name The name of the register being displayed
 */
static void cmac_display_status_register (const cmac_port_definition_t *const port_def, const uint32_t reg_offset,
                                          const char *const reg_name)
{
    const uint32_t initial_reg_value = read_reg32 (port_def->cmac_regs, reg_offset);
    const uint32_t subsequent_reg_value = read_reg32 (port_def->cmac_regs, reg_offset);

    if (initial_reg_value == subsequent_reg_value)
    {
        /* Both reads returned the value, so only need to display the value once */
        printf ("    %s: 0x%08x\n", reg_name, initial_reg_value);
    }
    else
    {
        /* An error condition has changed, so display both register values */
        printf ("    %s: 0x%08x -> 0x%08x (changed bits 0x%08x)\n",
                reg_name, initial_reg_value, subsequent_reg_value, initial_reg_value ^ subsequent_reg_value);
    }
}


/**
 * @brief Display information about the CMAC ports in an identified design
 * @param[in] design The identified design containing the CMAC ports
 */
void display_cmac_ports (const fpga_design_t *const design)
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
        const cmac_port_definition_t *const port_def = &design->cmac_ports[port_index];

        if (port_def->cmac_regs != NULL)
        {
            const uint32_t core_mode_reg = read_reg32 (port_def->cmac_regs, CORE_MODE_REG_OFFSET);
            const uint32_t core_mode = vfio_extract_field_u32 (core_mode_reg, CORE_MODE_REG_MASK);
            const uint32_t core_version_reg = read_reg32 (port_def->cmac_regs, CORE_VERSION_REG_OFFSET);
            const uint32_t core_version_minor = vfio_extract_field_u32 (core_version_reg, CORE_VERSION_REG_MINOR_MASK);
            const uint32_t core_version_major = vfio_extract_field_u32 (core_version_reg, CORE_VERSION_REG_MAJOR_MASK);

            snprintf (peripheral_name, sizeof (peripheral_name), "CMAC port %u", port_index);
            display_design_present_peripheral (design, peripheral_name, port_def->cmac_regs);
            printf ("    Core mode: %s\n", core_mode_names[core_mode]);
            printf ("    Core version: %u.%u\n", core_version_major, core_version_minor);

            /* Only display status registers for the configured features.
             * The status registers read as all ones if the feature isn't configured. */
            if (port_def->configured_features[CMAC_FEATURE_PACKET_TX])
            {
                cmac_display_status_register (port_def, STAT_TX_STATUS_REG_OFFSET, "TX Status");
            }
            if (port_def->configured_features[CMAC_FEATURE_PACKET_RX])
            {
                cmac_display_status_register (port_def, STAT_RX_STATUS_REG_OFFSET, "RX Status");
                cmac_display_status_register (port_def, STAT_RX_BLOCK_LOCK_REG_OFFSET, "RX Block Lock");
                cmac_display_status_register (port_def, STAT_RX_LANE_SYNC_REG_OFFSET, "RX Lane Sync");
                cmac_display_status_register (port_def, STAT_RX_LANE_SYNC_ERR_REG_OFFSET, "RX Lane Sync Error");
                cmac_display_status_register (port_def, STAT_RX_LANE_AM_ERR_REG_OFFSET, "RX Lane Alignment Marker Error");
                cmac_display_status_register (port_def, STAT_RX_LANE_AM_LEN_ERR_REG_OFFSET, "RX Lane Alignment Marker Length Error");
                cmac_display_status_register (port_def, STAT_RX_LANE_AM_REPEAT_ERR_REG_OFFSET, "RX Lane Alignment Marker Repeat Error");
                if (port_def->configured_features[CMAC_FEATURE_RS_FEC])
                {
                    cmac_display_status_register (port_def, STAT_RSFEC_STATUS_REG_OFFSET, "RSFEC Status");
                }
            }
        }
    }
}


/**
 * @brief Snapshot the statistic counters for one CMAC port
 * @details This uses the CMAC tick mechanism which snapshots the internal counters. The tick mechanism resets
 *          the internal counters to zero for the specific port. I.e.:
 *          a. Each call to this will reset the internal statistics counts for the next sampling interval.
 *          b. If multiple threads or processes call this function for the same port the counts may under-read.
 *
 *          Have split the API into cmac_snapshot_port_statistics() and cmac_read_port_statistics() functions to allow counters
 *          for multiple CMAC ports to be snapshot'ed as close together as possible, before later reading the values.
 * @param[in,out] design Contains the design with the CMAC
 * @param[in] Which CMAC port to snapshot the statistic counters for
 * @param[in,out] stats The statistics counters
 */
void cmac_snapshot_port_statistics (fpga_design_t *const design, const uint32_t port_num, cmac_port_statistics_t *const stats)
{
    stats->design = design;
    stats->port_num = port_num;

    /* Snapshot the statistics counters */
    stats->this_sample_tick_time_ns = get_monotonic_time ();
    write_reg32 (design->cmac_ports[port_num].cmac_regs, TICK_REG_OFFSET, TICK_REG_TICK_REG_MASK);
}


/**
 * @brief Read the statistics counters for one CMAC port
 * @details Reads the values of which were snapshot'ed by the previous call to cmac_snapshot_port_statistics()
 * @param[in,out] stats On return the statistics counters read from the port
 */
void cmac_read_port_statistics (cmac_port_statistics_t *const stats)
{
    const cmac_port_definition_t *const port_def = &stats->design->cmac_ports[stats->port_num];
    bool counter_defined;

    /* Store the counter values which have been snapshot'ed */
    for (uint32_t counter_index = 0; counter_index < CMAC_STAT_ARRAY_SIZE; counter_index++)
    {
        const cmac_statistics_counter_definition_t *const counter_def = &cmac_statistics_counter_definitions[counter_index];

        /* Determine if the counter is implemented for the CMAC port, based upon the configured port features.
         * Uses a string prefix match on the counter name to avoid having cmac_statistics_counter_definitions having to encode
         * the feature specific counters. */
        if (strncmp (counter_def->name, "RX_RSFEC", 8) == 0)
        {
            counter_defined = port_def->configured_features[CMAC_FEATURE_PACKET_RX] &&
                    port_def->configured_features[CMAC_FEATURE_RS_FEC];
        }
        else if (strncmp (counter_def->name, "OTN_TX", 6) == 0)
        {
            counter_defined = port_def->configured_features[CMAC_FEATURE_TX_OTN];
        }
        else if (strncmp (counter_def->name, "TX", 2) == 0)
        {
            counter_defined = port_def->configured_features[CMAC_FEATURE_PACKET_TX];
        }
        else if (strncmp (counter_def->name, "RX", 2) == 0)
        {
            counter_defined = port_def->configured_features[CMAC_FEATURE_PACKET_RX];
        }
        else
        {
            counter_defined = true;
        }

        if (counter_defined)
        {
            const uint32_t counter_lsb_register = read_reg32 (port_def->cmac_regs, counter_def->lsb_offset);
            const uint32_t counter_msb_register = read_reg32 (port_def->cmac_regs, counter_def->msb_offset);

            stats->counter_values[counter_index] = (((uint64_t) counter_msb_register & 0xffff) << 32) | counter_lsb_register;
        }
        else
        {
            /* Counter is not implemented for the port. Unimplemented counters read all ones, so need to suppress the read. */
            stats->counter_values[counter_index] = 0;
        }
    }

    /* Record the duration between samples on the same port */
    if (port_last_sample_tick_times_ns[stats->design->design_index][stats->port_num] != 0)
    {
        stats->sample_duration_ns =
                stats->this_sample_tick_time_ns - port_last_sample_tick_times_ns[stats->design->design_index][stats->port_num];
        stats->sample_duration_valid = true;
    }
    else
    {
        stats->sample_duration_ns = 0;
        stats->sample_duration_valid = false;
    }
    port_last_sample_tick_times_ns[stats->design->design_index][stats->port_num] = stats->this_sample_tick_time_ns;
}


/**
 * @brief Display the statistic counters for one CMAC port
 * @param[in] stats The statistic counters to display
 */
void cmac_display_port_statistics (const cmac_port_statistics_t *const stats)
{
    uint32_t counter_index;

    /* Find the maximum length of all statistic counter names, to format the output */
    int max_name_len = 0;
    for (counter_index = 0; counter_index < CMAC_STAT_ARRAY_SIZE; counter_index++)
    {
        const size_t name_len = strlen (cmac_statistics_counter_definitions[counter_index].name);

        if (name_len > max_name_len)
        {
            max_name_len = (int) name_len;
        }
    }

    /* Only display counters with non-zero values:
     * a. For a more compact display.
     * b. To ignore non-implemented counters for a port, for which cmac_read_port_statistics() stores a zero. */
    printf ("%s port %u statistics", fpga_design_names[stats->design->design_id], stats->port_num);
    if (stats->sample_duration_valid)
    {
        printf (" (over %.3f secs)", (double) stats->sample_duration_ns / 1E9);
    }
    printf (":\n");
    for (counter_index = 0; counter_index < CMAC_STAT_ARRAY_SIZE; counter_index++)
    {
        const uint64_t counter_value = stats->counter_values[counter_index];

        if (counter_value != 0)
        {
            printf ("  %*s: %15" PRIu64 "%s\n", -max_name_len, cmac_statistics_counter_definitions[counter_index].name,
                    counter_value, (counter_value == CMAC_STAT_SATURATED_VALUE) ? " (saturated)" : "");
        }
    }
}


/**
 * @brief Initialise an iterator for CMAC ports
 * @param[out] iterator The initialised iterator
 * @param[in] designs The designs to iterate over
 * @param[in] port_num_filter Optional port number filter
 * @param[in] port_num_filter_specified If true, the iterator will only return port number which are present and match port_num_filter
 */
void cmac_port_iterator_initialise (cmac_port_iterator_t *const iterator, fpga_designs_t *const designs,
                                    const uint32_t port_num_filter, const bool port_num_filter_specified)
{
    iterator->designs = designs;
    iterator->current_design_index = 0;
    iterator->current_port_index = 0;
    iterator->port_num_filter = port_num_filter;
    iterator->port_num_filter_specified = port_num_filter_specified;
}


/**
 * @brief Get the next CMAC port to processed by an iterator
 * @param[in,out] iterator The iterator to advance
 * @param[out] port_num The next port number to be processed
 * @return If non-NULL the design containing the next CMAC port to process.
 *         If NULL the iterator is complete.
 */
fpga_design_t *cmac_port_iterator_next (cmac_port_iterator_t *const iterator, uint32_t *const port_num)
{
    fpga_design_t *available_design = NULL;

    *port_num = 0;
    while ((available_design == NULL) && (iterator->current_design_index < iterator->designs->num_identified_designs))
    {
        if (iterator->designs->designs[iterator->current_design_index].num_cmac_ports > 0)
        {
            const bool port_num_wanted = (!iterator->port_num_filter_specified) ||
                    (iterator->port_num_filter_specified && (iterator->current_port_index == iterator->port_num_filter));

            if (port_num_wanted)
            {
                /* Have found a wanted CMAC port to return */
                available_design = &iterator->designs->designs[iterator->current_design_index];
                *port_num = iterator->current_port_index;
            }

            /* Advance the iterator to the next possible port / design */
            iterator->current_port_index++;
            if (iterator->current_port_index == iterator->designs->designs[iterator->current_design_index].num_cmac_ports)
            {
                iterator->current_port_index = 0;
                iterator->current_design_index++;
            }
        }
        else
        {
            /* Skip design with no CMAC */
            iterator->current_design_index++;
        }
    }

    return available_design;
}


/**
 * @brief Reset a CMAC port
 * @details This asserts all reset bits for the port, as is intended following a configuration change rather than recovering
 *          from a specific error.
 * @param[in,out] design Contains the design with the CMAC
 * @param[in] Which CMAC port to reset.
 */
void cmac_reset_port (fpga_design_t *const design, const uint32_t port_num)
{
    uint8_t *const port_regs = design->cmac_ports[port_num].cmac_regs;

    /* Apply reset for 100 microseconds, in the absence of any minimum reset duration in the CMAC documentation */
    const struct timespec reset_duration =
    {
        .tv_sec = 0,
        .tv_nsec = 100000
    };

    write_reg32 (port_regs, GT_RESET_REG_OFFSET, GT_RESET_REG_GT_RESET_ALL_MASK);
    write_reg32 (port_regs, RESET_REG_OFFSET,
            RESET_REG_USR_RX_SERDES_RESET_MASK | RESET_REG_USR_RX_RESET_MASK | RESET_REG_USR_TX_RESET_MASK);
    clock_nanosleep (CLOCK_MONOTONIC, 0, &reset_duration, NULL);
    write_reg32 (port_regs, GT_RESET_REG_OFFSET, 0);
    write_reg32 (port_regs, RESET_REG_OFFSET, 0);
}
