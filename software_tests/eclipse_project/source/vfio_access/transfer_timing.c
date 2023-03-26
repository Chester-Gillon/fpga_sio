/*
 * @file transfer_timing.c
 * @date 26 Mar 2023
 * @author Chester Gillon
 * @brief Provides an interface for measuring and reporting statistics on transfer timing
 */

#include "transfer_timing.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <time.h>


/**
 * @brief Return the current monotonic time in integer nanoseconds
 */
int64_t get_monotonic_time (void)
{
    struct timespec now;

    clock_gettime (CLOCK_MONOTONIC, &now);

    return (now.tv_sec * 1000000000LL) + now.tv_nsec;
}


/**
 * @brief Initialise transfer timing statistics to be empty
 * @param[out] timing The statistics to initialise
 * @param[in] transfer_type_name The name of the transfer type that is to be timed
 * @param[in] transfer_size_bytes The size of each transfer that is to be timed
 */
void initialise_transfer_timing (transfer_timing_t *const timing,
                                 const char *const transfer_type_name, const size_t transfer_size_bytes)
{
    memset (timing, 0, sizeof (*timing));
    timing->transfer_type_name = transfer_type_name;
    timing->transfer_size_bytes = transfer_size_bytes;
}


/*
 * @brief Called before starting a transfer to record the start time
 * @param[in/out] timing The transfer timing statistics to record the start time for
 */
void transfer_time_start (transfer_timing_t *const timing)
{
    timing->transfer_start_time_ns = get_monotonic_time ();
}


/**
 * @brief Called upon completing a transfer to update the transfer timing statistics
 * @param[in/out] timing The transfer timing statistics to update
 */
void transfer_time_stop (transfer_timing_t *const timing)
{
    const int64_t transfer_stop_time_ns = get_monotonic_time ();
    const int64_t transfer_time_ns = transfer_stop_time_ns - timing->transfer_start_time_ns;

    if (timing->num_transfers == 0)
    {
        timing->min_transfer_time_ns = transfer_time_ns;
        timing->max_transfer_time_ns = transfer_time_ns;
    }
    else
    {
        if (transfer_time_ns < timing->min_transfer_time_ns)
        {
            timing->min_transfer_time_ns = transfer_time_ns;
        }
        if (transfer_time_ns > timing->max_transfer_time_ns)
        {
            timing->max_transfer_time_ns = transfer_time_ns;
        }
    }

    timing->total_transfer_time_ns += transfer_time_ns;
    timing->num_transfers++;
    timing->transfer_start_time_ns = 0;
}


/**
 * @brief Display the transfer rate in floating point Mbytes per second
 * @param[in] timing The timing statistics to obtain the size of each transfer
 * @param[in] transfer_time_name Describes the transfer_time_ns
 * @param[in] transfer_time_ns The transfer time to display the rate for
 */
static void display_transfer_timing_rate (const transfer_timing_t *const timing,
                                         const char *const transfer_time_name, const int64_t transfer_time_ns)
{
    const double transfer_time_secs = (double) transfer_time_ns / 1E9;
    const double bytes_per_secs = (double) timing->transfer_size_bytes / transfer_time_secs;
    printf ("  %s = %0.6lf (Mbytes/sec)\n", transfer_time_name, bytes_per_secs / 1E6);
}


/**
 * @brief Display the statistics for a type of transfer
 * @param[in] timing The statistics to display
 */
void display_transfer_timing_statistics (const transfer_timing_t *const timing)
{
    printf ("%s timing for %" PRIu32 " transfers of %zu bytes:\n",
            timing->transfer_type_name, timing->num_transfers, timing->transfer_size_bytes);
    if ((timing->num_transfers > 0) && (timing->transfer_size_bytes > 0))
    {
        /* Max transfer time is min transfer rate and vice-versa */
        display_transfer_timing_rate (timing, " Min", timing->max_transfer_time_ns);
        display_transfer_timing_rate (timing, "Mean", timing->total_transfer_time_ns / timing->num_transfers);
        display_transfer_timing_rate (timing, " Max", timing->min_transfer_time_ns);
    }
}
