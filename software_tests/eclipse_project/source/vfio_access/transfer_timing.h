/*
 * @file transfer_timing.h
 * @date 26 Mar 2023
 * @author Chester Gillon
 * @brief Provides an interface for measuring and reporting statistics on transfer timing
 */

#ifndef TRANSFER_TIMING_H_
#define TRANSFER_TIMING_H_

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>


/* Used to collect statistics on transfer timing */
typedef struct
{
    /* Describes the type of transfer being timed */
    char transfer_type_name[PATH_MAX];
    /* The size in bytes of each transfer timed */
    size_t transfer_size_bytes;
    /* The number of transfers which have been timed */
    uint32_t num_transfers;
    /* The minimum and maximum transfer times (in nanoseconds) */
    int64_t min_transfer_time_ns;
    int64_t max_transfer_time_ns;
    /* The total time of all transfers (in nanoseconds) */
    int64_t total_transfer_time_ns;
    /* The time at which the transfer being timed started */
    int64_t transfer_start_time_ns;
} transfer_timing_t;


int64_t get_monotonic_time (void);
void initialise_transfer_timing (transfer_timing_t *const timing,
                                 const char *const transfer_type_name, const size_t transfer_size_bytes);
void transfer_time_start (transfer_timing_t *const timing);
void transfer_time_stop (transfer_timing_t *const timing);
void display_transfer_timing_statistics (const transfer_timing_t *const timing);


/**
 * @brief A 32-bit Linear congruential generator for creating a pseudo-random test pattern.
 * @details "Numerical Recipes" from https://en.wikipedia.org/wiki/Linear_congruential_generator
 * @param[in/out] seed the LCG value to advance
 */
static inline void linear_congruential_generator32 (uint32_t *const seed)
{
    *seed = (*seed * 1664525) + 1013904223;
}


/**
 * @brief A 32-bit Linear congruential generator for creating a pseudo-random test pattern.
 * @details "Numerical Recipes" from https://en.wikipedia.org/wiki/Linear_congruential_generator
 * @param[in/out] seed the LCG value to advance
 */
static inline void linear_congruential_generator64 (uint64_t *const seed)
{
    *seed = (*seed * 6364136223846793005) + 1442695040888963407;
}


#endif /* TRANSFER_TIMING_H_ */
