/*
 * @file test_dma_bridge.c
 * @date 6 Jan 2024
 * @author Chester Gillon
 * @brief A program to perform tests on a Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details
 *  Tests the "DMA/Bridge Subsystem for PCI Express" in the FPGA designs which either have:
 *  1. Memory accessible by the DMA bridge, for which can write to the memory and read back the contents by:
 *     a. H2C transfer from a host buffer to the card memory.
 *     b. C2H transfer from the card memory back to a different host buffer.
 *
 *     When multiple channels are configured in the DMA bridge, all combinations of H2C and C2H channels
 *     can be used for transfers.
 *  b. AXI streams which are looped back inside the FPGA. The allows a transfer from:
 *     a. H2C from host buffer to stream.
 *     b. C2H from stream to a different host buffer.
 *
 *     The program has a built-in assumptions about which H2C and C2H are looped back inside the FPGA.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <getopt.h>
#include <unistd.h>


/* Use a single fixed transfer timeout, to stop the test from hanging */
#define TRANSFER_TIMEOUT_SECS 10

/** Delimiter for comma-separated command line arguments */
#define DELIMITER ","


/* Stores the results from x2x_poll_completed_transfer() for a completed C2H transfer when using an AXI stream.
 * Consider changing the xilinx_dma_bridge_transfers API to avoid this. */
typedef struct
{
    void *host_buffer;
    size_t transfer_len;
    bool end_of_packet;
} c2h_stream_buffer_t;


/* The list of different tests which can be performed */
typedef enum
{
    /* Perform a write/read test of DMA accessible memory using a pair of channels, using fixed size buffers */
    DMA_TEST_MEMORY_FIXED_BUFFERS,
    /* Perform a DMA test of a pair of AXI streams which are looped-back, using fixed size buffers.
     * The software starts each C2H transfer. */
    DMA_TEST_STREAM_FIXED_BUFFERS,
    /* Perform a DMA test of a pair of AXI streams which are looped-back, using fixed size buffers.
     * The C2H DMA runs continuously, without software having to start each C2H transfer. */
    DMA_TEST_STREAM_FIXED_BUFFERS_C2H_CONTINUOUS,
    /* Perform a write/read test of DMA accessible memory using a pair of channels, and transfers in which
     * the descriptors are modified before use. */
    DMA_TEST_MEMORY_VARIABLE_TRANSFERS,
    /* Perform a DMA test of a pair of AXI streams which are looped-back, and transfers in which the
     * descriptors are modified before use. */
    DMA_TEST_STREAM_VARIABLE_TRANSFERS,

    DMA_TEST_ARRAY_SIZE
} dma_test_t;

/* The names of the different tests */
static const char *const dma_test_names[DMA_TEST_ARRAY_SIZE] =
{
    [DMA_TEST_MEMORY_FIXED_BUFFERS] = "memory_fixed_buffers",
    [DMA_TEST_STREAM_FIXED_BUFFERS] = "stream_fixed_buffers",
    [DMA_TEST_STREAM_FIXED_BUFFERS_C2H_CONTINUOUS] = "stream_fixed_buffers_c2h_continuous",
    [DMA_TEST_MEMORY_VARIABLE_TRANSFERS] = "memory_variable_transfers",
    [DMA_TEST_STREAM_VARIABLE_TRANSFERS] = "stream_variable_transfers"
};

/* Identifies which tests use AXI streams, as opposed to DMA accessible memory */
static const bool dma_test_uses_stream[DMA_TEST_ARRAY_SIZE] =
{
    [DMA_TEST_STREAM_FIXED_BUFFERS] = true,
    [DMA_TEST_STREAM_FIXED_BUFFERS_C2H_CONTINUOUS] = true,
    [DMA_TEST_STREAM_VARIABLE_TRANSFERS] = true
};


/* Command line argument used to enable which tests to perform */
static bool arg_enabled_tests[DMA_TEST_ARRAY_SIZE] =
{
    [DMA_TEST_MEMORY_FIXED_BUFFERS] = true,
    [DMA_TEST_STREAM_FIXED_BUFFERS] = true,
    [DMA_TEST_STREAM_FIXED_BUFFERS_C2H_CONTINUOUS] = true,
    [DMA_TEST_MEMORY_VARIABLE_TRANSFERS] = true,
    [DMA_TEST_STREAM_VARIABLE_TRANSFERS] = true
};


/* Command line argument which sets the VFIO buffer allocation type */
static vfio_buffer_allocation_type_t arg_buffer_allocation = VFIO_BUFFER_ALLOCATION_HEAP;


/* Command line argument which selects VFIO_DEVICE_DMA_CAPABILITY_A32, for testing the vfio_access code */
static int arg_test_a32_dma_capability;


/* Command line argument which specifies the maximum buffer length when using transfers with fixed size buffers.
 * Defaults to the next lower power of two from the maximum, to short-circuit the bytes_per_buffer calculation
 * when fixed size buffers are used. */
static size_t arg_max_buffer_size = (DMA_DESCRIPTOR_MAX_LEN + 1) / 2;


/* Command line argument which specifies the maximum number of combinations of different H2C and C2H channels tested */
static uint32_t arg_max_channel_combinations = X2X_MAX_CHANNELS * X2X_MAX_CHANNELS;


/* Command line arguments which specify the size of the mapping for the host buffer when performing AXI stream transfers */
static size_t arg_stream_h2c_mapping_size = 0x40000000;
static size_t arg_stream_c2h_mapping_size = 0x40000000;


/* Command line arguments which specify the number of descriptors when performing AXI stream transfers */
static uint32_t arg_stream_h2c_num_descriptors = 64;
static uint32_t arg_stream_c2h_num_descriptors = 64;


/* Command line arguments which specify the length of each transfer when performing memory or stream variable transfers */
static size_t arg_h2c_transfer_length = 0x10000000;
static size_t arg_c2h_transfer_length = 0x10000000;


/* Command line argument which specifies the width of the AXI streams, which controls C2H transfer alignment */
static size_t arg_stream_axi_width_bytes = 16;


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"device", required_argument, NULL, 0},
    {"a32", no_argument, &arg_test_a32_dma_capability, true},
    {"max_buffer_size", required_argument, NULL, 0},
    {"max_channel_combinations", required_argument, NULL, 0},
    {"buffer_allocation", required_argument, NULL, 0},
    {"stream_mapping_size", required_argument, NULL, 0},
    {"stream_num_descriptors", required_argument, NULL, 0},
    {"enabled_tests", required_argument, NULL, 0},
    {"transfer_length", required_argument, NULL, 0},
    {"stream_axi_width_bytes", required_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};



static inline size_t min_size_t (const size_t left, const size_t right)
{
    return (left < right) ? left : right;
}

static inline uint32_t min_uint32_t (const uint32_t left, const uint32_t right)
{
    return (left < right) ? left : right;
}


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("  test_dma_bridge <options>   Test Xilinx DMA/Bridge Subsystem for PCI Express\n");
    printf ("\n");
    printf ("--device <domain>:<bus>:<dev>.<func>\n");
    printf ("  only open using VFIO specific PCI devices in the event that there is one than\n");
    printf ("  one PCI device which matches the identity filters.\n");
    printf ("  May be used more than once.\n");
    printf ("--a32\n");
    printf ("  Selects VFIO_DEVICE_DMA_CAPABILITY_A32, for testing the vfio_access code\n");
    printf ("--max_buffer_size <size_bytes>\n");
    printf ("  Specifies the maximum buffer length when using transfers with fixed size\n");
    printf ("  buffers. Reducing increases the number of buffers used.\n");
    printf ("  Max value is limited by the DMA descriptor length having 28-bits\n");
    printf ("--max_channel_combinations <num>\n");
    printf ("  When a DMA bridge has more than 1 channel, limits the maximum number of\n");
    printf ("  different H2C and C2H channels used during testing\n");
    printf ("--buffer_allocation heap|shared_memory|huge_pages\n");
    printf ("  Selects the VFIO buffer allocation type\n");
    printf ("--stream_mapping_size <h2c>,<c2h>\n");
    printf ("  Specifies the size of the mapping for the host buffer when performing AXI\n");
    printf ("  stream transfers. May use different values for each direction.\n");
    printf ("--stream_num_descriptors <h2c>,<c2h>\n");
    printf ("  Specifies the number of descriptors when performing AXI stream transfers.\n");
    printf ("  May use different values for each direction.\n");
    printf ("--transfer_length <h2c>,<c2h>\n");
    printf ("  Specifies the length of each transfer when performing memory or stream\n");
    printf ("  variable transfers. May use different values for each direction.\n");
    printf ("--stream_axi_width_bytes <width>\n");
    printf ("  Sets the AXI stream width, used as the alignment for C2H stream transfers.\n");
    printf ("  The reason is a packet which is split across multiple C2H descriptors aligns\n");
    printf ("  each write except the one with EOP to the AXI stream width.\n");
    printf ("--enabled_tests <comma separated test names>\n");
    printf ("  Selects which tests are enabled. Possible tests are:\n");
    for (dma_test_t dma_test = 0; dma_test < DMA_TEST_ARRAY_SIZE; dma_test++)
    {
        printf ("  - %s\n", dma_test_names[dma_test]);
    }

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
            else if (strcmp (optdef->name, "max_buffer_size") == 0)
            {
                if ((sscanf (optarg, "%zi%c", &arg_max_buffer_size, &junk) != 1) ||
                    (arg_max_buffer_size == 0) || (arg_max_buffer_size > DMA_DESCRIPTOR_MAX_LEN))
                {
                    fprintf (stderr, "Invalid %s %s\n", optdef->name, optarg);
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
                if ((sscanf (optarg, "%zi,%zi%c", &arg_stream_h2c_mapping_size, &arg_stream_c2h_mapping_size, &junk) != 2) ||
                    (arg_stream_h2c_mapping_size < sizeof (uint32_t)) || (arg_stream_c2h_mapping_size < sizeof (uint32_t)))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
                if (((arg_stream_h2c_mapping_size % sizeof (uint32_t)) != 0) ||
                    ((arg_stream_c2h_mapping_size % sizeof (uint32_t)) != 0))
                {
                    printf ("stream_mapping_size not a multiple of words\n");
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "stream_num_descriptors") == 0)
            {
                if ((sscanf (optarg, "%i,%i%c", &arg_stream_h2c_num_descriptors, &arg_stream_c2h_num_descriptors, &junk) != 2) ||
                    (arg_stream_h2c_num_descriptors == 0) || (arg_stream_c2h_num_descriptors == 0))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "transfer_length") == 0)
            {
                if ((sscanf (optarg, "%zi,%zi%c", &arg_h2c_transfer_length, &arg_c2h_transfer_length, &junk) != 2) ||
                    (arg_h2c_transfer_length == 0) || (arg_c2h_transfer_length == 0))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else if (strcmp (optdef->name, "enabled_tests") == 0)
            {
                /* Parse the comma delimited test names, which define which tests to enable */
                char *const test_names = strdup (optarg);
                char *saveptr = NULL;
                char *token;
                dma_test_t dma_test;
                bool test_name_found;

                for (dma_test = 0; dma_test < DMA_TEST_ARRAY_SIZE; dma_test++)
                {
                    arg_enabled_tests[dma_test] = false;
                }
                token = strtok_r (test_names, DELIMITER, &saveptr);
                while (token != NULL)
                {
                    test_name_found = false;
                    for (dma_test = 0; !test_name_found && (dma_test < DMA_TEST_ARRAY_SIZE); dma_test++)
                    {
                        if (strcmp (dma_test_names[dma_test], token) == 0)
                        {
                            arg_enabled_tests[dma_test] = true;
                            test_name_found = true;
                        }
                    }
                    if (!test_name_found)
                    {
                        printf ("%s contains unknown test name %s\n", optdef->name, token);
                        exit (EXIT_FAILURE);
                    }
                    token = strtok_r (NULL, DELIMITER, &saveptr);
                }
                free (test_names);
            }
            else if (strcmp (optdef->name, "stream_axi_width_bytes") == 0)
            {
                if ((sscanf (optarg, "%zu%c", &arg_stream_axi_width_bytes, &junk) != 1) || (arg_stream_axi_width_bytes == 0))
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
 * @brief If a transfer failed, report an error to the console
 * @param[in] context The transfer context to check for errors.
 */
static void report_if_transfer_failed (const x2x_transfer_context_t *const context)
{
    if (context->failed)
    {
        printf ("  %s failure : %s%s\n",
                (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ? "H2C" : "C2H",
                context->error_message,
                context->timeout_awaiting_idle_at_finalisation ? " (+timeout waiting for idle at finalisation)" : "");
    }
}


/**
 * @brief Perform a DMA test of a pair of AXI streams which are looped-back, using fixed size buffers
 * @details Allows testing when:
 *  a. The C2H and H2C having different number of descriptors / mapping sizes, where each test iteration
 *     performs the maximum number of transfers which won't can't the C2H (receive) buffers to overflow
 *     before they are read.
 *  b. Each H2C buffer transfer is sent as a single packet. If The C2H buffer is smaller, then expects
 *     each H2C transfer to be split across multiple C2H buffers with end-of-packet only set on the
 *     final C2H buffer.
 *  c. C2H to operate with either the software having to start each transfer, or the DMA running continuously
 * @param[in] design The design containing the DMA bridge to test
 * @param[in/out] vfio_device The device containing the DMA bridge to test
 * @param[in] h2c_channel_id Which channel to use for H2C (transmit) transfers
 * @param[in] c2h_channel_id Which channel to use for C2H (receive) transfers
 * @param[in] c2h_stream_continuous Controls how the the C2H transfers are queued:
 *            - When false the software has to queue each transfer
 *            - When true the DMA is left to run continuously, without the software needing to queue transfers.
 *              The test controls the rate which at which H2C transfers are started, such that the C2H buffers
 *              can't be overwritten before their content has been verified.
 * @return Returns true if the test passed, or false otherwise
 */
static bool test_stream_loopback_with_fixed_buffers (const fpga_design_t *const design, vfio_device_t *const vfio_device,
                                                     const uint32_t h2c_channel_id, const uint32_t c2h_channel_id,
                                                     const bool c2h_stream_continuous)
{
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    transfer_timing_t populate_test_pattern_timing;
    transfer_timing_t verify_test_pattern_timing;
    transfer_timing_t h2c_and_c2h_transfer_timing;
    bool success;

    /* Populate the transfer configurations to be used:
     * a. The number of descriptors is set from the command line arguments.
     * b. The size of each buffer is set from dividing the mapping size from the command line arguments by the
     *    number of buffers, and ensuring a multiple of the word size. */
    const size_t h2c_buffer_size_words = (arg_stream_h2c_mapping_size / arg_stream_h2c_num_descriptors) / sizeof (uint32_t);
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* Stream has tlast to allow arbitrary number of bytes */
        .num_descriptors = arg_stream_h2c_num_descriptors,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = h2c_channel_id,
        .bytes_per_buffer = h2c_buffer_size_words * sizeof (uint32_t),
        .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = false,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &h2c_data_mapping,
        .overall_success = &success
    };

    const size_t c2h_aligned_buffer_size_bytes =
            ((arg_stream_c2h_mapping_size / arg_stream_c2h_num_descriptors) / arg_stream_axi_width_bytes) *
            arg_stream_axi_width_bytes;
    const size_t c2h_buffer_size_words = c2h_aligned_buffer_size_bytes / sizeof (uint32_t);
    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1,
        .num_descriptors = arg_stream_c2h_num_descriptors,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = c2h_channel_id,
        .bytes_per_buffer = c2h_buffer_size_words * sizeof (uint32_t),
        .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = c2h_stream_continuous,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &c2h_data_mapping,
        .overall_success = &success
    };

    /* Calculate the number of C2H buffers to hold one H2C buffers worth of data, allowing for the buffer sizes
     * used being different for each transfer direction.
     * I.e. a single H2C transfer may fill one or more C2H buffers, with the final buffer being partially filled. */
    const uint32_t num_c2h_buffers_per_h2c_buffer = (uint32_t)
            ((h2c_transfer_configuration.bytes_per_buffer + (c2h_transfer_configuration.bytes_per_buffer - 1)) /
             c2h_transfer_configuration.bytes_per_buffer);

    /* Determine the amount of data to be transferred each test iteration, to avoid overflowing the C2H buffers
     * which may be sized differently to the H2C buffers */
    const uint32_t num_h2c_buffers_which_fit_in_c2h_mapping =
            c2h_transfer_configuration.num_descriptors / num_c2h_buffers_per_h2c_buffer;
    const uint32_t num_h2c_buffers_per_iteration =
            min_uint32_t (num_h2c_buffers_which_fit_in_c2h_mapping, c2h_transfer_configuration.num_descriptors);
    const uint32_t num_c2h_buffers_per_iteration = num_h2c_buffers_per_iteration * num_c2h_buffers_per_h2c_buffer;

    const size_t num_bytes_per_iteration = num_h2c_buffers_per_iteration * h2c_transfer_configuration.bytes_per_buffer;
    const size_t num_words_per_iteration = num_bytes_per_iteration / sizeof (uint32_t);

    const size_t h2c_buffer_size_bytes = h2c_transfer_configuration.num_descriptors * h2c_transfer_configuration.bytes_per_buffer;
    const size_t c2h_buffer_size_bytes = c2h_transfer_configuration.num_descriptors * c2h_transfer_configuration.bytes_per_buffer;

    /* Allocate storage for the pointers to each host buffer, to validate that buffers are returned in the expected order */
    uint32_t **const tx_buffers = calloc (h2c_transfer_configuration.num_descriptors, sizeof (tx_buffers[0]));
    c2h_stream_buffer_t *const rx_buffers = calloc (c2h_transfer_configuration.num_descriptors, sizeof (rx_buffers[0]));

    printf ("\nTesting streams using H2C %u buffers of size 0x%zx bytes, C2H %u buffers of size 0x%zx bytes%s, H2C channel %u C2H channel %u\n",
            h2c_transfer_configuration.num_descriptors, h2c_transfer_configuration.bytes_per_buffer,
            c2h_transfer_configuration.num_descriptors, c2h_transfer_configuration.bytes_per_buffer,
            c2h_stream_continuous ? " in continuous mode" : "",
            h2c_channel_id, c2h_channel_id);

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    /* Read mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, h2c_buffer_size_bytes,
            VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

    /* Write mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, c2h_buffer_size_bytes,
            VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    success = (descriptors_mapping.buffer.vaddr != NULL) &&
              (h2c_data_mapping.buffer.vaddr    != NULL) &&
              (c2h_data_mapping.buffer.vaddr    != NULL);

    if (success)
    {
        uint32_t tx_test_pattern = 0;
        uint32_t rx_test_pattern = 0;
        uint32_t *const tx_words = h2c_data_mapping.buffer.vaddr;
        const uint32_t *const rx_words = c2h_data_mapping.buffer.vaddr;
        size_t word_index;
        uint32_t next_h2c_buffer_index = 0;
        uint32_t next_c2h_buffer_index = 0;
        uint32_t buffer_offset;
        uint32_t num_h2c_completed;
        uint32_t num_c2h_completed;
        size_t transfer_len;
        bool end_of_packet;
        void *h2c_buffer;
        void *c2h_buffer;
        size_t remaining_h2c_buffer_bytes;

        initialise_transfer_timing (&populate_test_pattern_timing, "populate test pattern", num_bytes_per_iteration);
        initialise_transfer_timing (&verify_test_pattern_timing, "verify test pattern", num_bytes_per_iteration);
        initialise_transfer_timing (&h2c_and_c2h_transfer_timing, "host-to-card and card-to-host DMA", num_bytes_per_iteration);

        /* Initialise the transfers */
        x2x_initialise_transfer_context (&h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&c2h_transfer, &c2h_transfer_configuration);

        /* Perform test iterations to exercise all values of 32-bit test words */
        for (size_t total_words = 0; success && (total_words < 0x100000000UL); total_words += num_words_per_iteration)
        {
            /* Fill the transmit buffers with the next test pattern. Done per buffer, as the buffer index may wrap. */
            transfer_time_start (&populate_test_pattern_timing);
            for (buffer_offset = 0; buffer_offset < num_h2c_buffers_per_iteration; buffer_offset++)
            {
                const uint32_t buffer_index = (next_h2c_buffer_index + buffer_offset) % h2c_transfer_configuration.num_descriptors;
                uint32_t *const buffer_words = &tx_words[buffer_index * h2c_buffer_size_words];

                for (word_index = 0; word_index < h2c_buffer_size_words; word_index++)
                {
                    buffer_words[word_index] = tx_test_pattern;
                    linear_congruential_generator (&tx_test_pattern);
                }
            }
            transfer_time_stop (&populate_test_pattern_timing);

            /* If not using continuous mode, start all CH2 buffer transfers for the iteration, before starting the H2C transfers.
             * This is so the C2H stream is ready for the transfers */
            transfer_time_start (&h2c_and_c2h_transfer_timing);
            if (!c2h_stream_continuous)
            {
                for (buffer_offset = 0; success && (buffer_offset < num_c2h_buffers_per_iteration); buffer_offset++)
                {
                    x2x_start_next_c2h_buffer (&c2h_transfer);
                }
            }

            /* Start all H2C buffer transfers for the iteration */
            for (buffer_offset = 0; success && (buffer_offset < num_h2c_buffers_per_iteration); buffer_offset++)
            {
                X2X_ASSERT (&h2c_transfer, x2x_get_next_h2c_buffer (&h2c_transfer) != NULL);
                x2x_start_populated_descriptors (&h2c_transfer);
            }

            /* Wait for all transfers to complete */
            num_h2c_completed = 0;
            num_c2h_completed = 0;
            while (success &&
                   ((num_h2c_completed < num_h2c_buffers_per_iteration) || (num_c2h_completed < num_c2h_buffers_per_iteration)))
            {
                h2c_buffer = x2x_poll_completed_transfer (&h2c_transfer, &transfer_len, NULL);
                if (h2c_buffer != NULL)
                {
                    X2X_ASSERT (&h2c_transfer, transfer_len == h2c_transfer_configuration.bytes_per_buffer);
                    tx_buffers[(next_h2c_buffer_index + num_h2c_completed) % h2c_transfer_configuration.num_descriptors] = h2c_buffer;
                    num_h2c_completed++;
                }

                c2h_buffer = x2x_poll_completed_transfer (&c2h_transfer, &transfer_len, &end_of_packet);;
                if (c2h_buffer != NULL)
                {
                    c2h_stream_buffer_t *const rx_buffer =
                            &rx_buffers[(next_c2h_buffer_index + num_c2h_completed) % c2h_transfer_configuration.num_descriptors];

                    rx_buffer->host_buffer = c2h_buffer;
                    rx_buffer->transfer_len = transfer_len;
                    rx_buffer->end_of_packet = end_of_packet;
                    num_c2h_completed++;
                }
            }
            transfer_time_stop (&h2c_and_c2h_transfer_timing);

            /* Check the transmit buffers returned were correct */
            for (buffer_offset = 0; buffer_offset < num_h2c_buffers_per_iteration; buffer_offset++)
            {
                X2X_ASSERT (&h2c_transfer, tx_buffers[next_h2c_buffer_index] ==
                        &tx_words[next_h2c_buffer_index * h2c_buffer_size_words]);
                next_h2c_buffer_index = (next_h2c_buffer_index + 1) % h2c_transfer_configuration.num_descriptors;
            }

            /* Verify that all receive buffers have the expected contents.
             * This has to allow for one H2C buffer being potentially split across multiple C2H buffers. */
            transfer_time_start (&verify_test_pattern_timing);
            remaining_h2c_buffer_bytes = h2c_transfer_configuration.bytes_per_buffer;
            for (buffer_offset = 0; success && (buffer_offset < num_c2h_buffers_per_iteration); buffer_offset++)
            {
                const c2h_stream_buffer_t *const rx_buffer = &rx_buffers[next_c2h_buffer_index];
                const uint32_t *const buffer_words = &rx_words[next_c2h_buffer_index * c2h_buffer_size_words];
                const bool expected_end_of_packet = remaining_h2c_buffer_bytes <= c2h_transfer_configuration.bytes_per_buffer;
                const size_t expected_transfer_len =
                        expected_end_of_packet ? remaining_h2c_buffer_bytes : c2h_transfer_configuration.bytes_per_buffer;
                const size_t num_words = rx_buffer->transfer_len / sizeof (uint32_t);

                X2X_ASSERT (&c2h_transfer, rx_buffer->host_buffer == buffer_words);
                X2X_ASSERT (&c2h_transfer, rx_buffer->transfer_len == expected_transfer_len);
                X2X_ASSERT (&c2h_transfer, rx_buffer->end_of_packet == expected_end_of_packet);

                for (word_index = 0; success && (word_index < num_words); word_index++)
                {
                    if (buffer_words[word_index] != rx_test_pattern)
                    {
                        x2x_record_failure (&c2h_transfer, "Rx word[%u][%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32,
                                next_c2h_buffer_index, word_index, buffer_words[word_index], rx_test_pattern);
                        success = false;
                    }
                    linear_congruential_generator (&rx_test_pattern);
                }

                next_c2h_buffer_index = (next_c2h_buffer_index + 1) % c2h_transfer_configuration.num_descriptors;
                if (expected_end_of_packet)
                {
                    remaining_h2c_buffer_bytes = h2c_transfer_configuration.bytes_per_buffer;
                }
                else
                {
                    remaining_h2c_buffer_bytes -= rx_buffer->transfer_len;
                }
            }
            transfer_time_stop (&verify_test_pattern_timing);
        }

        x2x_finalise_transfer_context (&h2c_transfer);
        x2x_finalise_transfer_context (&c2h_transfer);

        if (success)
        {
            display_transfer_timing_statistics (&populate_test_pattern_timing);
            display_transfer_timing_statistics (&h2c_and_c2h_transfer_timing);
            display_transfer_timing_statistics (&verify_test_pattern_timing);
            printf ("TEST PASS\n");
        }
        else
        {
            printf ("TEST FAIL:\n");
            report_if_transfer_failed (&h2c_transfer);
            report_if_transfer_failed (&c2h_transfer);
        }
    }
    else
    {
        printf ("TEST FAIL : allocate_vfio_dma_mapping()\n");
    }

    free_vfio_dma_mapping (&c2h_data_mapping);
    free_vfio_dma_mapping (&h2c_data_mapping);
    free_vfio_dma_mapping (&descriptors_mapping);
    free (tx_buffers);
    free (rx_buffers);

    return success;
}


/**
 * @briefPerform a DMA test of a pair of AXI streams which are looped-back, using variable size transfers
 * @param[in] design The design containing the DMA bridge to test
 * @param[in/out] vfio_device The device containing the DMA bridge to test
 * @param[in] h2c_channel_id Which channel to use for H2C transfers
 * @param[in] c2h_channel_id Which channel to use for C2H transfers
 * @return Returns true if the test passed, or false otherwise
 */
static bool test_stream_loopback_with_variable_transfers (const fpga_design_t *const design, vfio_device_t *const vfio_device,
                                                          const uint32_t h2c_channel_id, const uint32_t c2h_channel_id)
{
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    transfer_timing_t populate_test_pattern_timing;
    transfer_timing_t verify_test_pattern_timing;
    transfer_timing_t h2c_and_c2h_transfer_timing;
    bool success;

    /* Limit the transfer length to the maximum of the command line arguments and the mapping sizes.
     * Aligning down to a multiple of arg_stream_axi_width_bytes is done to avoid any issues when the mapping sizes
     * are different in each direction.
     * For C2H direction also have to limit to the maximum for one descriptor, since transfers terminated with EOP are
     * not allowed to span multiple descriptors as the API can only return a single transfer length. */
    const size_t max_h2c_transfer_length =
            (min_size_t (arg_h2c_transfer_length, arg_stream_h2c_mapping_size) / arg_stream_axi_width_bytes) *
            arg_stream_axi_width_bytes;
    const size_t max_c2h_transfer_length = (min_size_t (arg_c2h_transfer_length,
            (min_size_t (arg_stream_c2h_mapping_size, X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN)))) / arg_stream_axi_width_bytes *
                    arg_stream_axi_width_bytes;

    /* Populate the transfer configurations to be used.
     * The number of descriptors is set to the maximum. Each iteration may use a different number of descriptors
     * as wrap around the H2C and C2H buffers which may be different sizes. */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = X2X_SGDMA_MAX_DESCRIPTOR_CREDITS,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = h2c_channel_id,
        .bytes_per_buffer = 0, /* Length and offsets set before each each transfer */
        .host_buffer_start_offset = 0,
        .card_buffer_start_offset = 0,
        .c2h_stream_continuous = false,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &h2c_data_mapping,
        .overall_success = &success
    };

    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = X2X_SGDMA_MAX_DESCRIPTOR_CREDITS,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = c2h_channel_id,
        .bytes_per_buffer = 0, /* Length and offsets set before each each transfer */
        .host_buffer_start_offset = 0,
        .card_buffer_start_offset = 0,
        .c2h_stream_continuous = false,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &c2h_data_mapping,
        .overall_success = &success
    };

    /* Set the number of words in each iteration to the minimum of the mapping size for the host buffer for each direction,
     * so the transfer time may be taken. */
    const size_t h2c_mapping_size_words =
            ((arg_stream_h2c_mapping_size / arg_stream_axi_width_bytes) * arg_stream_axi_width_bytes) / sizeof (uint32_t);
    const size_t h2c_mapping_size_bytes = h2c_mapping_size_words * sizeof (uint32_t);
    const size_t c2h_mapping_size_words =
            ((arg_stream_c2h_mapping_size / arg_stream_axi_width_bytes) * arg_stream_axi_width_bytes) / sizeof (uint32_t);
    const size_t c2h_mapping_size_bytes = c2h_mapping_size_words * sizeof (uint32_t);
    const size_t num_bytes_per_iteration = min_size_t (h2c_mapping_size_bytes, c2h_mapping_size_bytes);
    const size_t num_words_per_iteration = num_bytes_per_iteration / sizeof (uint32_t);

    /* Defines the transfers which are used for a test iteration, in terms of the offsets and lengths.
     * Dynamically allocated lengths as the actual number of transfers may vary between iterations as a result
     * of transfers wrapping around the length of the mappings. */
    typedef struct
    {
        size_t transfer_len;
        uint64_t host_buffer_offset;
    } tx_transfer_t;
    tx_transfer_t *tx_transfers = NULL;
    uint32_t num_tx_transfers = 0;
    uint32_t tx_transfers_allocated_length = 0;

    typedef struct
    {
        size_t transfer_len;
        uint64_t host_buffer_offset;
        bool end_of_packet;
    } rx_transfer_t;
    rx_transfer_t *rx_transfers = NULL;
    uint32_t num_rx_transfers = 0;
    uint32_t rx_transfers_allocated_length = 0;

    const uint32_t transfers_grow_len = 64;

    printf ("\nTesting streams with variable size buffers:\n");
    printf ("  H2C mapping size 0x%zx max transfer length 0x%zx channel ID %u\n",
            arg_stream_h2c_mapping_size, max_h2c_transfer_length, h2c_channel_id);
    printf ("  C2H mapping size 0x%zx max transfer length 0x%zx channel ID %u\n",
            arg_stream_c2h_mapping_size, max_c2h_transfer_length, c2h_channel_id);

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    /* Read mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, h2c_mapping_size_bytes,
            VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

    /* Write mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, c2h_mapping_size_bytes,
            VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    success = (descriptors_mapping.buffer.vaddr != NULL) &&
              (h2c_data_mapping.buffer.vaddr    != NULL) &&
              (c2h_data_mapping.buffer.vaddr    != NULL);

    if (success)
    {
        uint32_t tx_test_pattern = 0;
        uint32_t rx_test_pattern = 0;
        uint32_t *const tx_words = h2c_data_mapping.buffer.vaddr;
        const uint32_t *const rx_words = c2h_data_mapping.buffer.vaddr;
        size_t tx_test_word_index = 0;
        size_t rx_test_word_index = 0;
        size_t tx_transfer_start_buffer_offset = 0;
        size_t rx_transfer_start_buffer_offset = 0;
        size_t h2c_num_bytes_transfer_defined;
        uint32_t h2c_num_transfers_started;
        uint32_t h2c_num_transfers_completed;
        uint32_t c2h_num_transfers_started;
        uint32_t c2h_num_transfers_completed;
        size_t word_offset;
        bool no_available_buffer;
        uint32_t *h2c_buffer;
        uint32_t *c2h_buffer;
        size_t transfer_len;
        bool end_of_packet;

        initialise_transfer_timing (&populate_test_pattern_timing, "populate test pattern", num_bytes_per_iteration);
        initialise_transfer_timing (&verify_test_pattern_timing, "verify test pattern", num_bytes_per_iteration);
        initialise_transfer_timing (&h2c_and_c2h_transfer_timing, "host-to-card and card-to-host DMA", num_bytes_per_iteration);

        /* Initialise the transfers */
        x2x_initialise_transfer_context (&h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&c2h_transfer, &c2h_transfer_configuration);

        /* Perform test iterations to exercise all values of 32-bit test words */
        for (size_t total_words = 0; success && (total_words < 0x100000000UL); total_words += num_words_per_iteration)
        {
            /* Determine the transmit transfers to be used for the iteration, which may wrap around the H2C buffer */
            num_tx_transfers = 0;
            h2c_num_bytes_transfer_defined = 0;
            while (h2c_num_bytes_transfer_defined < num_bytes_per_iteration)
            {
                const size_t remaining_buffer_bytes =
                        min_size_t (num_bytes_per_iteration - h2c_num_bytes_transfer_defined,
                                    h2c_mapping_size_bytes - tx_transfer_start_buffer_offset);

                transfer_len = min_size_t (remaining_buffer_bytes, max_h2c_transfer_length);
                if (num_tx_transfers == tx_transfers_allocated_length)
                {
                    tx_transfers_allocated_length += transfers_grow_len;
                    tx_transfers = realloc (tx_transfers, tx_transfers_allocated_length * sizeof (tx_transfers[0]));
                }
                tx_transfer_t *const tx_transfer = &tx_transfers[num_tx_transfers];

                tx_transfer->transfer_len = transfer_len;
                tx_transfer->host_buffer_offset = tx_transfer_start_buffer_offset;
                h2c_num_bytes_transfer_defined += transfer_len;
                tx_transfer_start_buffer_offset = (tx_transfer_start_buffer_offset + transfer_len) % h2c_mapping_size_bytes;
                num_tx_transfers++;
            }

            /* Determine the receive transfers to be used for the iteration.
             * For each transmit transfer, which is terminated by end-of-packet, define one or more receive transfers
             * for the complete transmit transfer allowing for wrapping around the C2H buffer.
             *
             * Since each transmit transfer is terminated by end-of-packet, if arg_h2c_transfer_length is less than
             * arg_c2h_transfer_length has the effect of reducing the C2H transfer length over that requested by the
             * command line arguments. */
            num_rx_transfers = 0;
            for (uint32_t tx_transfer_index = 0; tx_transfer_index < num_tx_transfers; tx_transfer_index++)
            {
                size_t remaining_tx_transfer_bytes = tx_transfers[tx_transfer_index].transfer_len;

                while (remaining_tx_transfer_bytes > 0)
                {
                    const size_t num_bytes_to_end_of_buffer = c2h_mapping_size_bytes - rx_transfer_start_buffer_offset;

                    transfer_len = min_size_t (min_size_t (num_bytes_to_end_of_buffer, max_c2h_transfer_length),
                            remaining_tx_transfer_bytes);
                    if (num_rx_transfers == rx_transfers_allocated_length)
                    {
                        rx_transfers_allocated_length += transfers_grow_len;
                        rx_transfers = realloc (rx_transfers, rx_transfers_allocated_length * sizeof (rx_transfers[0]));
                    }
                    rx_transfer_t *const rx_transfer = &rx_transfers[num_rx_transfers];

                    rx_transfer->transfer_len = transfer_len;
                    rx_transfer->host_buffer_offset = rx_transfer_start_buffer_offset;
                    remaining_tx_transfer_bytes -= transfer_len;
                    rx_transfer->end_of_packet = remaining_tx_transfer_bytes == 0;
                    rx_transfer_start_buffer_offset = (rx_transfer_start_buffer_offset + transfer_len) % c2h_mapping_size_bytes;
                    num_rx_transfers++;
                }
            }

            /* Populate the transmit words with the pattern for the iteration, which may wrap around */
            transfer_time_start (&populate_test_pattern_timing);
            for (word_offset = 0; word_offset < num_words_per_iteration; word_offset++)
            {
                tx_words[tx_test_word_index] = tx_test_pattern;
                linear_congruential_generator (&tx_test_pattern);
                tx_test_word_index++;
                if (tx_test_word_index == h2c_mapping_size_words)
                {
                    tx_test_word_index = 0;
                }
            }
            transfer_time_stop (&populate_test_pattern_timing);

            /* Perform the H2C and C2H transfers for all of the words for one iteration */
            h2c_num_transfers_started = 0;
            h2c_num_transfers_completed = 0;
            c2h_num_transfers_started = 0;
            c2h_num_transfers_completed = 0;
            transfer_time_start (&h2c_and_c2h_transfer_timing);
            while (success && ((h2c_num_transfers_completed < num_tx_transfers) || (c2h_num_transfers_completed < num_rx_transfers)))
            {
                /* Start all possible C2H transfers */
                no_available_buffer = false;
                while (success && !no_available_buffer && (c2h_num_transfers_started < num_rx_transfers))
                {
                    const rx_transfer_t *const rx_transfer = &rx_transfers[c2h_num_transfers_started];

                    c2h_buffer = x2x_populate_stream_transfer (&c2h_transfer,
                            rx_transfer->transfer_len, rx_transfer->host_buffer_offset);
                    if (c2h_buffer != NULL)
                    {
                        x2x_start_populated_descriptors (&c2h_transfer);
                        c2h_num_transfers_started++;
                    }
                    else
                    {
                        no_available_buffer = true;
                    }
                }

                /* Start all possible H2C transfers */
                no_available_buffer = false;
                while (success && !no_available_buffer && (h2c_num_transfers_started < num_tx_transfers))
                {
                    const tx_transfer_t *const tx_transfer = &tx_transfers[h2c_num_transfers_started];

                    h2c_buffer = x2x_populate_stream_transfer (&h2c_transfer,
                            tx_transfer->transfer_len, tx_transfer->host_buffer_offset);
                    if (h2c_buffer != NULL)
                    {
                        x2x_start_populated_descriptors (&h2c_transfer);
                        h2c_num_transfers_started++;
                    }
                    else
                    {
                        no_available_buffer = true;
                    }
                }

                /* Poll for completion of H2C transfers */
                h2c_buffer = x2x_poll_completed_transfer (&h2c_transfer, &transfer_len, NULL);
                if (h2c_buffer != NULL)
                {
                    const tx_transfer_t *const tx_transfer = &tx_transfers[h2c_num_transfers_completed];

                    X2X_ASSERT (&h2c_transfer, transfer_len == tx_transfer->transfer_len);
                    X2X_ASSERT (&h2c_transfer, h2c_buffer == &tx_words[tx_transfer->host_buffer_offset / sizeof (uint32_t)]);
                    h2c_num_transfers_completed++;
                }

                /* Poll for completion of C2H transfers */
                c2h_buffer = x2x_poll_completed_transfer (&c2h_transfer, &transfer_len, &end_of_packet);
                if (c2h_buffer != NULL)
                {
                    const rx_transfer_t *const rx_transfer = &rx_transfers[c2h_num_transfers_completed];

                    X2X_ASSERT (&c2h_transfer, transfer_len == rx_transfer->transfer_len);
                    X2X_ASSERT (&c2h_transfer, end_of_packet == rx_transfer->end_of_packet);
                    X2X_ASSERT (&c2h_transfer, c2h_buffer == &rx_words[rx_transfer->host_buffer_offset / sizeof (uint32_t)]);
                    c2h_num_transfers_completed++;
                }
            }
            transfer_time_stop (&h2c_and_c2h_transfer_timing);

            /* Verify the receive words */
            transfer_time_start (&verify_test_pattern_timing);
            for (word_offset = 0; success && (word_offset < num_words_per_iteration); word_offset++)
            {
                X2X_ASSERT (&c2h_transfer, rx_words[rx_test_word_index] == rx_test_pattern);
                linear_congruential_generator (&rx_test_pattern);
                rx_test_word_index++;
                if (rx_test_word_index == c2h_mapping_size_words)
                {
                    rx_test_word_index = 0;
                }
            }
            transfer_time_stop (&verify_test_pattern_timing);
        }

        x2x_finalise_transfer_context (&h2c_transfer);
        x2x_finalise_transfer_context (&c2h_transfer);

        if (success)
        {
            display_transfer_timing_statistics (&populate_test_pattern_timing);
            display_transfer_timing_statistics (&h2c_and_c2h_transfer_timing);
            display_transfer_timing_statistics (&verify_test_pattern_timing);
            printf ("TEST PASS\n");
        }
        else
        {
            printf ("TEST FAIL:\n");
            report_if_transfer_failed (&h2c_transfer);
            report_if_transfer_failed (&c2h_transfer);
        }
    }
    else
    {
        printf ("TEST FAIL : allocate_vfio_dma_mapping()\n");
    }

    free_vfio_dma_mapping (&c2h_data_mapping);
    free_vfio_dma_mapping (&h2c_data_mapping);
    free_vfio_dma_mapping (&descriptors_mapping);
    free (tx_transfers);
    free (rx_transfers);

    return success;
}


/**
 * @brief Perform a write/read test of DMA accessible memory using a pair of channels, using fixed size buffers
 * @param[in] design The design containing the DMA bridge to test
 * @param[in/out] vfio_device The device containing the DMA bridge to test
 * @param[in] h2c_channel_id Which channel to use for H2C transfers
 * @param[in] c2h_channel_id Which channel to use for C2H transfers
 * @return Returns true if the test passed, or false otherwise
 */
static bool test_dma_accessible_memory_with_fixed_buffers (const fpga_design_t *const design, vfio_device_t *const vfio_device,
                                                           const uint32_t h2c_channel_id, const uint32_t c2h_channel_id)
{
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    transfer_timing_t populate_test_pattern_timing;
    transfer_timing_t verify_test_pattern_timing;
    transfer_timing_t h2c_and_c2h_transfer_timing;
    uint32_t num_descriptors;
    size_t bytes_per_buffer;
    bool success;

    /* Determine the number and size of each buffer used for the test */
    if (design->dma_bridge_memory_size_bytes < arg_max_buffer_size)
    {
        /* Can use a single buffer for the entire DMA accessible memory */
        bytes_per_buffer = design->dma_bridge_memory_size_bytes;
        num_descriptors = 1;
    }
    else
    {
        /* Calculate the number of bytes per buffer, which needs to be a multiple of words */
        bytes_per_buffer = (arg_max_buffer_size / sizeof (uint32_t)) * sizeof (uint32_t);
        num_descriptors = (uint32_t) (design->dma_bridge_memory_size_bytes / bytes_per_buffer);
    }

    /* Populate the transfer configurations to be used */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = num_descriptors,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = h2c_channel_id,
        .bytes_per_buffer = bytes_per_buffer,
        .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
        .card_buffer_start_offset = 0, /* All of the card memory is tested */
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &h2c_data_mapping,
        .overall_success = &success
    };

    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = num_descriptors,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = c2h_channel_id,
        .bytes_per_buffer = bytes_per_buffer,
        .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
        .card_buffer_start_offset = 0, /* All of the card memory is tested */
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &c2h_data_mapping,
        .overall_success = &success
    };

    /* Allocate storage for the pointers to each host buffer, to validate that buffers are returned in the expected order */
    uint32_t **const tx_buffers = calloc (num_descriptors, sizeof (tx_buffers[0]));
    uint32_t **const rx_buffers = calloc (num_descriptors, sizeof (rx_buffers[0]));

    printf ("\nTesting using %u buffers of size 0x%zx bytes, H2C channel %u C2H channel %u\n",
            num_descriptors, bytes_per_buffer, h2c_channel_id, c2h_channel_id);

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    /* Read mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, design->dma_bridge_memory_size_bytes,
            VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

    /* Write mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, design->dma_bridge_memory_size_bytes,
            VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    success = (descriptors_mapping.buffer.vaddr != NULL) &&
              (h2c_data_mapping.buffer.vaddr    != NULL) &&
              (c2h_data_mapping.buffer.vaddr    != NULL);

    if (success)
    {
        uint32_t host_test_pattern = 0;
        uint32_t card_test_pattern = 0;
        uint32_t *const host_words = h2c_data_mapping.buffer.vaddr;
        const uint32_t *const card_words = c2h_data_mapping.buffer.vaddr;
        size_t word_index;
        uint32_t h2c_started_buffer_index;
        uint32_t h2c_completed_buffer_index;
        uint32_t c2h_completed_buffer_index;
        uint32_t *h2c_buffer;
        uint32_t *c2h_buffer;
        size_t transfer_len;
        const size_t ddr_size_bytes = num_descriptors * bytes_per_buffer;
        const size_t ddr_size_words = ddr_size_bytes / sizeof (uint32_t);

        initialise_transfer_timing (&populate_test_pattern_timing, "populate test pattern", ddr_size_bytes);
        initialise_transfer_timing (&verify_test_pattern_timing, "verify test pattern", ddr_size_bytes);
        initialise_transfer_timing (&h2c_and_c2h_transfer_timing, "host-to-card and card-to-host DMA", ddr_size_bytes);

        /* Initialise the transfers */
        x2x_initialise_transfer_context (&h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&c2h_transfer, &c2h_transfer_configuration);

        /* Perform test iterations to exercise all values of 32-bit test words */
        for (size_t total_words = 0; success && (total_words < 0x100000000UL); total_words += ddr_size_words)
        {
            /* Fill all host buffers with the next test pattern */
            transfer_time_start (&populate_test_pattern_timing);
            for (word_index = 0; word_index < ddr_size_words; word_index++)
            {
                host_words[word_index] = host_test_pattern;
                linear_congruential_generator (&host_test_pattern);
            }
            transfer_time_stop (&populate_test_pattern_timing);

            /* Perform the H2C and C2H transfers for all buffers (descriptors) which cover the DMA accessible memory.
             * Attempts to overlap transfers in both directions:
             * a. H2C transfers can be started as soon as possible
             * b. As each H2C transfer completes, the corresponding C2H transfer can be started.
             *
             * Due to potential overlapping transfers only records the timing across all H2C and C2H transfers. */
            h2c_started_buffer_index = 0;
            h2c_completed_buffer_index = 0;
            c2h_completed_buffer_index = 0;
            transfer_time_start (&h2c_and_c2h_transfer_timing);
            while (success && (c2h_completed_buffer_index < num_descriptors))
            {
                /* H2C transfers can be started as soon as possible since all host buffers have been filled with the test pattern */
                if (h2c_started_buffer_index < num_descriptors)
                {
                    h2c_buffer = x2x_get_next_h2c_buffer (&h2c_transfer);
                    if (h2c_buffer != NULL)
                    {
                        x2x_start_populated_descriptors (&h2c_transfer);
                        h2c_started_buffer_index++;
                    }
                }

                /* Poll for completion of H2C transfers, and as each completes start the corresponding C2H transfer */
                h2c_buffer = x2x_poll_completed_transfer (&h2c_transfer, &transfer_len, NULL);
                if (h2c_buffer != NULL)
                {
                    X2X_ASSERT (&h2c_transfer, transfer_len == bytes_per_buffer);
                    tx_buffers[h2c_completed_buffer_index] = h2c_buffer;
                    h2c_completed_buffer_index++;
                    x2x_start_next_c2h_buffer (&c2h_transfer);
                }

                /* Poll for completion of C2H transfers */
                c2h_buffer = x2x_poll_completed_transfer (&c2h_transfer, &transfer_len, NULL);
                if (c2h_buffer != NULL)
                {
                    X2X_ASSERT (&c2h_transfer, transfer_len == bytes_per_buffer);
                    rx_buffers[c2h_completed_buffer_index] = c2h_buffer;
                    c2h_completed_buffer_index++;
                }
            }
            transfer_time_stop (&h2c_and_c2h_transfer_timing);

            /* Check the buffer pointers returned were correct */
            for (uint32_t buffer_index = 0; success && (buffer_index < num_descriptors); buffer_index++)
            {
                word_index = buffer_index * (bytes_per_buffer / sizeof (uint32_t));
                X2X_ASSERT (&h2c_transfer, tx_buffers[buffer_index] == &host_words[word_index]);
                X2X_ASSERT (&c2h_transfer, rx_buffers[buffer_index] == &card_words[word_index]);
            }

            /* Verify that all card buffers have the expected contents */
            transfer_time_start (&verify_test_pattern_timing);
            for (word_index = 0; success && word_index < ddr_size_words; word_index++)
            {
                if (card_words[word_index] != card_test_pattern)
                {
                    x2x_record_failure (&c2h_transfer, "DDR word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32,
                            word_index, card_words[word_index], card_test_pattern);
                    success = false;
                }
                linear_congruential_generator (&card_test_pattern);
            }
            transfer_time_stop (&verify_test_pattern_timing);
        }

        x2x_finalise_transfer_context (&h2c_transfer);
        x2x_finalise_transfer_context (&c2h_transfer);

        if (success)
        {
            display_transfer_timing_statistics (&populate_test_pattern_timing);
            display_transfer_timing_statistics (&h2c_and_c2h_transfer_timing);
            display_transfer_timing_statistics (&verify_test_pattern_timing);
            printf ("TEST PASS\n");
        }
        else
        {
            printf ("TEST FAIL:\n");
            report_if_transfer_failed (&h2c_transfer);
            report_if_transfer_failed (&c2h_transfer);
        }
    }
    else
    {
        printf ("TEST FAIL : allocate_vfio_dma_mapping()\n");
    }

    free_vfio_dma_mapping (&c2h_data_mapping);
    free_vfio_dma_mapping (&h2c_data_mapping);
    free_vfio_dma_mapping (&descriptors_mapping);
    free (tx_buffers);
    free (rx_buffers);

    return success;
}


/**
 * @brief Perform a write/read test of DMA accessible memory using a pair of channels, using variable size transfers
 * @param[in] design The design containing the DMA bridge to test
 * @param[in/out] vfio_device The device containing the DMA bridge to test
 * @param[in] h2c_channel_id Which channel to use for H2C transfers
 * @param[in] c2h_channel_id Which channel to use for C2H transfers
 * @return Returns true if the test passed, or false otherwise
 */
static bool test_dma_accessible_memory_with_variable_transfers (const fpga_design_t *const design, vfio_device_t *const vfio_device,
                                                                const uint32_t h2c_channel_id, const uint32_t c2h_channel_id)
{
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    transfer_timing_t populate_test_pattern_timing;
    transfer_timing_t verify_test_pattern_timing;
    transfer_timing_t h2c_and_c2h_transfer_timing;
    bool success;

    /* Limit the transfer length to the maximum of the command line arguments and the card memory */
    const size_t h2c_transfer_length = min_size_t (arg_h2c_transfer_length, design->dma_bridge_memory_size_bytes);
    const size_t c2h_transfer_length = min_size_t (arg_c2h_transfer_length, design->dma_bridge_memory_size_bytes);

    /* Calculate the number of descriptors, to try and allow all transfers for the entire card memory to be queued at once.
     * For "small" transfer sizes, limits the maximum number of descriptors to the maximum supported by the DMA engine.
     *
     * With the maximum number of descriptors in use can support a transfer of up to 255 GiB so assumes a single transfer
     * will be sufficient to address all card memory, and so doesn't have to limit the maximum transfer size. */
    const uint32_t num_descriptors_per_h2c_transfer = x2x_num_descriptors_for_transfer_len (h2c_transfer_length);
    const uint32_t num_h2c_transfers_per_iteration = (uint32_t)
            ((design->dma_bridge_memory_size_bytes + (h2c_transfer_length - 1)) / h2c_transfer_length);
    const uint32_t num_h2c_descriptors =
            min_uint32_t (num_h2c_transfers_per_iteration * num_descriptors_per_h2c_transfer, X2X_SGDMA_MAX_DESCRIPTOR_CREDITS);

    const uint32_t num_descriptors_per_c2h_transfer = x2x_num_descriptors_for_transfer_len (c2h_transfer_length);
    const uint32_t num_c2h_transfers_per_iteration = (uint32_t)
            ((design->dma_bridge_memory_size_bytes + (c2h_transfer_length - 1)) / c2h_transfer_length);
    const uint32_t num_c2h_descriptors =
            min_uint32_t (num_c2h_transfers_per_iteration * num_descriptors_per_c2h_transfer, X2X_SGDMA_MAX_DESCRIPTOR_CREDITS);

    /* Populate the transfer configurations to be used */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = num_h2c_descriptors,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = h2c_channel_id,
        .bytes_per_buffer = 0, /* Length and offsets set before each each transfer */
        .host_buffer_start_offset = 0,
        .card_buffer_start_offset = 0,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &h2c_data_mapping,
        .overall_success = &success
    };

    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = num_c2h_descriptors,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = c2h_channel_id,
        .bytes_per_buffer = 0, /* Length and offsets set before each each transfer */
        .host_buffer_start_offset = 0,
        .card_buffer_start_offset = 0,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &c2h_data_mapping,
        .overall_success = &success
    };

    /* Allocate storage for the pointers to each host buffer, to validate that buffers are returned in the expected order */
    uint32_t **const tx_buffers = calloc (num_h2c_transfers_per_iteration, sizeof (tx_buffers[0]));
    uint32_t **const rx_buffers = calloc (num_c2h_transfers_per_iteration, sizeof (rx_buffers[0]));

    printf ("\nTesting using:\n");
    printf ("  H2C channel %u transfer length 0x%zx bytes with %u descriptors\n",
            h2c_channel_id, h2c_transfer_length, num_h2c_descriptors);
    printf ("  C2H channel %u transfer length 0x%zx bytes with %u descriptors\n",
            c2h_channel_id, c2h_transfer_length, num_c2h_descriptors);

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    /* Read mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, design->dma_bridge_memory_size_bytes,
            VFIO_DMA_MAP_FLAG_READ, arg_buffer_allocation);

    /* Write mapping used by device, for the entire card memory */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, design->dma_bridge_memory_size_bytes,
            VFIO_DMA_MAP_FLAG_WRITE, arg_buffer_allocation);

    success = (descriptors_mapping.buffer.vaddr != NULL) &&
              (h2c_data_mapping.buffer.vaddr    != NULL) &&
              (c2h_data_mapping.buffer.vaddr    != NULL);

    if (success)
    {
        uint32_t host_test_pattern = 0;
        uint32_t card_test_pattern = 0;
        uint32_t *const host_words = h2c_data_mapping.buffer.vaddr;
        const uint32_t *const card_words = c2h_data_mapping.buffer.vaddr;
        size_t transfer_len;
        size_t h2c_num_bytes_transfer_started;
        size_t h2c_num_bytes_transfer_completed;
        uint32_t h2c_completed_buffer_index;
        size_t c2h_num_bytes_transfer_started;
        size_t c2h_num_bytes_transfer_completed;
        uint32_t c2h_completed_buffer_index;
        size_t word_index;
        uint32_t *h2c_buffer;
        uint32_t *c2h_buffer;
        bool no_available_buffer;
        bool attempt_further_transfer;
        uint32_t buffer_index;
        const size_t ddr_size_words = design->dma_bridge_memory_size_bytes / sizeof (uint32_t);

        initialise_transfer_timing (&populate_test_pattern_timing, "populate test pattern", design->dma_bridge_memory_size_bytes);
        initialise_transfer_timing (&verify_test_pattern_timing, "verify test pattern", design->dma_bridge_memory_size_bytes);
        initialise_transfer_timing (&h2c_and_c2h_transfer_timing, "host-to-card and card-to-host DMA", design->dma_bridge_memory_size_bytes);

        /* Initialise the transfers */
        x2x_initialise_transfer_context (&h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&c2h_transfer, &c2h_transfer_configuration);

        /* Perform test iterations to exercise all values of 32-bit test words */
        for (size_t total_words = 0; success && (total_words < 0x100000000UL); total_words += ddr_size_words)
        {
            /* Fill all host buffers with the next test pattern */
            transfer_time_start (&populate_test_pattern_timing);
            for (word_index = 0; word_index < ddr_size_words; word_index++)
            {
                host_words[word_index] = host_test_pattern;
                linear_congruential_generator (&host_test_pattern);
            }
            transfer_time_stop (&populate_test_pattern_timing);

            /* Perform the H2C and C2H transfers for all buffers (descriptors) which cover the DMA accessible memory.
             * Attempts to overlap transfers in both directions:
             * a. H2C transfers can be started as soon as possible.
             * b. C2H transfers can be started once the card memory has been written to by H2C transfers.
             *    The logic allows for H2C and C2H directions to use different transfers lengths, and the number of
             *    completed bytes of H2C transfers is used to determine when C2H transfers can be started.
             *
             * Due to potential overlapping transfers only records the timing across all H2C and C2H transfers. */
            h2c_num_bytes_transfer_started = 0;
            h2c_num_bytes_transfer_completed = 0;
            h2c_completed_buffer_index = 0;
            c2h_num_bytes_transfer_started = 0;
            c2h_num_bytes_transfer_completed = 0;
            c2h_completed_buffer_index = 0;
            transfer_time_start (&h2c_and_c2h_transfer_timing);
            while (success && (c2h_num_bytes_transfer_completed < design->dma_bridge_memory_size_bytes))
            {
                /* Start the H2C transfers for the entire card memory as soon as there are available descriptors */
                no_available_buffer = false;
                while (success && (h2c_num_bytes_transfer_started < design->dma_bridge_memory_size_bytes) && !no_available_buffer)
                {
                    const size_t remaining_bytes = design->dma_bridge_memory_size_bytes - h2c_num_bytes_transfer_started;

                    transfer_len = min_size_t (remaining_bytes, h2c_transfer_length);
                    h2c_buffer = x2x_populate_memory_transfer (&h2c_transfer, transfer_len,
                            h2c_num_bytes_transfer_started, h2c_num_bytes_transfer_started);
                    if (h2c_buffer != NULL)
                    {
                        x2x_start_populated_descriptors (&h2c_transfer);
                        h2c_num_bytes_transfer_started += transfer_len;
                    }
                    else
                    {
                        no_available_buffer = true;
                    }
                }

                /* Poll for completion of H2C transfers */
                h2c_buffer = x2x_poll_completed_transfer (&h2c_transfer, &transfer_len, NULL);
                if (h2c_buffer != NULL)
                {
                    const size_t remaining_bytes = design->dma_bridge_memory_size_bytes - h2c_num_bytes_transfer_completed;
                    const size_t expected_transfer_len = min_size_t (remaining_bytes, h2c_transfer_length);;

                    X2X_ASSERT (&h2c_transfer, transfer_len == expected_transfer_len);
                    tx_buffers[h2c_completed_buffer_index] = h2c_buffer;
                    h2c_completed_buffer_index++;
                    h2c_num_bytes_transfer_completed += transfer_len;
                }

                /* Start the C2H transfers which encompass the range of card memory which has been written to by the
                 * completed H2C transfers */
                attempt_further_transfer = true;
                while (success && (c2h_num_bytes_transfer_started < design->dma_bridge_memory_size_bytes) &&
                       attempt_further_transfer)
                {
                    const size_t remaining_bytes = design->dma_bridge_memory_size_bytes - c2h_num_bytes_transfer_started;
                    const size_t bytes_pending_c2h_transfer = h2c_num_bytes_transfer_completed - c2h_num_bytes_transfer_started;

                    attempt_further_transfer = false;
                    transfer_len = min_size_t (remaining_bytes, c2h_transfer_length);
                    if (bytes_pending_c2h_transfer >= transfer_len)
                    {
                        c2h_buffer = x2x_populate_memory_transfer (&c2h_transfer, transfer_len,
                                c2h_num_bytes_transfer_started, c2h_num_bytes_transfer_started);
                        if (c2h_buffer != NULL)
                        {
                            x2x_start_populated_descriptors (&c2h_transfer);
                            c2h_num_bytes_transfer_started += transfer_len;
                            attempt_further_transfer = true;
                        }
                    }
                }

                /* Poll for completion of C2H transfers */
                c2h_buffer = x2x_poll_completed_transfer (&c2h_transfer, &transfer_len, NULL);
                if (c2h_buffer != NULL)
                {
                    const size_t remaining_bytes = design->dma_bridge_memory_size_bytes - c2h_num_bytes_transfer_completed;
                    const size_t expected_transfer_len = min_size_t (remaining_bytes, c2h_transfer_length);

                    X2X_ASSERT (&c2h_transfer, transfer_len == expected_transfer_len);
                    rx_buffers[c2h_completed_buffer_index] = c2h_buffer;
                    c2h_completed_buffer_index++;
                    c2h_num_bytes_transfer_completed += transfer_len;
                }
            }
            transfer_time_stop (&h2c_and_c2h_transfer_timing);

            /* Check the buffer pointers returned were correct */
            X2X_ASSERT (&h2c_transfer, h2c_completed_buffer_index == num_h2c_transfers_per_iteration);
            for (buffer_index = 0; success && (buffer_index < num_h2c_transfers_per_iteration); buffer_index++)
            {
                word_index = buffer_index * (h2c_transfer_length / sizeof (uint32_t));
                X2X_ASSERT (&h2c_transfer, tx_buffers[buffer_index] == &host_words[word_index]);
            }
            X2X_ASSERT (&c2h_transfer, c2h_completed_buffer_index == num_c2h_transfers_per_iteration);
            for (buffer_index = 0; success && (buffer_index < num_c2h_transfers_per_iteration); buffer_index++)
            {
                word_index = buffer_index * (c2h_transfer_length / sizeof (uint32_t));
                X2X_ASSERT (&c2h_transfer, rx_buffers[buffer_index] == &card_words[word_index]);
            }

            /* Verify that all card buffers have the expected contents */
            transfer_time_start (&verify_test_pattern_timing);
            for (word_index = 0; success && word_index < ddr_size_words; word_index++)
            {
                if (card_words[word_index] != card_test_pattern)
                {
                    x2x_record_failure (&c2h_transfer, "DDR word[%zu] actual=0x%" PRIx32 " expected=0x%" PRIx32,
                            word_index, card_words[word_index], card_test_pattern);
                    success = false;
                }
                linear_congruential_generator (&card_test_pattern);
            }
            transfer_time_stop (&verify_test_pattern_timing);
        }

        x2x_finalise_transfer_context (&h2c_transfer);
        x2x_finalise_transfer_context (&c2h_transfer);

        if (success)
        {
            display_transfer_timing_statistics (&populate_test_pattern_timing);
            display_transfer_timing_statistics (&h2c_and_c2h_transfer_timing);
            display_transfer_timing_statistics (&verify_test_pattern_timing);
            printf ("TEST PASS\n");
        }
        else
        {
            printf ("TEST FAIL:\n");
            report_if_transfer_failed (&h2c_transfer);
            report_if_transfer_failed (&c2h_transfer);
        }
    }
    else
    {
        printf ("TEST FAIL : allocate_vfio_dma_mapping()\n");
    }

    free_vfio_dma_mapping (&c2h_data_mapping);
    free_vfio_dma_mapping (&h2c_data_mapping);
    free_vfio_dma_mapping (&descriptors_mapping);
    free (tx_buffers);
    free (rx_buffers);

    return success;
}


/**
 * @brief Perform one DMA bridge test which is enabled and supported by a design
 * @param[in] dma_test Which test to perform
 * @param[in] design The design containing the DMA bridge to test
 * @param[in/out] vfio_device The device containing the DMA bridge to test
 * @param[in] h2c_channel_id Which channel to use for H2C transfers
 * @param[in] c2h_channel_id Which channel to use for C2H transfers
 * @return Returns true if the test passed, or false otherwise
 */
static bool perform_enabled_test (const dma_test_t dma_test,
                                  const fpga_design_t *const design, vfio_device_t *const vfio_device,
                                  const uint32_t h2c_channel_id, const uint32_t c2h_channel_id)
{
    bool c2h_stream_continuous;
    bool success;

    switch (dma_test)
    {
    case DMA_TEST_MEMORY_FIXED_BUFFERS:
        success = test_dma_accessible_memory_with_fixed_buffers (design, vfio_device, h2c_channel_id, c2h_channel_id);
        break;

    case DMA_TEST_STREAM_FIXED_BUFFERS:
        c2h_stream_continuous = false;
        success = test_stream_loopback_with_fixed_buffers (design, vfio_device, h2c_channel_id, c2h_channel_id,
                c2h_stream_continuous);
        break;

    case DMA_TEST_STREAM_FIXED_BUFFERS_C2H_CONTINUOUS:
        c2h_stream_continuous = true;
        success = test_stream_loopback_with_fixed_buffers (design, vfio_device, h2c_channel_id, c2h_channel_id,
                c2h_stream_continuous);
        break;

    case DMA_TEST_MEMORY_VARIABLE_TRANSFERS:
        success = test_dma_accessible_memory_with_variable_transfers (design, vfio_device, h2c_channel_id, c2h_channel_id);
        break;

    case DMA_TEST_STREAM_VARIABLE_TRANSFERS:
        success = test_stream_loopback_with_variable_transfers (design, vfio_device, h2c_channel_id, c2h_channel_id);
        break;

    default:
        /* Shouldn't get here */
        success = false;
        break;
    }

    return success;
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    uint32_t design_index;
    uint32_t num_h2c_channels;
    uint32_t num_c2h_channels;
    uint32_t h2c_channel_id;
    uint32_t c2h_channel_id;
    uint32_t num_channel_combinations_tested;
    bool test_success;
    bool overall_success = true;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Optionally test A32 DMA capability */
    if (arg_test_a32_dma_capability)
    {
        for (design_index = 0; design_index < designs.num_identified_designs; design_index++)
        {
            if (designs.designs[design_index].dma_bridge_present)
            {
                designs.vfio_devices.devices[design_index].dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A32;
            }
        }
    }

    /* Process any FPGA designs which have a DMA bridge */
    for (design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];
        vfio_device_t *const vfio_device = &designs.vfio_devices.devices[design_index];

        if (design->dma_bridge_present)
        {
            const bool design_uses_stream = design->dma_bridge_memory_size_bytes == 0;

            x2x_get_num_channels (vfio_device, design->dma_bridge_bar, design->dma_bridge_memory_size_bytes,
                    &num_h2c_channels, &num_c2h_channels);
            if ((num_h2c_channels > 0) && (num_c2h_channels > 0))
            {
                /* Perform all enabled tests which are supported by the design */
                for (dma_test_t dma_test = 0; dma_test < DMA_TEST_ARRAY_SIZE; dma_test++)
                {
                    if (arg_enabled_tests[dma_test] && (dma_test_uses_stream[dma_test] == design_uses_stream))
                    {
                        if (design_uses_stream)
                        {
                            printf ("Testing %s design", fpga_design_names[design->design_id]);
                            if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
                            {
                                printf (" version 0x%x", design->board_version);
                            }
                            printf (" with AXI stream\n");
                            printf ("PCI device %s IOMMU group %s\n", design->vfio_device->device_name, design->vfio_device->iommu_group);

                            /* Test the pairs of streams cross-connected within the FPGA */
                            num_channel_combinations_tested = 0;
                            for (h2c_channel_id = 0;
                                 (h2c_channel_id < num_h2c_channels) && (num_channel_combinations_tested < arg_max_channel_combinations);
                                 h2c_channel_id++)
                            {
                                if ((h2c_channel_id & 1) == 1)
                                {
                                    c2h_channel_id = h2c_channel_id - 1;
                                }
                                else
                                {
                                    c2h_channel_id = (h2c_channel_id + 1) % num_c2h_channels;
                                }
                                test_success = perform_enabled_test (dma_test, design, vfio_device,
                                        h2c_channel_id, c2h_channel_id);
                                overall_success = overall_success && test_success;
                                num_channel_combinations_tested++;
                            }
                        }
                        else
                        {
                            printf ("Testing %s design", fpga_design_names[design->design_id]);
                            if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
                            {
                                printf (" version 0x%x", design->board_version);
                            }
                            printf (" with memory size 0x%zx\n", design->dma_bridge_memory_size_bytes);
                            printf ("PCI device %s IOMMU group %s\n", design->vfio_device->device_name, design->vfio_device->iommu_group);

                            num_channel_combinations_tested = 0;
                            for (h2c_channel_id = 0;
                                 (h2c_channel_id < num_h2c_channels) && (num_channel_combinations_tested < arg_max_channel_combinations);
                                 h2c_channel_id++)
                            {
                                for (c2h_channel_id = 0;
                                     (c2h_channel_id < num_c2h_channels) && (num_channel_combinations_tested < arg_max_channel_combinations);
                                     c2h_channel_id++)
                                {
                                    test_success = perform_enabled_test (dma_test, design, vfio_device,
                                            h2c_channel_id, c2h_channel_id);
                                    overall_success = overall_success && test_success;
                                    num_channel_combinations_tested++;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                /* Have to skip a design which doesn't have channels in both directions */
                printf ("Skipping design %s PCI device %s IOMMU group %s due to num_h2c_channels=%u num_c2h_channels=%u\n",
                        fpga_design_names[design->design_id], vfio_device->device_name, vfio_device->iommu_group,
                        num_h2c_channels, num_c2h_channels);
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    printf ("\nOverall %s\n", overall_success ? "PASS" : "FAIL");

    return EXIT_SUCCESS;
}
