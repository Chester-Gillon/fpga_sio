/*
 * @file test_dma_bridge_independent_streams.c
 * @date 1 Dec 2024
 * @author Chester Gillon
 * @brief A program to test perform tests on a Xilinx "DMA/Bridge Subsystem for PCI Express" with parallel independent streams
 * @details
 *  Only tests designs with AXI streams. It attempts to perform tests in parallel on all AXI streams present, to try and generate
 *  maximum PCIe throughput. It assumes the H2C and C2H streams are independent. If use with designs in which the streams
 *  are looped back internally, may run into timeouts as the transfers stall.
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
#include "xilinx_axi_stream_switch_configure.h"
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


/* Used to size arrays of streams in one direction which may be tested in parallel */
#define MAX_STREAMS_PER_DIRECTION (MAX_VFIO_DEVICES * X2X_MAX_CHANNELS)


/* Command line argument which sets the VFIO buffer allocation type */
static vfio_buffer_allocation_type_t arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;


/* Command line arguments which specify the size of the mapping for the host buffer when performing AXI stream transfers */
static size_t arg_stream_mapping_size = 0x40000000;


/* Command line arguments which specify the number of descriptors when performing AXI stream transfers */
static uint32_t arg_stream_num_descriptors = 64;


/* Command line argument which causes the first container, to be used for all DMA mappings. */
static bool arg_use_one_container_for_mappings;


/* Identifies the direction for one stream tested */
typedef enum
{
    X2X_DIRECTION_H2C,
    X2X_DIRECTION_C2H,
    X2X_DIRECTION_ARRAY_SIZE
} x2x_direction_t;

const static char *const x2x_direction_names[X2X_DIRECTION_ARRAY_SIZE] =
{
    [X2X_DIRECTION_H2C] = "H2C",
    [X2X_DIRECTION_C2H] = "C2H"
};


/* Command line arguments to specify which stream on which devices to perform the test on.
 * If no filters are specified on the command line, all possible streams are tested. */
typedef struct
{
    /* The location of the PCI device containing the streams to be tested */
    vfio_pci_device_location_filter_t device_filter;
    /* The number of tested on the PCI device, per direction */
    uint32_t num_device_streams[X2X_DIRECTION_ARRAY_SIZE];
    /* The channels IDs of the streams on the device to be tested */
    uint32_t channel_ids[X2X_DIRECTION_ARRAY_SIZE][X2X_MAX_CHANNELS];
} tested_device_filter_t;
static tested_device_filter_t arg_tested_device_filters[MAX_VFIO_DEVICES];
static uint32_t arg_num_tested_device_filters;


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"h2c_stream_device", required_argument, NULL, 0},
    {"c2h_stream_device", required_argument, NULL, 0},
    {"device_routing", required_argument, NULL, 0},
    {"buffer_allocation", required_argument, NULL, 0},
    {"stream_mapping_size", required_argument, NULL, 0},
    {"stream_num_descriptors", required_argument, NULL, 0},
    {"isolate_iommu_groups", no_argument, NULL, 0},
    {"use_one_container_for_mappings", no_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};


/* Set true in a signal handler when Ctrl-C is used to request a running test stops */
static volatile bool test_stop_requested;


/* Used to maintain statistics for the throughout on one AXI stream */
typedef struct
{
    /* Monotonic time for start of the statistics collection interval*/
    int64_t collection_interval_start_time;
    /* Monotonic time at which the most recent transfer in the statistics collection interval was completed */
    int64_t time_last_transfer_completed;
    /* The number of completed transfers in the statistics collection interval */
    uint32_t num_completed_transfers;
    /* The number of bytes transferred in the statistics collection interval */
    uint64_t num_transferred_bytes;
} stream_throughput_statistics_t;


/* Defines the context to test one AXI stream.
 * The mappings are separate for each context to simplify the software.
 * Sharing mappings between contexts could potentially reduce the number of page translations needed by the IOMMU,
 * but without testing not sure if that would increase performance. */
typedef struct
{
    /* The design containing the DMA bridge to test */
    fpga_design_t *design;
    /* The device containing the DMA bridge to test */
    vfio_device_t *vfio_device;
    /* Which channel to use */
    uint32_t channel_id;
    /* Read/write mapping for the descriptors */
    vfio_dma_mapping_t descriptors_mapping;
    /* Read or write mapping used by device (depending upon direction) */
    vfio_dma_mapping_t data_mapping;
    /* Used to perform transfers in one direction for the stream */
    x2x_transfer_context_t transfer;
    /* Array sizes for the number of descriptors. Each index gives the monotonic time at which the transfer was completed.
     * Used to update throughput statistics. */
    int64_t *completed_times;
    /* Index for the last descriptor to have completed, to read from completed_times when re-reseting interval_statistics for the
     * next reporting interval. */
    uint32_t last_completed_descriptor_index;
    /* The overall throughput statistics for the test */
    stream_throughput_statistics_t overall_statistics;
    /* The throughput statistics for the current reporting interval */
    stream_throughput_statistics_t interval_statistics;
} stream_test_context_t;


/* Contains the overall context for all the streams tested in parallel */
typedef struct
{
    /* The number of streams tested, per direction */
    uint32_t num_streams[X2X_DIRECTION_ARRAY_SIZE];
    /* Total number of streams tested, sum of num_streams[] */
    uint32_t total_streams_tested;
    /* The array of streams to test in parallel.
     * Valid indices are in the range [][0 .. stream_pairs-1] */
    stream_test_context_t streams[X2X_DIRECTION_ARRAY_SIZE][MAX_STREAMS_PER_DIRECTION];
    /* Points at the fist used entry in streams[], for code which uses any DMA transfer or device */
    stream_test_context_t *first_stream;
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
    stream_throughput_statistics_t streams[X2X_DIRECTION_ARRAY_SIZE][MAX_STREAMS_PER_DIRECTION];
    /* Set true in the final statistics before the independent_streams_test_thread() exits */
    bool final_statistics;
} stream_test_statistics_t;


/* test_statistics contains the statistics from the most recent completed test interval.
 * It is written by the independent_streams_test_thread, and read by the main thread to report the test progress.
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
    printf ("  test_dma_bridge_independent_streams <options>\n");
    printf ("   Test Xilinx DMA/Bridge Subsystem for PCI Express with independent streams\n");
    printf ("--h2c_stream_device <domain>:<bus>:<dev>.<func>,<h2c_channel_id>\n");
    printf ("  Specify a specific PCI device and H2C channel ID to perform a\n");
    printf ("  transmit stream test on\n");
    printf ("  May be used more than once.\n");
    printf ("--c2h_stream_device <domain>:<bus>:<dev>.<func>,<c2h_channel_id>\n");
    printf ("  Specify a specific PCI device and C2H channel ID to perform a\n");
    printf ("  receiver stream test on\n");
    printf ("  May be used more than once.\n");
    printf ("--device_routing <domain>:<bus>:<dev>.<func>[,<master_port>:<slave_port>]\n");
    printf ("  Specify a PCI device to set the AXI4-Stream Switch routing for.\n");
    printf ("  The routing in specified as zero or more pairs of the master port and the\n");
    printf ("  slave port used for the route. Unspecified master ports are left disabled\n");
    printf ("  May be used more than once.\n");
    printf ("--buffer_allocation heap|shared_memory|huge_pages\n");
    printf ("  Selects the VFIO buffer allocation type\n");
    printf ("--stream_mapping_size <size_bytes>\n");
    printf ("  Specifies the size of the mapping for the host buffer when performing AXI\n");
    printf ("  stream transfers.\n");
    printf ("--stream_num_descriptors <num_descriptors>\n");
    printf ("  Specifies the number of descriptors when performing AXI stream transfers.\n");
    printf ("--isolate_iommu_groups\n");
    printf ("  Causes each IOMMU group to use it's own container\n");
    printf ("--use_one_container_for_mappings\n");
    printf ("  Causes the first container to be used for all DMA mappings\n");
    printf ("  mappings.\n");
    printf ("\n");

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
    vfio_pci_device_location_filter_t filter;
    uint32_t channel_id;
    char device_name[64];
    bool found_existing_device;
    uint32_t device_filter_index;

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
            else if ((strcmp (optdef->name, "c2h_stream_device") == 0) || (strcmp (optdef->name, "h2c_stream_device") == 0))
            {
                const x2x_direction_t direction =
                        (strcmp (optdef->name, "c2h_stream_device") == 0) ? X2X_DIRECTION_C2H : X2X_DIRECTION_H2C;
                const int num_values = sscanf (optarg, "%d:%" SCNx8 ":%" SCNx8 ".%" SCNx8 ",%" SCNu32 "%c",
                        &filter.domain, &filter.bus, &filter.dev, &filter.func, &channel_id, &junk);

                if (num_values == 5)
                {
                    found_existing_device = false;
                    for (device_filter_index = 0;
                         !found_existing_device && (device_filter_index < arg_num_tested_device_filters);
                         device_filter_index++)
                    {
                        tested_device_filter_t *const existing = &arg_tested_device_filters[device_filter_index];

                        if ((filter.domain == existing->device_filter.domain) &&
                            (filter.bus    == existing->device_filter.bus   ) &&
                            (filter.dev    == existing->device_filter.dev   ) &&
                            (filter.func   == existing->device_filter.func  )   )
                        {
                            /* Append a stream to be tested to a device which is already to be tested */
                            if (existing->num_device_streams[direction] < X2X_MAX_CHANNELS)
                            {
                                existing->channel_ids[direction][existing->num_device_streams[direction]] = channel_id;
                                existing->num_device_streams[direction]++;
                            }
                            found_existing_device = true;
                        }
                    }

                    if (!found_existing_device && (arg_num_tested_device_filters < MAX_VFIO_DEVICES))
                    {
                        /* Add a new device to be tested */
                        tested_device_filter_t *const new_filter = &arg_tested_device_filters[arg_num_tested_device_filters];

                        new_filter->device_filter = filter;
                        new_filter->channel_ids[direction][0] = channel_id;
                        new_filter->num_device_streams[direction] = 1;
                        arg_num_tested_device_filters++;

                        snprintf (device_name, sizeof (device_name), "%04x:%02x:%02x.%x",
                                filter.domain, filter.bus, filter.dev, filter.func);
                        vfio_add_pci_device_location_filter (device_name);
                    }
                }
                else
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "device_routing") == 0)
            {
                const bool add_pci_device_location_filter = false;

                process_device_routing_argument (optarg, add_pci_device_location_filter);
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
            else if (strcmp (optdef->name, "isolate_iommu_groups") == 0)
            {
                vfio_enable_iommu_group_isolation ();
            }
            else if (strcmp (optdef->name, "use_one_container_for_mappings") == 0)
            {
                arg_use_one_container_for_mappings = true;
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
 * @brief Determine if a stream on a VFIO device is to be tested
 * @param[in] vfio_device The potential device to test
 * @param[in] direction The direction of the stream to potentially test
 * @param[in] channel_id The channel of the stream to potentially test
 * @return Returns true if the stream pair is to be tested
 */
static bool is_stream_tested (const vfio_device_t *const vfio_device, const x2x_direction_t direction, const uint32_t channel_id)
{
    bool stream_tested = false;

    if (arg_num_tested_device_filters == 0)
    {
        /* No filter supplied on command line arguments, so test all available streams */
        stream_tested = true;
    }
    else
    {
        for (uint32_t device_filter_index = 0;
             !stream_tested && (device_filter_index < arg_num_tested_device_filters);
             device_filter_index++)
        {
            const tested_device_filter_t *const tested_device = &arg_tested_device_filters[device_filter_index];

            if ((tested_device->device_filter.domain == vfio_device->pci_dev->domain) &&
                (tested_device->device_filter.bus    == vfio_device->pci_dev->bus   ) &&
                (tested_device->device_filter.dev    == vfio_device->pci_dev->dev   ) &&
                (tested_device->device_filter.func   == vfio_device->pci_dev->func  )   )
            {
                for (uint32_t stream_index = 0;
                     !stream_tested && (stream_index < tested_device->num_device_streams[direction]);
                     stream_index++)
                {
                    stream_tested = channel_id == tested_device->channel_ids[direction][stream_index];
                }
            }
        }
    }

    return stream_tested;
}


/**
 * @brief Perform the initialisation for all streams which are to be tested in parallel
 * @param[in/out] context The test context to initialise. overall_success will be false if the initialisation fails.
 */
static void initialise_independent_streams (stream_test_contexts_t *const context)
{
    uint32_t tx_test_pattern = 0;
    size_t word_index;

    context->overall_success = true;
    context->first_stream = NULL;
    for (x2x_direction_t direction = 0; context->overall_success && (direction < X2X_DIRECTION_ARRAY_SIZE); direction++)
    {
        for (uint32_t stream_index = 0; context->overall_success && (stream_index < context->num_streams[direction]); stream_index++)
        {
            stream_test_context_t *const stream = &context->streams[direction][stream_index];

            if (context->first_stream == NULL)
            {
                context->first_stream = stream;
            }

            /* Use command line option to control if attempt to use one container for all mappings.
             * This is to test the effect of the isolate_iommu_groups command line option when the stream pairs
             * are across more than IOMMU group. */
            vfio_iommu_container_t *const vfio_container_for_mapping = arg_use_one_container_for_mappings ?
                    context->first_stream->vfio_device->group->container : stream->vfio_device->group->container;

            /* Populate the transfer configurations to be used, selecting use of fixed size buffers */
            const x2x_transfer_configuration_t transfer_configuration =
            {
                .dma_bridge_memory_size_bytes = stream->design->dma_bridge_memory_size_bytes,
                .min_size_alignment = 1, /* The host memory is byte addressable */
                .num_descriptors = context->num_descriptors,
                .channels_submodule = (direction == X2X_DIRECTION_H2C) ? DMA_SUBMODULE_H2C_CHANNELS : DMA_SUBMODULE_C2H_CHANNELS,
                .channel_id = stream->channel_id,
                .bytes_per_buffer = context->bytes_per_buffer,
                .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
                .card_buffer_start_offset = 0, /* Not used for AXI stream */
                .c2h_stream_continuous = false,
                .timeout_seconds = TRANSFER_TIMEOUT_SECS,
                .vfio_device = stream->vfio_device,
                .bar_index = stream->design->dma_bridge_bar,
                .descriptors_mapping = &stream->descriptors_mapping,
                .data_mapping = &stream->data_mapping,
                .overall_success = &context->overall_success
            };

            /* Create read/write mapping for DMA descriptors */
            const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&transfer_configuration) +
                    x2x_get_descriptor_allocation_size (&transfer_configuration);
            allocate_vfio_container_dma_mapping (vfio_container_for_mapping, stream->vfio_device->dma_capability,
                    &stream->descriptors_mapping, descriptors_allocation_size,
                    VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

            /* Data mapping used by device, for the entire card memory */
            allocate_vfio_container_dma_mapping (vfio_container_for_mapping, stream->vfio_device->dma_capability,
                    &stream->data_mapping, arg_stream_mapping_size,
                    (direction == X2X_DIRECTION_H2C) ? VFIO_DMA_MAP_FLAG_READ : VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

            context->overall_success = (stream->descriptors_mapping.buffer.vaddr != NULL) &&
                                       (stream->data_mapping.buffer.vaddr        != NULL);
            if (context->overall_success)
            {
                /* Initialise the transfers */
                x2x_initialise_transfer_context (&stream->transfer, &transfer_configuration);
            }

            stream->completed_times = calloc (context->num_descriptors, sizeof (stream->completed_times[0]));

            if (context->overall_success && (direction == X2X_DIRECTION_H2C))
            {
                uint32_t *const tx_words = stream->data_mapping.buffer.vaddr;

                /* Populate a transmit test pattern.
                 * As the streams are independent the actual pattern doesn't get checked. */
                for (word_index = 0; word_index < context->data_mapping_size_words; word_index++)
                {
                    tx_words[word_index] = tx_test_pattern;
                    linear_congruential_generator (&tx_test_pattern);
                }
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
    X2X_ASSERT (&context->first_stream->transfer, rc == 0);

    test_statistics.final_statistics = final_statistics;
    for (x2x_direction_t direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
    {
        for (uint32_t stream_index = 0; context->overall_success && (stream_index < context->num_streams[direction]); stream_index++)
        {
            stream_test_context_t *const stream = &context->streams[direction][stream_index];

            test_statistics.streams[direction][stream_index] = stream->interval_statistics;
            stream->interval_statistics.num_completed_transfers = 0;
            stream->interval_statistics.num_transferred_bytes = 0;

            /* Set the start time for the next collection interval to be when the last transfer completed for the reported
             * interval. This makes the timespan used to report the throughput rate a multiple of a whole number of transfers,
             * so that the reported throughput in Mbytes/sec should not jitter when the time to complete one transfer buffer
             * isn't a multiple of the statistics reporting interval. */
            stream->interval_statistics.collection_interval_start_time =
                    stream->completed_times[stream->last_completed_descriptor_index];
        }
    }

    rc = sem_post (&test_statistics_populated);
    X2X_ASSERT (&context->first_stream->transfer, rc == 0);
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
static void *independent_streams_test_thread (void *const arg)
{
    stream_test_contexts_t *const context = arg;
    x2x_direction_t direction;
    uint32_t stream_index;
    uint32_t descriptor_index;
    void *buffer;
    uint32_t num_idle_streams;
    bool test_stopping;
    int64_t now;
    bool final_statistics;
    size_t transfer_len;
    bool end_of_packet;

    const int64_t nsecs_per_sec = 1000000000;
    const int64_t reporting_interval_ns = 10 * nsecs_per_sec;

    int64_t next_report_time = get_monotonic_time () + reporting_interval_ns;

    /* Start all transfers */
    for (descriptor_index = 0; context->overall_success && (descriptor_index < context->num_descriptors); descriptor_index++)
    {
        for (direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
        {
            for (stream_index = 0; context->overall_success && (stream_index < context->num_streams[direction]); stream_index++)
            {
                stream_test_context_t *const stream = &context->streams[direction][stream_index];

                if (descriptor_index == 0)
                {
                    stream->overall_statistics.collection_interval_start_time = get_monotonic_time ();
                }

                if (direction == X2X_DIRECTION_C2H)
                {
                    x2x_start_next_c2h_buffer (&stream->transfer);
                }
                else
                {
                    buffer = x2x_get_next_h2c_buffer (&stream->transfer);
                    X2X_ASSERT (&stream->transfer, buffer != NULL);
                    x2x_start_populated_descriptors (&stream->transfer);
                }
            }
        }
    }

    /* Initialise the throughput statistics (collection_interval_start_time set above) */
    for (direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
    {
        for (stream_index = 0; context->overall_success && (stream_index < context->num_streams[direction]); stream_index++)
        {
            stream_test_context_t *const stream = &context->streams[direction][stream_index];

            stream->last_completed_descriptor_index = context->num_descriptors - 1;
            stream->overall_statistics.time_last_transfer_completed =
                    stream->overall_statistics.collection_interval_start_time;
            stream->overall_statistics.num_completed_transfers = 0;
            stream->overall_statistics.num_transferred_bytes = 0;
            stream->interval_statistics = stream->overall_statistics;
        }
    }

    /* Run the test until either:
     * a. A failure occurs (DMA timeout) on any stream.
     * b. A test stop has been requested, and all previously queued transfers have completed. */
    num_idle_streams = 0;
    test_stopping = false;
    while (context->overall_success && (num_idle_streams < context->total_streams_tested))
    {
        /* Sample a request to stop the test */
        if (test_stop_requested)
        {
            test_stopping = true;
        }

        num_idle_streams = 0;
        for (direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
        {
            for (stream_index = 0; context->overall_success && (stream_index < context->num_streams[direction]); stream_index++)
            {
                stream_test_context_t *const stream = &context->streams[direction][stream_index];

                /* Poll for completion of transfer, updating the throughput statistics upon completion.
                 * Re-starts the transfer, unless the test has been requested to stop. */
                buffer = x2x_poll_completed_transfer (&stream->transfer, &transfer_len, &end_of_packet);
                if (buffer != NULL)
                {
                    /* Allow for CRC64 streams where C2H has a fixed packet length, rather than the C2H packet length */
                    const size_t num_transferred_bytes = (direction == X2X_DIRECTION_H2C) ? context->bytes_per_buffer : transfer_len;

                    now = get_monotonic_time ();
                    stream->overall_statistics.time_last_transfer_completed = now;
                    stream->overall_statistics.num_completed_transfers++;
                    stream->overall_statistics.num_transferred_bytes += num_transferred_bytes;
                    stream->interval_statistics.time_last_transfer_completed = now;
                    stream->interval_statistics.num_completed_transfers++;
                    stream->interval_statistics.num_transferred_bytes += num_transferred_bytes;

                    if (!test_stopping)
                    {
                        if (direction == X2X_DIRECTION_C2H)
                        {
                            x2x_start_next_c2h_buffer (&stream->transfer);
                        }
                        else
                        {
                            buffer = x2x_get_next_h2c_buffer (&stream->transfer);
                            X2X_ASSERT (&stream->transfer, buffer != NULL);
                            x2x_start_populated_descriptors (&stream->transfer);
                        }
                    }

                    stream->last_completed_descriptor_index =
                            (stream->last_completed_descriptor_index + 1) % context->num_descriptors;
                    stream->completed_times[stream->last_completed_descriptor_index] = now;
                }

                /* Once the test has been requested to stop, monitor when the transfers have become idle meaning
                 * all outstanding transfers have completed */
                if (test_stopping && (stream->transfer.num_in_use_descriptors == 0))
                {
                    num_idle_streams++;
                }
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
static void finalise_independent_streams (stream_test_contexts_t *const context)
{
    for (x2x_direction_t direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
    {
        for (uint32_t stream_index = 0; stream_index < context->num_streams[direction]; stream_index++)
        {
           stream_test_context_t *const stream = &context->streams[direction][stream_index];

           /* Finalise the transfer contexts if the initialisation completed without error */
           if (stream->transfer.completed_descriptor_count != NULL)
           {
               x2x_finalise_transfer_context (&stream->transfer);
           }

           report_if_transfer_failed (&stream->transfer);

           free (stream->completed_times);
           free_vfio_dma_mapping (&stream->data_mapping);
           free_vfio_dma_mapping (&stream->descriptors_mapping);
        }
    }
}

/**
 * @brief Display the statistics for one tested stream
 * @param[in] context The overall context
 * @param[in] direction The direction of tested stream the statistics are for.
 * @param[in] stream_index The tested stream stream the statistics are for.
 * @param[in] statistics The statistics to display, either for one interval or the overall test
 */
static void display_stream_statistics (const stream_test_contexts_t *const context,
                                       const x2x_direction_t direction,
                                       const uint32_t stream_index,
                                       const stream_throughput_statistics_t *const statistics)
{
    const stream_test_context_t *const stream = &context->streams[direction][stream_index];
    if (statistics->num_completed_transfers > 0)
    {
        const double interval_secs =
                ((double) (statistics->time_last_transfer_completed - statistics->collection_interval_start_time)) / 1E9;
        const double mbytes_per_sec = (((double) statistics->num_transferred_bytes) / 1E6) / interval_secs;

        printf ("  %s %s channel %u %.3f Mbytes/sec (%zu bytes in %u transfers over %.06f secs)\n",
                stream->vfio_device->device_name, x2x_direction_names[direction], stream->channel_id,
                mbytes_per_sec, statistics->num_transferred_bytes, statistics->num_completed_transfers, interval_secs);
    }
    else
    {
        printf ("  %s %s channel %u No completed transfers\n",
                stream->vfio_device->device_name, x2x_direction_names[direction], stream->channel_id);
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
static void sequence_independent_streams_test (stream_test_contexts_t *const context)
{
    struct sigaction action;
    int rc;
    x2x_direction_t direction;
    uint32_t stream_index;
    pthread_t id;

    /* Perform initialisation.
     * X2X_ASSERT doesn't suspend the calling process on failure, which is reason for conditional tests on overall_success. */
    initialise_independent_streams (context);

    if (context->overall_success)
    {
        /* Initialise the semaphores used to control access to the test interval statistics */
        rc = sem_init (&test_statistics_free, 0, 1);
        X2X_ASSERT (&context->first_stream->transfer, rc == 0);
        rc = sem_init (&test_statistics_populated, 0, 0);
        X2X_ASSERT (&context->first_stream->transfer, rc == 0);

        /* Install signal handler, used to request test is stopped */
        memset (&action, 0, sizeof (action));
        action.sa_handler = stop_test_handler;
        action.sa_flags = SA_RESTART;
        rc = sigaction (SIGINT, &action, NULL);
        X2X_ASSERT (&context->first_stream->transfer, rc == 0);
    }

    if (context->overall_success)
    {
        rc = pthread_create (&id, NULL, independent_streams_test_thread, context);
        X2X_ASSERT (&context->first_stream->transfer, rc == 0);
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
            X2X_ASSERT (&context->first_stream->transfer, rc == 0);

            /* Report the statistics */
            for (direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
            {
                for (stream_index = 0; stream_index < context->num_streams[direction]; stream_index++)
                {
                    display_stream_statistics (context, direction, stream_index, &test_statistics.streams[direction][stream_index]);
                }
            }
            printf ("\n");
            exit_requested = test_statistics.final_statistics;

            /* Indicate the main thread has completed using the test_statistics */
            rc = sem_post (&test_statistics_free);
            X2X_ASSERT (&context->first_stream->transfer, rc == 0);
        }

        /* Wait for thread to exit */
        rc = pthread_join (id, NULL);
        X2X_ASSERT (&context->first_stream->transfer, rc == 0);
    }

    /* Display overall test statistics */
    printf ("Overall test statistics:\n");
    for (direction = 0; direction < X2X_DIRECTION_ARRAY_SIZE; direction++)
    {
        for (stream_index = 0; stream_index < context->num_streams[direction]; stream_index++)
        {
            display_stream_statistics (context, direction, stream_index,
                    &context->streams[direction][stream_index].overall_statistics);
        }
    }
    printf ("\n");

    finalise_independent_streams (context);
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    uint32_t num_h2c_channels;
    uint32_t num_c2h_channels;
    uint32_t channel_id;
    x2x_direction_t direction;
    device_routing_t routing;
    stream_test_contexts_t context = {0};

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
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];
        vfio_device_t *const vfio_device = &designs.vfio_devices.devices[design_index];

        if (design->dma_bridge_present)
        {
            const bool design_uses_stream = design->dma_bridge_memory_size_bytes == 0;

            x2x_get_num_channels (vfio_device, design->dma_bridge_bar, design->dma_bridge_memory_size_bytes,
                    &num_h2c_channels, &num_c2h_channels, NULL, NULL);
            if (design_uses_stream)
            {
                if (design->axi_switch_regs != NULL)
                {
                    /* When the design contains a AXI4-Stream Switch allow the switch to be configured.
                     * The configured routes are not used in the decision of which streams to test, since this program is
                     * used to investigate independent use of H2C and C2H streams which may not be connected. */
                    configure_routing_for_device (design, &routing);
                }

                direction = X2X_DIRECTION_H2C;
                for (channel_id = 0; channel_id < num_h2c_channels; channel_id++)
                {
                    if (is_stream_tested (vfio_device, direction, channel_id))
                    {
                        stream_test_context_t *const stream = &context.streams[direction][context.num_streams[direction]];

                        stream->design = design;
                        stream->vfio_device = vfio_device;
                        stream->channel_id = channel_id;
                        printf ("Selecting test of %s design PCI device %s IOMMU group %s %s channel %u\n",
                                fpga_design_names[stream->design->design_id],
                                stream->vfio_device->device_name, stream->vfio_device->group->iommu_group_name,
                                x2x_direction_names[direction], stream->channel_id);

                        context.num_streams[direction]++;
                        context.total_streams_tested++;
                    }
                }

                direction = X2X_DIRECTION_C2H;
                for (channel_id = 0; channel_id < num_c2h_channels; channel_id++)
                {
                    if (is_stream_tested (vfio_device, direction, channel_id))
                    {
                        stream_test_context_t *const stream = &context.streams[direction][context.num_streams[direction]];

                        stream->design = design;
                        stream->vfio_device = vfio_device;
                        stream->channel_id = channel_id;
                        printf ("Selecting test of %s design PCI device %s IOMMU group %s %s channel %u\n",
                                fpga_design_names[stream->design->design_id],
                                stream->vfio_device->device_name, stream->vfio_device->group->iommu_group_name,
                                x2x_direction_names[direction], stream->channel_id);

                        context.num_streams[direction]++;
                        context.total_streams_tested++;
                    }
                }
            }
        }
    }

    if (context.total_streams_tested > 0)
    {
        sequence_independent_streams_test (&context);
    }

    close_pcie_fpga_designs (&designs);

    if (context.total_streams_tested > 0)
    {
        printf ("\nOverall %s\n", context.overall_success ? "PASS" : "FAIL");
    }

    return context.overall_success ? EXIT_SUCCESS : EXIT_FAILURE;
}
