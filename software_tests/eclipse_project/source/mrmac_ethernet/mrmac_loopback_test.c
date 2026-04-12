/*
 * @file mrmac_loopback_test.c
 * @date 11 Apr 2026
 * @author Chester Gillon
 * @brief Perform a loopback test of packets on a MRMAC
 * @details
 *  The loopback may be provided either:
 *  a. Either externally on the MRMAC ports.
 *  b. Internally by using the Vivado Hardware Manager to set loopback in the transceivers.
 *
 *  The loopback is a functional test, which tests all packets sizes from the minimum to maximum configured in the MAC,
 *  incrementing one byte at a time. This checks the AXI stream tlast end-of-packet handling is as expected.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"
#include "mrmac_register_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <time.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>


#define ETHER_MAC_ADDRESS_LEN 6


/* Timeout used to detect if the transmit or receive DMA has hung. */
#define XMDA_TRANSFER_TIMEOUT_SECS 5


/* The number of transmit and receive buffers, each to allow the maximum size of test_ethernet_frame_t.
 * While this test has only one frame outstanding at once, have a larger number of buffers to flush any pending receive frames
 * in the FIFOs at the start of the test. */
#define NUM_BUFFERS 64


/* Based upon MRMAC_CTL_RX_MAX_PACKET_LEN_MASK having 15 bits.
 * Albeit the description of the register says the maximum value is 16383 (i.e. 14 bits) */
#define MAX_PACKET_BYTES 32767


/* Defines a variable length test Ethernet frame to be transmitted / received.
 *
 * Since the default MRMAC ctl_rx_min_packet_len is 64, the ctl_rx_min_packet_len and ctl_rx_max_packet_len fields are
 * assumed to be inclusive of the FCS. */
typedef struct __attribute__((packed))
{
    /* Ethernet Header */
    uint8_t destination_mac_addr[ETHER_MAC_ADDRESS_LEN];
    uint8_t source_mac_addr[ETHER_MAC_ADDRESS_LEN];
    uint16_t ether_type; /* Set to indicate a VLAN */

    /* Variable length */
    uint8_t test_payload[MAX_PACKET_BYTES -
                         (ETHER_MAC_ADDRESS_LEN /* destination_mac_addr */ +
                          ETHER_MAC_ADDRESS_LEN /* source_mac_addr */ +
                          sizeof (uint16_t) /* ether_type */)];
} test_ethernet_frame_t;


/* The context used to send/receive the loopback test frames */
typedef struct
{
    /* All the FPGA designs which have been opened */
    fpga_designs_t designs;
    /* The MRMAC design used to send/receive the test frames */
    fpga_design_t *mrmac_design;
    /* Mapped to the MRMAC port Configuration registers, Status registers, and Statistics counters.
     * Two sets for transmit and receive related registers, to allow for the command line arguments potentially specifying
     * different MRMAC ports on the same design to be used for transmit and receive. */
    uint8_t *mrmac_tx_port_regs;
    uint8_t *mrmac_rx_port_regs;
    /* The number of MRMAC ports for which statistics are collected */
    uint32_t num_ports_used_for_statistics;
    /* The port numbers for which statistics are collected. Number of valid indices is num_ports_used_for_statistics. */
    uint32_t statistics_port_nums[2];
    /* The MRMAC port statistics over the duration of the test. Number of valid indices is num_ports_used_for_statistics.
     * When the transmit and receive ports are different both entries are used. Otherwise only the first entry is used. */
    mrmac_port_statistics_t port_statistics[2];
    /* Overall success of initialising and performing XMDA transfers */
    bool xdma_overall_success;
    /* Read/write mapping for the XDMA descriptors */
    vfio_dma_mapping_t descriptors_mapping;
    /* XDMA read mapping used by device for Ethernet transmission */
    vfio_dma_mapping_t h2c_data_mapping;
    /* XDMA write mapping used by device for Ethernet reception */
    vfio_dma_mapping_t c2h_data_mapping;
    /* Used to perform XMDA transfers for Ethernet transmission / reception */
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
} loopback_test_context_t;


/* Command line arguments which specify the MRMAC device and port used to send/receive test frames */
static char arg_mrmac_device_location[PATH_MAX];
static uint32_t arg_mrmac_tx_port_num;
static uint32_t arg_mrmac_rx_port_num;


/**
 * @brief Display the program usage and then exit
 * @param[in] program_name Name of the program from argv[0]
 */
static void display_usage (const char *const program_name)
{
    printf ("Usage %s: [-i <domain>:<bus>:<dev>.<func>] -n [<mrmac_port_num>|<mrmac_tx_port_num>:<mrmac_rx_port_num>]\n", program_name);
    printf ("\n");
    printf ("  -i only open using VFIO specific PCI device in the event that there is more than\n");
    printf ("     one PCI device which matches the identity filters.\n");
    printf ("  -n specifies which MRMAC port to use. May be either:\n");
    printf ("     - A single number of the port to use for transmit and receive\n");
    printf ("     - A pair of colon delimited <mrmac_tx_port_num>:<mrmac_rx_port_num>\n");
    printf ("       to allow independent MRMAC ports to be used for transmit and receive.\n");

    exit (EXIT_FAILURE);
}


/**
 * @brief Read the command line arguments, exiting if an error in the arguments
 * @param[in] argc, argv Command line arguments passed to main
 */
static void read_command_line_arguments (const int argc, char *argv[])
{
    bool mrmac_port_num_specified = false;
    const char *const program_name = argv[0];
    const char *const optstring = "i:n:";
    int option;
    char junk;
    uint32_t port_num;
    uint32_t tx_port_num;
    uint32_t rx_port_num;

    /* Process the command line arguments */
    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'i':
            snprintf (arg_mrmac_device_location, sizeof (arg_mrmac_device_location), optarg);
            break;

        case 'n':
            if ((sscanf (optarg, "%" SCNu32 ":%" SCNu32 "%c", &tx_port_num, &rx_port_num, &junk) == 2) &&
                (tx_port_num < NUM_MRMAC_PORTS) && (rx_port_num < NUM_MRMAC_PORTS))
            {
                arg_mrmac_tx_port_num = tx_port_num;
                arg_mrmac_rx_port_num = rx_port_num;
            }
            else if ((sscanf (optarg, "%" SCNu32 "%c", &port_num, &junk) == 1) && (port_num < NUM_MRMAC_PORTS))
            {
                arg_mrmac_tx_port_num = port_num;
                arg_mrmac_rx_port_num = port_num;
            }
            else
            {
                printf ("Error: Invalid MRMAC port(s) %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            mrmac_port_num_specified = true;
            break;

        case '?':
        default:
            display_usage (program_name);
            break;
        }

        option = getopt (argc, argv, optstring);
    }

    /* Check the expected arguments have been provided */
    if (!mrmac_port_num_specified)
    {
        printf ("Error: The MRMAC port(s) must be specified\n\n");
        display_usage (program_name);
    }

    if (optind < argc)
    {
        printf ("Error: Unexpected nonoption (first %s)\n\n", argv[optind]);
        display_usage (program_name);
    }
}


/**
 * @brief Open the MRMAC device used to send/receive test frames
 * @param[in/out] context The context being initialised.
 */
static void open_mrmac_device (loopback_test_context_t *const context)
{
    /* Apply any device filter specified in the command line arguments */
    if (strlen (arg_mrmac_device_location) > 0)
    {
        vfio_add_pci_device_location_filter (arg_mrmac_device_location);
    }

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&context->designs);

    /* Obtain the MRMAC design to be used for the test, and validate has the port number requested by the command line arguments */
    uint32_t num_designs_with_mrmacs = 0;
    context->mrmac_design = NULL;
    for (uint32_t design_index = 0; design_index < context->designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const candidate_design = &context->designs.designs[design_index];

        if (candidate_design->mrmac.regs != NULL)
        {
            num_designs_with_mrmacs++;
            if (context->mrmac_design == NULL)
            {
                context->mrmac_design = candidate_design;
            }
        }
    }

    if (num_designs_with_mrmacs == 0)
    {
        printf ("Error: Found no design with a supported MRMAC\n");
        exit (EXIT_FAILURE);
    }
    else if (num_designs_with_mrmacs > 1)
    {
        printf ("Error: Found %u designs with MRMAC. Use -i option to select a single design\n", num_designs_with_mrmacs);
        exit (EXIT_FAILURE);
    }
    else if (!context->mrmac_design->mrmac.used_ports[arg_mrmac_tx_port_num])
    {
        printf ("Error: Design %s doesn't have requested MRMAC Tx port %u\n",
                fpga_design_names[context->mrmac_design->design_id], arg_mrmac_tx_port_num);
        exit (EXIT_FAILURE);
    }
    else if (!context->mrmac_design->mrmac.used_ports[arg_mrmac_rx_port_num])
    {
        printf ("Error: Design %s doesn't have requested MRMAC Rx port %u\n",
                fpga_design_names[context->mrmac_design->design_id], arg_mrmac_rx_port_num);
        exit (EXIT_FAILURE);
    }

    context->mrmac_tx_port_regs = &context->mrmac_design->mrmac.regs[arg_mrmac_tx_port_num * MRMAC_PORT_REGS_FRAME_SIZE];
    context->mrmac_rx_port_regs = &context->mrmac_design->mrmac.regs[arg_mrmac_rx_port_num * MRMAC_PORT_REGS_FRAME_SIZE];
    context->xdma_overall_success = true;

    if (arg_mrmac_tx_port_num == arg_mrmac_rx_port_num)
    {
        /* Collect statistics from the same port used for transmit and receive */
        context->statistics_port_nums[0] = arg_mrmac_tx_port_num;
        context->num_ports_used_for_statistics = 1;
    }
    else
    {
        /* Transmit and receive ports are different, so collect statistics for both */
        context->statistics_port_nums[0] = arg_mrmac_tx_port_num;
        context->statistics_port_nums[1] = arg_mrmac_rx_port_num;
        context->num_ports_used_for_statistics = 2;
    }

    /* Configure XDMA transmit to use a queue of variable size buffers. */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = context->mrmac_design->dma_bridge_memory_size_bytes,
        .dma_bridge_memory_base_address = context->mrmac_design->dma_bridge_memory_base_address,
        .min_size_alignment = 1, /* The host memory is byte addressable */
        .num_descriptors = NUM_BUFFERS,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = arg_mrmac_tx_port_num,
        .bytes_per_buffer = 0, /* Using variable length transfers */
        .host_buffer_start_offset = 0, /* Not used for variable length transfers */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = false,
        .timeout_seconds = XMDA_TRANSFER_TIMEOUT_SECS,
        .vfio_device = context->mrmac_design->vfio_device,
        .bar_index = context->mrmac_design->dma_bridge_bar,
        .descriptors_mapping = &context->descriptors_mapping,
        .data_mapping = &context->h2c_data_mapping,
        .overall_success = &context->xdma_overall_success
    };

    /* Configure XMDA receive to use a queue of fixed size buffers, based upon the cache line aligned size of the maximum
     * length payload. */
    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = context->mrmac_design->dma_bridge_memory_size_bytes,
        .dma_bridge_memory_base_address = context->mrmac_design->dma_bridge_memory_base_address,
        .min_size_alignment = 1, /* The host memory is byte addressable */
        .num_descriptors = NUM_BUFFERS,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = arg_mrmac_rx_port_num,
        .bytes_per_buffer = vfio_align_cache_line_size (sizeof (test_ethernet_frame_t)),
        .host_buffer_start_offset = 0, /* Separate host buffer used for transmit and receive transfers */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = false,
        .timeout_seconds = XMDA_TRANSFER_TIMEOUT_SECS,
        .vfio_device = context->mrmac_design->vfio_device,
        .bar_index = context->mrmac_design->dma_bridge_bar,
        .descriptors_mapping = &context->descriptors_mapping,
        .data_mapping = &context->c2h_data_mapping,
        .overall_success = &context->xdma_overall_success
    };

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (context->mrmac_design->vfio_device, &context->descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Read mapping used by device, for all the transmit buffers */
    allocate_vfio_dma_mapping (context->mrmac_design->vfio_device,
            &context->h2c_data_mapping,
            h2c_transfer_configuration.num_descriptors * vfio_align_cache_line_size (sizeof (test_ethernet_frame_t)),
            VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Write mapping used by device, for all the receive buffers */
    allocate_vfio_dma_mapping (context->mrmac_design->vfio_device,
            &context->c2h_data_mapping, c2h_transfer_configuration.num_descriptors * c2h_transfer_configuration.bytes_per_buffer,
            VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    context->xdma_overall_success = (context->descriptors_mapping.buffer.vaddr != NULL) &&
                                    (context->h2c_data_mapping.buffer.vaddr    != NULL) &&
                                    (context->c2h_data_mapping.buffer.vaddr    != NULL);
    if (context->xdma_overall_success)
    {
        /* Initialise the transfers */
        x2x_initialise_transfer_context (&context->h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&context->c2h_transfer, &c2h_transfer_configuration);
    }

    if (context->xdma_overall_success)
    {
        /* Start transfers for all receive buffers */
        for (uint32_t rx_buffer_index = 0; rx_buffer_index < NUM_BUFFERS; rx_buffer_index++)
        {
            x2x_start_next_c2h_buffer (&context->c2h_transfer);
        }
    }
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
 * @brief Close the MRMAC device used to send/receive test frames, freeing the resources.
 * @param[in/out] context The context being closed.
 */
static void close_mrmac_device (loopback_test_context_t *const context)
{
    x2x_finalise_transfer_context (&context->h2c_transfer);
    x2x_finalise_transfer_context (&context->c2h_transfer);

    report_if_transfer_failed (&context->h2c_transfer);
    report_if_transfer_failed (&context->c2h_transfer);

    free_vfio_dma_mapping (&context->c2h_data_mapping);
    free_vfio_dma_mapping (&context->h2c_data_mapping);
    free_vfio_dma_mapping (&context->descriptors_mapping);
    close_pcie_fpga_designs (&context->designs);
}


/*
 * @brief Wait for the receive link to be ready ("up") before starting the test
 * @details This is done since a VFIO open can reset the MRAMC ports.
 * @param[in/out] context The context to wait for the link to be ready.
 */
static void wait_receive_link_ready (loopback_test_context_t *const context)
{
    uint32_t rate_gbps = 0;
    uint32_t num_lanes = 0;
    uint32_t first_rx_status;
    uint32_t rx_status;
    uint32_t rx_rt_status;
    bool rx_link_ready;
    const struct timespec hold_off =
    {
        .tv_sec = 0,
        .tv_nsec = 100000000 /* 100 milliseconds */
    };
    bool first_wait = true;

    /* Read the statically configured rate from the MRMAC.
     * There is no support in the FPGA user logic nor this software for dynamic rate configuration. */
    const uint32_t tx_mode_reg = read_reg32 (context->mrmac_tx_port_regs, MRMAC_MODE_REG_OFFSET);
    const uint32_t tx_port_data_rate = vfio_extract_field_u32 (tx_mode_reg, MRMAC_CTL_DATA_RATE_MASK);
    const uint32_t rx_mode_reg = read_reg32 (context->mrmac_rx_port_regs, MRMAC_MODE_REG_OFFSET);
    const uint32_t rx_port_data_rate = vfio_extract_field_u32 (rx_mode_reg, MRMAC_CTL_DATA_RATE_MASK);

    switch (tx_port_data_rate)
    {
    case MRMAC_CTL_DATA_RATE_10GE:
        rate_gbps = 10;
        num_lanes = 1;
        break;

    case MRMAC_CTL_DATA_RATE_25GE:
        rate_gbps = 25;
        num_lanes = 1;
        break;

    case MRMAC_CTL_DATA_RATE_40GE:
        rate_gbps = 40;
        num_lanes = 4;
        break;

    case MRMAC_CTL_DATA_RATE_50GE:
        rate_gbps = 50;
        num_lanes = 2;
        break;

    case MRMAC_CTL_DATA_RATE_100GE:
        rate_gbps = 100;
        num_lanes = 4;
        break;

    default:
        printf ("Error: Unknown port_data_rate encoding 0x%x\n", tx_port_data_rate);
        exit (EXIT_FAILURE);
    }

    if (rx_port_data_rate != tx_port_data_rate)
    {
        printf ("Error Rx port data rate 0x%x and Tx port data rate 0x%x differ\n", rx_port_data_rate, tx_port_data_rate);
        exit (EXIT_FAILURE);
    }

    /* Determine the rx status register value which indicates if the link is ready to receive */
    uint32_t rx_link_ready_status_value = MRMAC_STAT_RX_STATUS_MASK | MRMAC_STAT_RX_BLOCK_LOCK_MASK;
    if (num_lanes > 1)
    {
        /* The Rx Aligned and Rx Synced bits are only applicable when more than one lane is use.
         * TBC that is is correct, since initial testing only used 10G and these bits weren't set. */
        rx_link_ready_status_value |= MRMAC_STAT_RX_ALIGNED_MASK | MRMAC_STAT_RX_SYNCED_MASK;
    }

    /* Wait for the latched and real-time receive status registers to indicate the link is ready to receive */
    do
    {
        rx_status = read_reg32 (context->mrmac_rx_port_regs, MRMAC_STAT_RX_STATUS_REG1_OFFSET);
        rx_rt_status = read_reg32 (context->mrmac_rx_port_regs, MRMAC_STAT_RX_RT_STATUS_REG1_OFFSET);
        rx_link_ready = (rx_status == rx_link_ready_status_value) && (rx_rt_status == rx_link_ready_status_value);

        if (!rx_link_ready)
        {
            if (rx_status != rx_link_ready_status_value)
            {
                /* Write to latched status to clear error indications */
                write_reg32 (context->mrmac_rx_port_regs, MRMAC_STAT_RX_STATUS_REG1_OFFSET, UINT32_MAX);
            }

            if (first_wait)
            {
                first_rx_status = rx_status;
                printf ("Waiting to link to be ready to receive.\n");
                first_wait = false;
            }
            clock_nanosleep (CLOCK_MONOTONIC, 0, &hold_off, NULL);
        }
    } while (!rx_link_ready);

    printf ("Link ready at %u Gb/s", rate_gbps);
    if (!first_wait)
    {
        printf (" (latched rx_status initial 0x%08X last 0x%08X)", first_rx_status, rx_status);
    }
    printf ("\n");
}


/**
 * @brief Collect the MRMAC port statistics
 * @param[in/out] context Context to get the MRMAC port statistics for
 */
static void collect_port_statistics (loopback_test_context_t *const context)
{
    uint32_t port_index;

    for (port_index = 0; port_index < context->num_ports_used_for_statistics; port_index++)
    {
        mrmac_snapshot_port_statistics (context->mrmac_design, context->statistics_port_nums[port_index],
                &context->port_statistics[port_index]);
    }

    for (port_index = 0; port_index < context->num_ports_used_for_statistics; port_index++)
    {
        mrmac_read_port_statistics (&context->port_statistics[port_index]);
    }
}


/**
 * @brief Sequence the MRMAC loopback test
 * @param[in/out] context Defines the MRMAC ports to perform the loopback test on
 */
static void sequence_mrmac_loopback_test (loopback_test_context_t *const context)
{
    size_t transfer_len;
    bool end_of_packet;

    /* Randomly generated MAC addresses created by https://www.browserling.com/tools/random-mac.
     * For this test with the Ethernet packets looped back, the actual addresses don't matter. */
    const uint8_t destination_mac_addr[ETHER_MAC_ADDRESS_LEN] = {0x7a, 0xca, 0x56, 0x3c, 0x55, 0x17};
    const uint8_t source_mac_addr     [ETHER_MAC_ADDRESS_LEN] = {0x9a, 0xf3, 0x0c, 0xd5, 0x79, 0x51};

    /* Read the MRMAC configuration for min/max valid receive lengths, to control the range of frame sizes tested. */
    const uint32_t configuration_rx_mtu_reg = read_reg32 (context->mrmac_rx_port_regs, MRMAC_CONFIGURATION_RX_MTU_OFFSET);
    const uint32_t rx_min_packet_len = vfio_extract_field_u32 (configuration_rx_mtu_reg, MRMAC_CTL_RX_MIN_PACKET_LEN_MASK);
    const uint32_t rx_max_packet_len = vfio_extract_field_u32 (configuration_rx_mtu_reg, MRMAC_CTL_RX_MAX_PACKET_LEN_MASK);

    /* Flush any receive frames prior to the start of the test */
    uint32_t num_frames_flushed = 0;
    while (context->xdma_overall_success && x2x_poll_completed_transfer (&context->c2h_transfer, &transfer_len, &end_of_packet))
    {
        num_frames_flushed++;
        x2x_start_next_c2h_buffer (&context->c2h_transfer);
    }
    if (num_frames_flushed > 0)
    {
        printf ("Flush %u receive frames at start of test\n", num_frames_flushed);
    }

    printf ("Testing %s Tx port %u Rx Port %u with %u packet lengths (including FCS) from %u to %u bytes\n",
            fpga_design_names[context->mrmac_design->design_id],
            arg_mrmac_tx_port_num, arg_mrmac_rx_port_num,
            (rx_max_packet_len - rx_min_packet_len) + 1, rx_min_packet_len, rx_max_packet_len);

    /* Start statistics collection for the test. The counter values collected here are before the test and the values
     * will be be overwritten at the end without being used. */
    collect_port_statistics (context);

    /* Iterate over the valid frame lengths as configured in the MRMAC, which include the FCS */
    uint32_t tx_buffer_index = 0;
    uint8_t test_pattern = 0;
    size_t total_bytes_including_fcs = 0;
    for (uint32_t packet_len_including_fcs = rx_min_packet_len;
            context->xdma_overall_success && (packet_len_including_fcs <= rx_max_packet_len); packet_len_including_fcs++)
    {
        /* The packet length used on the AXI streams doesn't include the FCS. Determine the payload length in bytes
         * to get the required packet length. */
        const size_t packet_len_excluding_fcs = packet_len_including_fcs - sizeof (uint32_t);
        const size_t num_payload_bytes = packet_len_excluding_fcs - offsetof (test_ethernet_frame_t, test_payload);

        /* Populate and transmit a test frame:
         * - Source and destination are fixed unicast MAC addresses.
         * - Ethertype "802.1 Local Experimental 1", since an arbitrary payload.
         * - Payload is just an incrementing byte value. */
        const uint64_t host_buffer_offset = tx_buffer_index * vfio_align_cache_line_size (sizeof (test_ethernet_frame_t));
        test_ethernet_frame_t *const tx_frame =
                x2x_populate_stream_transfer (&context->h2c_transfer, packet_len_excluding_fcs, host_buffer_offset);
        memcpy (tx_frame->destination_mac_addr, destination_mac_addr, sizeof (tx_frame->destination_mac_addr));
        memcpy (tx_frame->source_mac_addr, source_mac_addr, sizeof (tx_frame->source_mac_addr));
        tx_frame->ether_type = htons (ETH_P_802_EX1);
        for (uint32_t payload_index = 0; payload_index < num_payload_bytes; payload_index++)
        {
            tx_frame->test_payload[payload_index] = test_pattern++;
        }
        x2x_start_populated_descriptors (&context->h2c_transfer);

        /* Wait for the frame transmit and loopback receive to complete. A transfer timeout has been enabled */
        bool tx_completed = false;
        const test_ethernet_frame_t *rx_frame = NULL;
        while (context->xdma_overall_success && (!tx_completed || (rx_frame == NULL)))
        {
            if (!tx_completed)
            {
                tx_completed = x2x_poll_completed_transfer (&context->h2c_transfer, NULL, NULL) != NULL;
            }
            if (rx_frame == NULL)
            {
                rx_frame = x2x_poll_completed_transfer (&context->c2h_transfer, &transfer_len, &end_of_packet);
            }
        }

        if (context->xdma_overall_success)
        {
            /* Check the expected receive frame is a copy of the transmit frame */
            if (!end_of_packet)
            {
                x2x_record_failure (&context->c2h_transfer,
                        "end_of_packet not indicated, packet_len_excluding_fcs=%zu rx transfer_len=%zu",
                        packet_len_excluding_fcs, transfer_len);
            }
            else if (transfer_len != packet_len_excluding_fcs)
            {
                x2x_record_failure (&context->c2h_transfer, "Rx transfer_len=%zu, expected %zu", transfer_len, packet_len_excluding_fcs);
            }
            else if (memcmp (tx_frame, rx_frame, packet_len_excluding_fcs) != 0)
            {
                x2x_record_failure (&context->c2h_transfer, "Receive frame has incorrect context for transfer_len=%zu", transfer_len);
            }

            x2x_start_next_c2h_buffer (&context->c2h_transfer);
            total_bytes_including_fcs += packet_len_including_fcs;
        }

        tx_buffer_index = (tx_buffer_index + 1) % NUM_BUFFERS;
    }

    /* Display the post statistics over the loopback test */
    collect_port_statistics (context);
    for (uint32_t stats_index = 0; stats_index < context->num_ports_used_for_statistics; stats_index++)
    {
        mrmac_display_port_statistics (&context->port_statistics[stats_index]);
    }

    /* Display a summary. Any error messages will be reported by report_if_transfer_failed() */
    printf ("Total byte including FCS: %zu\n", total_bytes_including_fcs);
    printf ("Loopback test: %s\n", context->xdma_overall_success ? "PASS" : "FAIL");
}


int main (int argc, char *argv[])
{
    loopback_test_context_t context;

    /* Read the commandline arguments */
    read_command_line_arguments (argc, argv);

    memset (&context, 0, sizeof (context));
    open_mrmac_device (&context);
    wait_receive_link_ready (&context);
    sequence_mrmac_loopback_test (&context);

    close_mrmac_device (&context);

    return EXIT_SUCCESS;
}
