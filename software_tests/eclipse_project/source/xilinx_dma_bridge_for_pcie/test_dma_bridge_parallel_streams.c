/*
 * @file test_dma_bridge_parallel_streams.c
 * @date 4 Feb 2024
 * @author Chester Gillon
 * @brief A program to test perform tests on a Xilinx "DMA/Bridge Subsystem for PCI Express" with parallel streams
 * @details
 *  Only tests designs with AXI streams which are looped back inside the FPGA. It attempts to perform tests in parallel
 *  on all AXI streams present, to try and generate maximum PCIe throughput.
 *
 *  Compared to the test_dma_bridge program:
 *  1. It doesn't validate the data contents of data received from the stream as the test is running, since is trying to maximise
 *     throughput.
 *
 *     The stream transmit data is initialised to a fixed test pattern at initialisation, which is only checked once the test
 *     stopped the stream transfers at the end of the test.
 *  2. Performs transfers continuously, until requested to stop.
 *  3. Forces the stream transmit and receive to use the same transfer sizes, to simplify the code.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <getopt.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>


/* Use a single fixed transfer timeout, to stop the test from hanging */
#define TRANSFER_TIMEOUT_SECS 10


/* Used to size arrays of stream pairs which may be tested in parallel */
#define MAX_STREAM_PAIRS (MAX_VFIO_DEVICES * X2X_MAX_CHANNELS)


/* Command line argument which sets the VFIO buffer allocation type */
static vfio_buffer_allocation_type_t arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;


/* Command line argument which specifies the maximum number of combinations of different H2C and C2H channels tested */
static uint32_t arg_max_channel_combinations = X2X_MAX_CHANNELS * X2X_MAX_CHANNELS;


/* Command line arguments which specify the size of the mapping for the host buffer when performing AXI stream transfers */
static size_t arg_stream_mapping_size = 0x40000000;


/* Command line arguments which specify the number of descriptors when performing AXI stream transfers */
static uint32_t arg_stream_num_descriptors = 64;


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"device", required_argument, NULL, 0},
    {"buffer_allocation", required_argument, NULL, 0},
    {"max_channel_combinations", required_argument, NULL, 0},
    {"stream_mapping_size", required_argument, NULL, 0},
    {"stream_num_descriptors", required_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};


/* Set true in a signal handler when Ctrl-C is used to request a running test stops */
static volatile bool test_stop_requested;


/* Used to maintain statistics for the throughout on one pair of looped back AXI streams */
typedef struct
{
    /* Monotonic time for start of the statistics collection interval*/
    int64_t collection_interval_start_time;
    /* Monotonic time at which the most recent C2H transfer in the statistics collection interval was completed */
    int64_t time_last_transfer_c2h_completed;
    /* The number of completed transfers in the statistics collection interval */
    uint32_t num_completed_transfers;
} stream_pair_throughput_statistics_t;


/* Defines the context to test one pair of looped back AXI streams.
 * The mappings are separate for each context to simplify the software.
 * Sharing mappings between contexts could potentially reduce the number of page translations needed by the IOMMU,
 * but without testing not sure if that would increase performance. */
typedef struct
{
    /* The design containing the DMA bridge to test */
    fpga_design_t *design;
    /* The device containing the DMA bridge to test */
    vfio_device_t *vfio_device;
    /* Which channel to use for H2C transfers */
    uint32_t h2c_channel_id;
    /* Which channel to use for C2H transfers */
    uint32_t c2h_channel_id;
    /* Read/write mapping for the descriptors */
    vfio_dma_mapping_t descriptors_mapping;
    /* Read mapping used by device */
    vfio_dma_mapping_t h2c_data_mapping;
    /* Write mapping used by device */
    vfio_dma_mapping_t c2h_data_mapping;
    /* Used to perform transfers in both directions of the looped back stream */
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    /* The expected receive test pattern at the start of the c2h_data_mapping */
    uint32_t rx_test_pattern;
    /* Array sizes for the number of descriptors. Each index gives the monotonic time at which the C2H transfer was completed.
     * Used to update throughput statistics. */
    int64_t *c2h_completed_times;
    /* Index for the last descriptor to have completed, to read from c2h_completed_times when re-reseting interval_statistics for the
     * next reporting interval. */
    uint32_t last_completed_descriptor_index;
    /* The overall throughput statistics for the test */
    stream_pair_throughput_statistics_t overall_statistics;
    /* The throughput statistics for the current reporting interval */
    stream_pair_throughput_statistics_t interval_statistics;
} stream_test_context_t;


/* Contains the overall context for all the pairs of streams tested in parallel */
typedef struct
{
    /* The number of pairs of streams tested */
    uint32_t num_stream_pairs;
    /* The array of stream pairs to test in parallel.
     * Valid indices are in the range [0 .. num_stream_pairs-1] */
    stream_test_context_t stream_pairs[MAX_STREAM_PAIRS];
    /* The test operates with the stream transfers set to use fixed size buffers, so doesn't need to modify the
     * descriptors when the descriptors are started. */
    uint32_t num_descriptors;
    size_t bytes_per_buffer;
    /* The number of words in each data mapping, which defines the length of the test pattern */
    size_t data_mapping_size_words;
    /* Overall success for the test. Set to false any an error on any test stream pair, which stops the test. */
    bool overall_success;
} stream_test_contexts_t;


/* Contains the statistics for all tested streams for one reporting interval of the test */
typedef struct
{
    /* The throughput statistics for the current reporting interval for each stream */
    stream_pair_throughput_statistics_t stream_pairs[MAX_STREAM_PAIRS];
    /* Set true in the final statistics before the parallel_streams_test_thread() exits */
    bool final_statistics;
} stream_test_statistics_t;


/* test_statistics contains the statistics from the most recent completed test interval.
 * It is written by the parallel_streams_test_thread, and read by the main thread to report the test progress.
 *
 * The semaphores control the access by:
 * a. The free semaphore is initialised to 1, and the populated semaphore to 0.
 * b. The main thread blocks in sem_wait (test_statistics_populated) waiting for results.
 * c. At the end of a test interval the transmit_receive_thread:
 *    - sem_wait (test_statistics_free) which should not block unless the main thread isn't keeping up with reporting
 *      the test progress.
 *    - Stores the results for the completed test interval in test_statistics
 *    - sem_post (test_statistics_populated) to wake up the main thread.
 * d. When the main thread is woken up from sem_wait(test_statistics_populated):
 *    - Reports the contents of test_statistics
 *    - sem_post (test_statistics_free) to indicate has processed test_statistics
 * e. The sequence starts again from b.
 */
static stream_test_statistics_t test_statistics;
static sem_t test_statistics_free;
static sem_t test_statistics_populated;


/**
 * @brief Signal handler to request a running test stops
 * @param[in] sig Not used
 */
static void stop_test_handler (const int sig)
{
    test_stop_requested = true;
}


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("  test_dma_bridge_parallel_streams <options>\n");
    printf ("   Test Xilinx DMA/Bridge Subsystem for PCI Express with parallel streams\n");
    printf ("\n");
    printf ("--device <domain>:<bus>:<dev>.<func>\n");
    printf ("  only open using VFIO specific PCI devices in the event that there is one than\n");
    printf ("  one PCI device which matches the identity filters.\n");
    printf ("  May be used more than once.\n");
    printf ("--buffer_allocation heap|shared_memory|huge_pages\n");
    printf ("  Selects the VFIO buffer allocation type\n");
    printf ("--max_channel_combinations <num>\n");
    printf ("  When a DMA bridge has more than 1 channel, limits the maximum number of\n");
    printf ("  different H2C and C2H channels used during testing\n");
    printf ("--stream_mapping_size <size_bytes>\n");
    printf ("  Specifies the size of the mapping for the host buffer when performing AXI\n");
    printf ("  stream transfers.\n");
    printf ("--stream_num_descriptors <num_descriptors>\n");
    printf ("  Specifies the number of descriptors when performing AXI stream transfers.\n");

    exit (EXIT_FAILURE);
}


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    int opt_status;
    char junk;

    do
    {
        int option_index = 0;

        opt_status = getopt_long (argc, argv, "", command_line_options, &option_index);
        if (opt_status == '?')
        {
            display_usage ();
        }
        else if (opt_status >= 0)
        {
            const struct option *const optdef = &command_line_options[option_index];

            if (optdef->flag != NULL)
            {
                /* Argument just sets a flag */
            }
            else if (strcmp (optdef->name, "device") == 0)
            {
                vfio_add_pci_device_location_filter (optarg);
            }
            else if (strcmp (optdef->name, "buffer_allocation") == 0)
            {
                if (strcmp (optarg, "heap") == 0)
                {
                    arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;
                }
                else if (strcmp (optarg, "shared_memory") == 0)
                {
                    arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_SHARED_MEMORY;
                }
                else if (strcmp (optarg, "huge_pages") == 0)
                {
                    arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HUGE_PAGES;
                }
                else
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "max_channel_combinations") == 0)
            {
                if (sscanf (optarg, "%u%c", &arg_max_channel_combinations, &junk) != 1)
                {
                    fprintf (stderr, "Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "stream_mapping_size") == 0)
            {
                if ((sscanf (optarg, "%zi%c", &arg_stream_mapping_size, &junk) != 1) ||
                    (arg_stream_mapping_size < sizeof (uint32_t)))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
                if ((arg_stream_mapping_size % sizeof (uint32_t)) != 0)
                {
                    printf ("stream_mapping_size not a multiple of words\n");
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "stream_num_descriptors") == 0)
            {
                if ((sscanf (optarg, "%i%c", &arg_stream_num_descriptors, &junk) != 1) ||
                    (arg_stream_num_descriptors == 0) || (arg_stream_num_descriptors > X2X_SGDMA_MAX_DESCRIPTOR_CREDITS))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else
            {
                /* This is a program error, and shouldn't be triggered by the command line options */
                fprintf (stderr, "Unexpected argument definition %s\n", optdef->name);
                exit (EXIT_FAILURE);
            }
        }
    } while (opt_status != -1);
}


/**
 * @brief Perform the initialisation for all streams which are to be tested in parallel
 * @param[in/out] context The test context to initialise. overall_success will be false if the initialisation fails.
 */
static void initialise_parallel_streams (stream_test_contexts_t *const context)
{
    uint32_t tx_test_pattern = 0;
    size_t word_index;

    context->overall_success = true;
    for (uint32_t pair_index = 0; context->overall_success && (pair_index < context->num_stream_pairs); pair_index++)
    {
        stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];

        /* Populate the transfer configurations to be used, selecting use of fixed size buffers */
        const x2x_transfer_configuration_t h2c_transfer_configuration =
        {
            .dma_bridge_memory_size_bytes = stream_pair->design->dma_bridge_memory_size_bytes,
            .min_size_alignment = 1, /* The host memory is byte addressable */
            .num_descriptors = context->num_descriptors,
            .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
            .channel_id = stream_pair->h2c_channel_id,
            .bytes_per_buffer = context->bytes_per_buffer,
            .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
            .card_buffer_start_offset = 0, /* Not used for AXI stream */
            .c2h_stream_continuous = false,
            .timeout_seconds = TRANSFER_TIMEOUT_SECS,
            .vfio_device = stream_pair->vfio_device,
            .bar_index = stream_pair->design->dma_bridge_bar,
            .descriptors_mapping = &stream_pair->descriptors_mapping,
            .data_mapping = &stream_pair->h2c_data_mapping,
            .overall_success = &context->overall_success
        };

        const x2x_transfer_configuration_t c2h_transfer_configuration =
        {
            .dma_bridge_memory_size_bytes = stream_pair->design->dma_bridge_memory_size_bytes,
            .min_size_alignment = 1, /* The host memory is byte addressable */
            .num_descriptors = context->num_descriptors,
            .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
            .channel_id = stream_pair->c2h_channel_id,
            .bytes_per_buffer = context->bytes_per_buffer,
            .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
            .card_buffer_start_offset = 0, /* Not used for AXI stream */
            .c2h_stream_continuous = false,
            .timeout_seconds = TRANSFER_TIMEOUT_SECS,
            .vfio_device = stream_pair->vfio_device,
            .bar_index = stream_pair->design->dma_bridge_bar,
            .descriptors_mapping = &stream_pair->descriptors_mapping,
            .data_mapping = &stream_pair->c2h_data_mapping,
            .overall_success = &context->overall_success
        };

        /* Create read/write mapping for DMA descriptors */
        const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
                x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
        allocate_vfio_dma_mapping (stream_pair->vfio_device, &stream_pair->descriptors_mapping, descriptors_allocation_size,
                VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

        /* Read mapping used by device, for the entire card memory */
        allocate_vfio_dma_mapping (stream_pair->vfio_device, &stream_pair->h2c_data_mapping, arg_stream_mapping_size,
                VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

        /* Write mapping used by device, for the entire card memory */
        allocate_vfio_dma_mapping (stream_pair->vfio_device, &stream_pair->c2h_data_mapping, arg_stream_mapping_size,
                VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

        context->overall_success = (stream_pair->descriptors_mapping.buffer.vaddr != NULL) &&
                                   (stream_pair->h2c_data_mapping.buffer.vaddr    != NULL) &&
                                   (stream_pair->c2h_data_mapping.buffer.vaddr    != NULL);
        if (context->overall_success)
        {
            /* Initialise the transfers */
            x2x_initialise_transfer_context (&stream_pair->h2c_transfer, &h2c_transfer_configuration);
            x2x_initialise_transfer_context (&stream_pair->c2h_transfer, &c2h_transfer_configuration);
        }

        stream_pair->c2h_completed_times = calloc (context->num_descriptors, sizeof (stream_pair->c2h_completed_times[0]));

        if (context->overall_success)
        {
            uint32_t *const tx_words = stream_pair->h2c_data_mapping.buffer.vaddr;

            /* Populate the transmit test pattern.
             * The receive buffer is left at the zero filled value set by allocate_vfio_dma_mapping() and so won't
             * match the expected pattern unless the receive is successful. */
            stream_pair->rx_test_pattern = tx_test_pattern;
            for (word_index = 0; word_index < context->data_mapping_size_words; word_index++)
            {
                tx_words[word_index] = tx_test_pattern;
                linear_congruential_generator (&tx_test_pattern);
            }
        }
    }
}


/**
 * @brief Publish and then reset statistics for the next test interval
 * @param[in/out] context The context for the test.
 * @param[in] final_statistics Indicates if being called to publish the final statistics, before the test thread exits
 */
static void publish_statistics (stream_test_contexts_t *const context, const bool final_statistics)
{
    int rc;

    rc = sem_wait (&test_statistics_free);
    X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);

    test_statistics.final_statistics = final_statistics;
    for (uint32_t pair_index = 0; context->overall_success && (pair_index < context->num_stream_pairs); pair_index++)
    {
        stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];

        test_statistics.stream_pairs[pair_index] = stream_pair->interval_statistics;
        stream_pair->interval_statistics.num_completed_transfers = 0;

        /* Set the start time for the next collection interval to be when the last transfer completed for the reported
         * interval. This makes the timespan used to report the throughput rate a multiple of a whole number of transfers,
         * so that the reported throughput in Mbytes/sec should not jitter when the time to complete one transfer buffer
         * isn't a multiple of the statistics reporting interval. */
        stream_pair->interval_statistics.collection_interval_start_time =
                stream_pair->c2h_completed_times[stream_pair->last_completed_descriptor_index];
    }

    rc = sem_post (&test_statistics_populated);
    X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);
}


/**
 * @brief The entry point for thread which tests streams in parallel
 * @details
 *   Attempts to re-start transfers on all streams in parallel as quickly as possible, to maximum throughput.
 *   Exits when either a failure occurs on any stream, or the test has been requested to stop.
 *   Generates throughput statistics at regular intervals.
 * @param[in/out] arg The context for the test
 * @return Not used
 */
static void *parallel_streams_test_thread (void *const arg)
{
    stream_test_contexts_t *const context = arg;
    uint32_t pair_index;
    uint32_t descriptor_index;
    void *h2c_buffer;
    void *c2h_buffer;
    uint32_t num_idle_stream_pairs;
    bool test_stopping;
    int64_t now;
    bool final_statistics;

    const int64_t nsecs_per_sec = 1000000000;
    const int64_t reporting_interval_ns = 10 * nsecs_per_sec;

    int64_t next_report_time = get_monotonic_time () + reporting_interval_ns;

    /* Start all C2H transfers */
    for (pair_index = 0; context->overall_success && (pair_index < context->num_stream_pairs); pair_index++)
    {
        stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];

        for (descriptor_index = 0; context->overall_success && (descriptor_index < context->num_descriptors); descriptor_index++)
        {
            x2x_start_next_c2h_buffer (&stream_pair->c2h_transfer);
        }
    }

    /* Start all H2C transfers */
    for (descriptor_index = 0; context->overall_success && (descriptor_index < context->num_descriptors); descriptor_index++)
    {
        for (pair_index = 0; context->overall_success && (pair_index < context->num_stream_pairs); pair_index++)
        {
            stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];

            if (descriptor_index == 0)
            {
                stream_pair->overall_statistics.collection_interval_start_time = get_monotonic_time ();
            }
            h2c_buffer = x2x_get_next_h2c_buffer (&stream_pair->h2c_transfer);
            X2X_ASSERT (&stream_pair->h2c_transfer, h2c_buffer != NULL);
            x2x_start_populated_descriptors (&stream_pair->h2c_transfer);
        }
    }

    /* Initialise the throughput statistics (collection_interval_start_time set above) */
    for (pair_index = 0; context->overall_success && (pair_index < context->num_stream_pairs); pair_index++)
    {
        stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];

        stream_pair->last_completed_descriptor_index = context->num_descriptors - 1;
        stream_pair->overall_statistics.time_last_transfer_c2h_completed =
                stream_pair->overall_statistics.collection_interval_start_time;
        stream_pair->overall_statistics.num_completed_transfers = 0;
        stream_pair->interval_statistics = stream_pair->overall_statistics;
    }

    /* Run the test until either:
     * a. A failure occurs (DMA timeout) on any stream pair.
     * b. A test stop has been requested, and all previously queued transfers have completed. */
    num_idle_stream_pairs = 0;
    test_stopping = false;
    while (context->overall_success && (num_idle_stream_pairs < context->num_stream_pairs))
    {
        /* Sample a request to stop the test */
        if (test_stop_requested)
        {
            test_stopping = true;
        }

        num_idle_stream_pairs = 0;
        for (pair_index = 0; context->overall_success && (pair_index < context->num_stream_pairs); pair_index++)
        {
            stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];

            /* Poll for completion of C2H transfer, updating the throughput statistics upon completion.
             * Re-starts the transfer, unless the test has been requested to stop. */
            c2h_buffer = x2x_poll_completed_transfer (&stream_pair->c2h_transfer, NULL, NULL);
            if (c2h_buffer != NULL)
            {
                now = get_monotonic_time ();
                stream_pair->overall_statistics.time_last_transfer_c2h_completed = now;
                stream_pair->overall_statistics.num_completed_transfers++;
                stream_pair->interval_statistics.time_last_transfer_c2h_completed = now;
                stream_pair->interval_statistics.num_completed_transfers++;

                if (!test_stopping)
                {
                    x2x_start_next_c2h_buffer (&stream_pair->c2h_transfer);
                }
            }

            /* Poll for completion of H2C transfer.
             * Re-starts the transfer, unless the test has been requested to stop. */
            h2c_buffer = x2x_poll_completed_transfer (&stream_pair->h2c_transfer, NULL, NULL);
            if ((h2c_buffer != NULL) && !test_stopping)
            {
                now = get_monotonic_time ();
                h2c_buffer = x2x_get_next_h2c_buffer (&stream_pair->h2c_transfer);
                X2X_ASSERT (&stream_pair->h2c_transfer, h2c_buffer != NULL);
                x2x_start_populated_descriptors (&stream_pair->h2c_transfer);
                stream_pair->last_completed_descriptor_index =
                        (stream_pair->last_completed_descriptor_index + 1) % context->num_descriptors;
                stream_pair->c2h_completed_times[stream_pair->last_completed_descriptor_index] = now;
            }

            /* Once the test has been requested to stop, monitor when the transfers have become idle meaning
             * all outstanding transfers have completed */
            if (test_stopping &&
                (stream_pair->h2c_transfer.num_in_use_descriptors == 0) &&
                (stream_pair->c2h_transfer.num_in_use_descriptors == 0))
            {
                num_idle_stream_pairs++;
            }
        }

        if (get_monotonic_time () >= next_report_time)
        {
            final_statistics = false;
            publish_statistics (context, final_statistics);
            next_report_time += reporting_interval_ns;
        }
    }

    final_statistics = true;
    publish_statistics (context, final_statistics);

    return NULL;
}


/**
 * @brief If a transfer failed, report an error to the console
 * @param[in] context The transfer context to check for errors.
 */
static void report_if_transfer_failed (const x2x_transfer_context_t *const context)
{
    if (context->failed)
    {
        printf ("  %s %s channel %u failure : %s%s\n",
                context->configuration.vfio_device->device_name,
                (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ? "H2C" : "C2H",
                context->configuration.channel_id,
                context->error_message,
                context->timeout_awaiting_idle_at_finalisation ? " (+timeout waiting for idle at finalisation)" : "");
    }
}


/**
 * @brief Release the resources for all streams tested in parallel
 * @param[in/out] context The test context to release the resources for.
 */
static void finalise_parallel_streams (stream_test_contexts_t *const context)
{
    size_t word_index;
    bool data_valid;
    uint32_t expected_word;

    for (uint32_t pair_index = 0; pair_index < context->num_stream_pairs; pair_index++)
    {
        stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];
        const uint32_t num_buffers_to_verify = (stream_pair->overall_statistics.num_completed_transfers < context->num_descriptors)
                ? stream_pair->overall_statistics.num_completed_transfers : context->num_descriptors;
        const size_t num_words_to_verify = (num_buffers_to_verify * context->bytes_per_buffer) / sizeof (uint32_t);

        /* Verify the test pattern in the receive buffer.
         * This only verifies the final contents, as the pattern in the transmit buffer is not modified during the test. */
        if (num_words_to_verify > 0)
        {
            const uint32_t *const rx_words = stream_pair->c2h_data_mapping.buffer.vaddr;

            data_valid = true;
            expected_word = stream_pair->rx_test_pattern;
            for (word_index = 0; data_valid && (word_index < num_words_to_verify); word_index++)
            {
                if (expected_word != rx_words[word_index])
                {
                    x2x_record_failure (&stream_pair->c2h_transfer, "word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32,
                            word_index, rx_words[word_index], expected_word);
                    data_valid = false;
                }
                linear_congruential_generator (&expected_word);
            }

            if (data_valid)
            {
                printf ("%s %u -> %u Test pattern verified in %zu words\n",
                        stream_pair->vfio_device->device_name, stream_pair->h2c_channel_id, stream_pair->c2h_channel_id,
                        num_words_to_verify);
            }
        }

        /* Finalise the transfer contexts if the initialisation completed without error */
        if (stream_pair->h2c_transfer.completed_descriptor_count != NULL)
        {
            x2x_finalise_transfer_context (&stream_pair->h2c_transfer);
        }
        if (stream_pair->c2h_transfer.completed_descriptor_count != NULL)
        {
            x2x_finalise_transfer_context (&stream_pair->c2h_transfer);
        }

        report_if_transfer_failed (&stream_pair->h2c_transfer);
        report_if_transfer_failed (&stream_pair->c2h_transfer);

        free (stream_pair->c2h_completed_times);
        free_vfio_dma_mapping (&stream_pair->c2h_data_mapping);
        free_vfio_dma_mapping (&stream_pair->h2c_data_mapping);
        free_vfio_dma_mapping (&stream_pair->descriptors_mapping);
    }
}


/**
 * @brief Display the statistics for one pair of tested streams
 * @param[in] context The overall context
 * @param[in] pair_index The pair of tested streams the statistics are for.
 * @param[in] statistics The statistics to display, either for one interval or the overall test
 */
static void display_stream_pair_statistics (const stream_test_contexts_t *const context,
                                            const uint32_t pair_index,
                                            const stream_pair_throughput_statistics_t *const statistics)
{
    const stream_test_context_t *const stream_pair = &context->stream_pairs[pair_index];
    if (statistics->num_completed_transfers > 0)
    {
        const double interval_secs =
                ((double) (statistics->time_last_transfer_c2h_completed - statistics->collection_interval_start_time)) / 1E9;
        const size_t bytes_transferred = statistics->num_completed_transfers * context->bytes_per_buffer;
        const double mbytes_per_sec = (((double) bytes_transferred) / 1E6) / interval_secs;

        printf ("  %s %u -> %u %.3f Mbytes/sec (%zu bytes in %.06f secs)\n",
                stream_pair->vfio_device->device_name, stream_pair->h2c_channel_id, stream_pair->c2h_channel_id,
                mbytes_per_sec, bytes_transferred, interval_secs);
    }
    else
    {
        printf ("  %s %u -> %u No completed transfers\n",
                stream_pair->vfio_device->device_name, stream_pair->h2c_channel_id, stream_pair->c2h_channel_id);
    }
}


/**
 * @brief Sequence the testing of streams tested in parallel
 * @details
 *   This runs in the main thread and:
 *   a. Performs initialisation of the streams.
 *   b. Start a thread which performs the testing of streams.
 *   c. While the test is running displays statistics on the throughput of the streams.
 *      This function blocks on a semaphore, and is woken by the test thread when there are new statistics or the test
 *      has completed.
 *   d. Displays the overall statistics, and then releases the resources.
 * @param[in/out] context The test context, which on entry identifies the pairs of streams to be tested
 */
static void sequence_parallel_streams_test (stream_test_contexts_t *const context)
{
    struct sigaction action;
    int rc;
    uint32_t pair_index;
    pthread_t id;

    /* Perform initialisation.
     * X2X_ASSERT doesn't suspend the calling process on failure, which is reason for conditional tests on overall_success. */
    initialise_parallel_streams (context);

    if (context->overall_success)
    {
        /* Initialise the semaphores used to control access to the test interval statistics */
        rc = sem_init (&test_statistics_free, 0, 1);
        X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);
        rc = sem_init (&test_statistics_populated, 0, 0);
        X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);

        /* Install signal handler, used to request test is stopped */
        memset (&action, 0, sizeof (action));
        action.sa_handler = stop_test_handler;
        action.sa_flags = SA_RESTART;
        rc = sigaction (SIGINT, &action, NULL);
        X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);
    }

    if (context->overall_success)
    {
        rc = pthread_create (&id, NULL, parallel_streams_test_thread, context);
        X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);
    }

    /* Report regular statistics from the test, until indicates the test is complete */
    if (context->overall_success)
    {
        printf ("Press Ctrl-C to stop test\n");

        /* Report the statistics for each test interval, stopping when get the final statistics */
        bool exit_requested = false;
        while (!exit_requested)
        {
            /* Wait for the statistics upon completion of a test interval */
            rc = sem_wait (&test_statistics_populated);
            X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);

            /* Report the statistics */
            for (pair_index = 0; pair_index < context->num_stream_pairs; pair_index++)
            {
                display_stream_pair_statistics (context, pair_index, &test_statistics.stream_pairs[pair_index]);
            }
            printf ("\n");
            exit_requested = test_statistics.final_statistics;

            /* Indicate the main thread has completed using the test_statistics */
            rc = sem_post (&test_statistics_free);
            X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);
        }

        /* Wait for thread to exit */
        rc = pthread_join (id, NULL);
        X2X_ASSERT (&context->stream_pairs[0].c2h_transfer, rc == 0);
    }

    /* Display overall test statistics */
    printf ("Overall test statistics:\n");
    for (pair_index = 0; pair_index < context->num_stream_pairs; pair_index++)
    {
        display_stream_pair_statistics (context, pair_index, &context->stream_pairs[pair_index].overall_statistics);
    }
    printf ("\n");

    finalise_parallel_streams (context);
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    stream_test_contexts_t context = {0};
    uint32_t num_h2c_channels;
    uint32_t num_c2h_channels;
    uint32_t h2c_channel_id;
    uint32_t c2h_channel_id;
    uint32_t num_channel_combinations_tested;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Set buffering based upon command line arguments */
    context.num_descriptors = arg_stream_num_descriptors;
    context.bytes_per_buffer = arg_stream_mapping_size / context.num_descriptors;
    context.data_mapping_size_words = arg_stream_mapping_size / sizeof (uint32_t);
    printf ("Using num_descriptors=%u bytes_per_buffer=0x%zx data_mapping_size_words=0x%zx\n",
            context.num_descriptors, context.bytes_per_buffer, context.data_mapping_size_words);

    /* Create the array of AXI streams which can be tested */
    context.num_stream_pairs = 0;
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];
        vfio_device_t *const vfio_device = &designs.vfio_devices.devices[design_index];

        if (design->dma_bridge_present)
        {
            const bool design_uses_stream = design->dma_bridge_memory_size_bytes == 0;

            x2x_get_num_channels (vfio_device, design->dma_bridge_bar, design->dma_bridge_memory_size_bytes,
                    &num_h2c_channels, &num_c2h_channels, NULL, NULL);
            if (design_uses_stream && (num_h2c_channels > 0) && (num_c2h_channels > 0))
            {
                num_channel_combinations_tested = 0;
                for (h2c_channel_id = 0;
                     (h2c_channel_id < num_h2c_channels) && (num_channel_combinations_tested < arg_max_channel_combinations);
                     h2c_channel_id++)
                {
                    stream_test_context_t *const stream_pair = &context.stream_pairs[context.num_stream_pairs];

                    if ((h2c_channel_id & 1) == 1)
                    {
                        c2h_channel_id = h2c_channel_id - 1;
                    }
                    else
                    {
                        c2h_channel_id = (h2c_channel_id + 1) % num_c2h_channels;
                    }

                    stream_pair->design = design;
                    stream_pair->vfio_device = vfio_device;
                    stream_pair->h2c_channel_id = h2c_channel_id;
                    stream_pair->c2h_channel_id = c2h_channel_id;
                    printf ("Selecting test of %s design PCI device %s IOMMU group %s H2C channel %u C2H channel %u\n",
                            fpga_design_names[stream_pair->design->design_id],
                            stream_pair->vfio_device->device_name, stream_pair->vfio_device->iommu_group,
                            stream_pair->h2c_channel_id, stream_pair->c2h_channel_id);

                    num_channel_combinations_tested++;
                    context.num_stream_pairs++;
                }
            }
        }
    }

    if (context.num_stream_pairs > 0)
    {
        sequence_parallel_streams_test (&context);
    }

    close_pcie_fpga_designs (&designs);

    if (context.num_stream_pairs > 0)
    {
        printf ("\nOverall %s\n", context.overall_success ? "PASS" : "FAIL");
    }

    return EXIT_SUCCESS;
}
