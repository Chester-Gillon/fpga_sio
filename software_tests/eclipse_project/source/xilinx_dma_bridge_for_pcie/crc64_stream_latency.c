/*
 * @file crc64_stream_latency.c
 * @date 20 Jun 2025
 * @author Chester Gillon
 * @brief Measure the latency of CRC64 stream with different packet sizes
 * @details
 *   The latency may be impacted by the test thread getting preempted. There is no attempt to set a core affinity, not try and
 *   isolate other background tasks. For that reason the latency values for different percentiles is reported, so can get an
 *   indication of timing outliers.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"
#include "xilinx_axi_stream_switch_configure.h"
#include "transfer_timing.h"
#include "crc64.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/mman.h>


/* Defines the range of H2C packet lengths (input to CRC64 stream) which are tested in increments of power of two */
#define MIN_H2C_PACKET_LEN_BYTES 32
#define MAX_H2C_PACKET_LEN_BYTES (1024 * 1024)

/* Number of timing measurements for each different packet length */
#define NUM_MEASUREMENT_SAMPLES 100000


/* Contains the measured latencies for one test iteration */
static int64_t measured_latencies_ns[NUM_MEASUREMENT_SAMPLES];


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
 * @brief qsort comaprison function for latency values
 */
static int latency_compare (const void *const compare_a, const void *const compare_b)
{
    const int64_t *const latency_a = compare_a;
    const int64_t *const latency_b = compare_b;

    if (*latency_a < *latency_b)
    {
        return -1;
    }
    else if (*latency_a == *latency_b)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


/**
 * @brief Measure the CRC64 stream latency for a particular packet length
 * @param[in] design The design containing the CRC64 stream to test
 * @param[in/out] vfio_device The VFIO device containing the CRC64 stream to test
 * @param[in] h2c_channel_id The identity of the H2C channel used for the CRC64 stream
 * @param[in] c2h_channel_id The identity of the C2H channel used for the CRC64 stream
 * @param[in] h2c_packet_len_bytes The length the packet over which the CRC64 is calculated
 * @param[in/out] test_sequence Used to generate a different test pattern to perform the CRC64 calculation on for every call.
 */
static void measure_crc64_stream_latency (fpga_design_t *const design, vfio_device_t *const vfio_device,
                                          const uint32_t h2c_channel_id, const uint32_t c2h_channel_id,
                                          const uint32_t h2c_packet_len_bytes, uint64_t *const test_sequence)
{
    const uint32_t h2c_packet_len_words = h2c_packet_len_bytes / sizeof (uint64_t);

    /* Read/write mapping for the descriptors */
    vfio_dma_mapping_t descriptors_mapping;
    /* Read mapping used by device */
    vfio_dma_mapping_t h2c_data_mapping;
    /* Write mapping used by device */
    vfio_dma_mapping_t c2h_data_mapping;

    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    bool overall_success = true;
    int64_t start_time_ns;
    int64_t stop_time_ns;

    /* Disable timeout, so that the xilinx_dma_bridge_transfers code doesn't use timers */
    const int64_t disable_timeout = -1;

    /* Populate the transfer configurations to be used, selecting use of a single fixed size buffer */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The host memory is byte addressable */
        .num_descriptors = 1,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = h2c_channel_id,
        .bytes_per_buffer = h2c_packet_len_bytes,
        .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = false,
        .timeout_seconds = disable_timeout,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &h2c_data_mapping,
        .overall_success = &overall_success
    };

    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The host memory is byte addressable */
        .num_descriptors = 1,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = c2h_channel_id,
        .bytes_per_buffer = sizeof (uint64_t), /* The calculated CRC64 */
        .host_buffer_start_offset = 0, /* Separate host buffer used for the transfer in each direction */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = false,
        .timeout_seconds = disable_timeout,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &descriptors_mapping,
        .data_mapping = &c2h_data_mapping,
        .overall_success = &overall_success
    };

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Read mapping used by device, for the entire input packet length */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, h2c_packet_len_bytes, VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Write mapping used by device, for just the CRC64 result */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, sizeof (uint64_t), VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    uint64_t *const input_words = h2c_data_mapping.buffer.vaddr;
    uint64_t expected_crc64;

    overall_success = (descriptors_mapping.buffer.vaddr != NULL) &&
                      (h2c_data_mapping.buffer.vaddr    != NULL) &&
                      (c2h_data_mapping.buffer.vaddr    != NULL);
    if (overall_success)
    {
        /* Initialise the transfers */
        x2x_initialise_transfer_context (&h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&c2h_transfer, &c2h_transfer_configuration);

        /* Populate the input packet contents, and calculate the expected CRC64 */
        expected_crc64 = UINT64_MAX;
        for (uint32_t word_index = 0; word_index < h2c_packet_len_words; word_index++)
        {
            linear_congruential_generator64 (test_sequence);
            input_words[word_index] = *test_sequence;
            expected_crc64 = crc (expected_crc64, *test_sequence);
        }

        /* Perform a number of test iterations, collecting the latency of the CRC64 calculation for each iteration.
         * The number of iterations is one more than the number of stored measurements, since the first latency value
         * is excluded from the measurements to in case increased due to processor caching on the 1st iteration.
         * Use of mlockall() should prevent any page faults during the test. */
        void *h2c_buffer;
        uint64_t *actual_crc64;
        size_t transfer_len;
        bool end_of_packet;
        for (uint32_t test_iteration = 0; overall_success && (test_iteration <= NUM_MEASUREMENT_SAMPLES); test_iteration++)
        {
            /* Latency measurements starts just before starting the transfers */
            start_time_ns = get_monotonic_time ();

            /* Start the transfers */
            x2x_start_next_c2h_buffer (&c2h_transfer);
            h2c_buffer = x2x_get_next_h2c_buffer (&h2c_transfer);
            x2x_start_populated_descriptors (&h2c_transfer);

            /* Wait for the transfers to complete */
            do
            {
                actual_crc64 = x2x_poll_completed_transfer (&c2h_transfer, &transfer_len, &end_of_packet);
            } while (overall_success && (actual_crc64 == NULL));
            do
            {
                h2c_buffer = x2x_poll_completed_transfer (&h2c_transfer, NULL, NULL);
            } while (overall_success && (h2c_buffer == NULL));

            /* Latency measurement stops after the transfers have completed */
            stop_time_ns = get_monotonic_time ();

            /* Check for successful completion of the transfers with the expected CRC64 value */
            X2X_ASSERT (&h2c_transfer, h2c_buffer != NULL);
            X2X_ASSERT (&c2h_transfer, actual_crc64 != NULL);
            if (actual_crc64 != NULL)
            {
                X2X_ASSERT (&c2h_transfer, *actual_crc64 == expected_crc64);

                /* Since the input data is the same for every test iteration, write back an invalid CRC64 result
                 * so can check the expected value does get written for every test iteration. */
                *actual_crc64 = ~expected_crc64;
            }

            /* Store the latency, except for the 1st iteration where the measurement is discarded */
            if (test_iteration > 0)
            {
                measured_latencies_ns[test_iteration - 1] = stop_time_ns - start_time_ns;
            }
        }

        /* If the transfers were successful, report the latency measurements */
        if (overall_success)
        {
            const double reported_percentiles[] = {50.0, 75.0, 99.0, 99.999};
            const uint32_t num_percentiles = sizeof (reported_percentiles) / sizeof (reported_percentiles[0]);

            /* Sort the latency measurements to get percentiles */
            qsort (measured_latencies_ns, NUM_MEASUREMENT_SAMPLES, sizeof (measured_latencies_ns[0]), latency_compare);
            printf ("%7u len bytes latencies (us):", h2c_packet_len_bytes);
            for (uint32_t percentile_index = 0; percentile_index < num_percentiles; percentile_index++)
            {
                const uint32_t latency_index =
                        ((uint32_t) ((reported_percentiles[percentile_index] / 100.0) * (double) NUM_MEASUREMENT_SAMPLES)) - 1;
                const double latency_us = ((double) measured_latencies_ns[latency_index]) / 1E3;

                printf (" %7.3f (%g')", latency_us, reported_percentiles[percentile_index]);
            }
            printf ("\n");
        }

        /* Finalise the transfer contexts */
        if (h2c_transfer.completed_descriptor_count != NULL)
        {
            x2x_finalise_transfer_context (&h2c_transfer);
        }
        if (c2h_transfer.completed_descriptor_count != NULL)
        {
            x2x_finalise_transfer_context (&c2h_transfer);
        }

        report_if_transfer_failed (&h2c_transfer);
        report_if_transfer_failed (&c2h_transfer);

        free_vfio_dma_mapping (&c2h_data_mapping);
        free_vfio_dma_mapping (&h2c_data_mapping);
        free_vfio_dma_mapping (&descriptors_mapping);
    }
}


int main (int argc, char *argv[])
{
    int rc;
    int saved_errno;
    fpga_designs_t designs;
    uint32_t num_h2c_channels;
    uint32_t num_c2h_channels;
    device_routing_t routing;
    uint64_t test_sequence;

    /* Attempt to lock all future pages to try and get deterministic timing */
    errno = 0;
    rc = mlockall (MCL_CURRENT | MCL_FUTURE);
    saved_errno = errno;
    if (rc != 0)
    {
        printf ("mlockall() failed : %s\n", strerror (saved_errno));
    }

    /* Use a repeatable test data sequence for every run */
    test_sequence = 0;

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Process any FPGA designs which have the CRC64 stream.
     * This is a sub-set of those using a DMA bridge with AXI streams */
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
                configure_routing_for_device (design, &routing);
                for (uint32_t route_index = 0; route_index < routing.num_routes; route_index++)
                {
                    const xilinx_axi_switch_master_port_configuration_t *const route = &routing.routes[route_index];
                    const uint32_t h2c_channel_id = route->slave_port;
                    const uint32_t c2h_channel_id = route->master_port;

                    if (route->enabled)
                    {
                        switch (design->design_id)
                        {
                        case FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64:
                        case FPGA_DESIGN_TEF1001_DMA_STREAM_CRC64:
                        case FPGA_DESIGN_TOSING_160T_DMA_STREAM_CRC64:
                        case FPGA_DESIGN_NITEFURY_DMA_STREAM_CRC64:
                        case FPGA_DESIGN_AS02MC04_DMA_STREAM_CRC64:
                            printf ("Testing design %s using C2H %u -> H2C %u\n",
                                    fpga_design_names[design->design_id], h2c_channel_id, c2h_channel_id);
                            for (uint32_t h2c_packet_len_bytes = MIN_H2C_PACKET_LEN_BYTES;
                                    h2c_packet_len_bytes <= MAX_H2C_PACKET_LEN_BYTES;
                                    h2c_packet_len_bytes <<= 1)
                            {
                                measure_crc64_stream_latency (design, vfio_device, h2c_channel_id, c2h_channel_id,
                                        h2c_packet_len_bytes, &test_sequence);
                            }
                            break;

                        default:
                            /* The streams in this design don't contain the CRC64 functionality */
                            break;
                        }
                    }
                }
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
