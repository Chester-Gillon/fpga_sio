/*
 * @file test_dma_descriptor_credits.c
 * @date 3 Dec 2023
 * @author Chester Gillon
 * @brief Investigate the use of descriptor credits in the Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details
 *   This was written since from PG195 couldn't see how can add a new descriptor to the linked-list for a channel
 *   while the DMA is running, while avoiding potential race conditions of the existing linked-list stopping at
 *   the same time as trying to append a new descriptor to the linked-list.
 *
 *   Whereas by giving the DMA engine a "ring" of descriptors and enabling credits, can leave the DMA engine
 *   running and write to X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET to cause the DMA engine to process the next
 *   set of populated descriptors.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_host_interface.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>

#include <unistd.h>


/* Defines the context for a descriptor ring for a AXI4 Memory Mapped Interface or AXI4 Stream Interface */
typedef struct
{
    /* DMA_SUBMODULE_H2C_CHANNELS or DMA_SUBMODULE_C2H_CHANNELS to identify which direction of DMA transfers being used */
    uint32_t channels_submodule;
    /* Mapped base of the H2C Channel or C2H Channel registers for the DMA transfers */
    uint8_t *x2x_channel_regs;
    /* Mapped base of the H2C SGDMA or C2H SGDMA registers for the DMA transfers */
    uint8_t *x2x_sgdma_regs;
    /* Mapped base of the SGDMA Common registers for the DMA transfers */
    uint8_t *sgdma_common_regs;
    /* The number of descriptors in the ring */
    uint32_t num_descriptors;
    /* Where the completed descriptor count is written back to by the DMA engine */
    completed_descriptor_count_writeback_t *completed_descriptor_count;
    /* The ring of descriptors */
    dma_descriptor_t *descriptors;
    /* For the C2H of a Stream Interface for each descriptor used to write back the length information */
    c2h_stream_writeback_t *stream_writeback;
    /* The count of descriptors which have been started */
    uint32_t started_descriptor_count;
    /* The index of the descriptor in the ring which is to be used next */
    uint32_t next_descriptor_index;
} descriptor_ring_t;


/* The timeout for test. A global variable, so may be changed when single stepping */
static int64_t test_timeout_secs = 10;


/* The absolute CLOCK_MONOTONIC time at which the test is timed out */
static int64_t abs_test_timeout;


/* Optional command line arguments which can be used to add an offset to the allocate IOVA values
 * for the VFIO DMA mapping to test the effect of using getting the PCIe card DMA to use an invalid IOVA. */
static bool arg_apply_iova_offsets;
static uint64_t arg_descriptors_iova_offset;
static uint64_t arg_h2c_data_iova_offset;
static uint64_t arg_c2h_data_iova_offset;


/* Command line argument which selects VFIO_DEVICE_DMA_CAPABILITY_A32, for testing the vfio_access code */
static bool arg_test_a32_dma_capability;


/* Command line argument which performs a test of VFIO reset */
static bool arg_test_vfio_reset;


/**
 * @brief Parse the command line arguments
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "d:o:3r?";
    int option;
    char junk;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case 'o':
            if (sscanf (optarg, "%" SCNi64 ",%" SCNi64 ",%" SCNi64 ",%c",
                    &arg_descriptors_iova_offset, &arg_h2c_data_iova_offset, &arg_c2h_data_iova_offset, &junk) != 3)
            {
                printf ("Invalid IOVA offsets %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_apply_iova_offsets = true;
            break;

        case '3':
            arg_test_a32_dma_capability = true;
            break;

        case 'r':
            arg_test_vfio_reset = true;
            break;

        case '?':
        default:
            printf ("Usage %s -d <pci_device_location> -o <descriptors_iova_offset,h2c_data_iova_offset,c2h_data_iova_offset> [-3] [-r]\n", argv[0]);
            printf ("  -3 specifies only 32-bit DMA addressing capability\n");
            printf ("  -r performs a test of VFIO reset\n");
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


/**
 * @brief Start the timeout for a test
 */
static void start_test_timeout (void)
{
    const int64_t nsecs_per_sec = 1000000000;

    abs_test_timeout = get_monotonic_time () + (test_timeout_secs * nsecs_per_sec);
}


/**
 * @brief Check for a test timeout, displaying a diagnostic message when the timeout expires
 * @param[in/out] success Set to false upon timeout. Takes no action if the test has already failed.
 * @param[in] format printf style format string for diagnostic message.
 * @param[in] ... printf arguments
 */
static void check_for_test_timeout (bool *const success, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
static void check_for_test_timeout (bool *const success, const char *format, ...)
{
    if (*success)
    {
        const int64_t now = get_monotonic_time ();

        if (now >= abs_test_timeout)
        {
            va_list args;

            printf ("Test timeout waiting for ");
            va_start (args, format);
            vprintf (format, args);
            va_end (args);
            printf ("\n");
            *success = false;
        }
    }
}


/**
 * @brief Get the mapped registers base for the DMA control registers
 * @param[in,out] design The FPGA design containing the DMA bridge to get the mapped registers for
 * @return The mapped registers, or NULL upon error
 */
static uint8_t *get_dma_mapped_registers_base (fpga_design_t *const design)
{

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    const size_t dma_control_base_offset = 0x0;
    const size_t dma_control_frame_size = 0x10000;
    uint8_t *const mapped_registers_base =
            map_vfio_registers_block (design->vfio_device, design->dma_bridge_bar, dma_control_base_offset, dma_control_frame_size);
    if (mapped_registers_base == NULL)
    {
        printf ("BAR[%" PRIu32 " size of 0x%llx too small for DMA/Bridge Subsystem for PCI Express\n",
                design->dma_bridge_bar, design->vfio_device->regions_info[design->dma_bridge_bar].size);
    }

    return mapped_registers_base;
}


/**
 * @brief Test that the DMA credits in a DMA bridge can be incremented and read back as expected
 * @details This doesn't actually start DMA transfers to consume descriptor credits, just checks that
 *          can increment the credits.
 *          Only tests a single H2C channel.
 * @param[in/out] design the FPGA design containing a DMA bridge to test
 * @return Returns true if the test has passed
 */
static bool test_dma_credit_incrementing (fpga_design_t *const design)
{
    const uint32_t channel_id = 0;
    uint32_t actual_credits;
    uint32_t expected_credits;

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    uint8_t *const mapped_registers_base = get_dma_mapped_registers_base (design);
    if (mapped_registers_base == NULL)
    {
        return false;
    }

    uint8_t *const x2x_channel_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (DMA_SUBMODULE_H2C_CHANNELS, channel_id)];
    uint8_t *const x2x_sgdma_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (DMA_SUBMODULE_H2C_SGDMA, channel_id)];
    uint8_t *const sgdma_common_regs = &mapped_registers_base[DMA_SUBMODULE_BAR_START_OFFSET (DMA_SUBMODULE_SGDMA_COMMON)];

    /* Halt descriptor fetches for the channel as need to set the channel running to test adding credits,
     * but this test doesn't setup any actual descriptors. */
    write_reg32 (sgdma_common_regs, SGDMA_DESCRIPTOR_CONTROL_W1S_OFFSET, 1U << (SGDMA_DESCRIPTOR_H2C_DSC_HALT_LOW_BIT + channel_id));

    /* Set the channel running, but with actual descriptor fetches halted */
    write_reg32 (x2x_channel_regs, X2X_CHANNEL_CONTROL_W1S_OFFSET, X2X_CHANNEL_CONTROL_RUN);

    /* Test all possible increments of credits, from the minimum to maximum */
    for (uint32_t credit_increment = 1; credit_increment < X2X_SGDMA_MAX_DESCRIPTOR_CREDITS; credit_increment++)
    {
        /* The number of credits should be zero since:
         * a. Opening a VFIO device issues a PCI reset.
         * b. This function resets the number of credits at the end of each run. */
        expected_credits = 0;

        /* Enable descriptor credits for the channel */
        write_reg32 (sgdma_common_regs, SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET,
                1U << (SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT + channel_id));

        actual_credits = read_reg32 (x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET);
        if (actual_credits != expected_credits)
        {
            printf ("At start of test credit_increment=%" PRIu32 " actual_credits=%" PRIu32 " expected_credits=%" PRIu32 "\n",
                    credit_increment, actual_credits, expected_credits);
            return false;
        }

        /* Set the number of credits to the maximum, advancing by credit_increment on each increment where possible
         * On the final iteration may have to limit the the number of credits advanced to avoid exceeding the maximum. */
        while (expected_credits < X2X_SGDMA_MAX_DESCRIPTOR_CREDITS)
        {
            const uint32_t remaining_credits = X2X_SGDMA_MAX_DESCRIPTOR_CREDITS - expected_credits;
            const uint32_t num_credits_to_add = (credit_increment < remaining_credits) ? credit_increment : remaining_credits;

            write_reg32 (x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, num_credits_to_add);
            expected_credits += num_credits_to_add;
            actual_credits = read_reg32 (x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET);
            if (actual_credits != expected_credits)
            {
                printf ("During test credit_increment=%" PRIu32 " actual_credits=%" PRIu32 " expected_credits=%" PRIu32 "\n",
                        credit_increment, actual_credits, expected_credits);
                return false;
            }
        }

        /* Disable descriptor credits for the channel, which should reset the credits */
        write_reg32 (sgdma_common_regs, SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1C_OFFSET,
                1U << (SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT + channel_id));
    }

    /* Stop the channel running at the end of the test */
    write_reg32 (x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);

    return true;
}


/**
 * @brief Initialise one ring of descriptors for a AXI4 Memory Mapped or AXI4 Stream Interface.
 * @details This creates the ring of descriptors and starts the channel running with no credits available.
 *          As a result, the descriptors won't be use for DMA transfers until credits are made available.
 * @param[out] The descriptor ring to initialise
 * @param[in/out] mapped_registers_base The mapped registers for the control registers.
 * @param[in] channels_submodule Either DMA_SUBMODULE_H2C_CHANNELS or DMA_SUBMODULE_C2H_CHANNELS
 *                               to identify which direction are initialising the ring for
 * @param[in] channel_id The identity of the channel are initialising the ring for
 * @param[in] num_descriptors The number of descriptors in the ring
 * @param[in/out] descriptors_mapping The mapping to allocate the descriptor ring from
 */
static void initialise_descriptor_ring (descriptor_ring_t *const ring, uint8_t *const mapped_registers_base,
                                        const uint32_t channels_submodule, const uint32_t channel_id,
                                        const uint32_t num_descriptors,
                                        vfio_dma_mapping_t *const descriptors_mapping)
{
    const uint32_t sgdma_channels_submodule = (channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            DMA_SUBMODULE_H2C_SGDMA : DMA_SUBMODULE_C2H_SGDMA;
    uint64_t completed_descriptor_count_iova;
    uint64_t first_descriptor_iova;
    uint64_t first_stream_writeback_iova;

    /* Access the channel registers */
    ring->channels_submodule = channels_submodule;
    ring->x2x_channel_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (channels_submodule, channel_id)];
    ring->x2x_sgdma_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (sgdma_channels_submodule, channel_id)];
    ring->sgdma_common_regs = &mapped_registers_base[DMA_SUBMODULE_BAR_START_OFFSET (DMA_SUBMODULE_SGDMA_COMMON)];
    ring->num_descriptors = num_descriptors;
    ring->next_descriptor_index = 0;
    ring->started_descriptor_count = 0;

    /* Determine if a stream interface */
    const uint32_t identity_reg_value = read_reg32 (ring->x2x_channel_regs, SUBMODULE_IDENTIFIER_OFFSET);
    const bool is_axi4_stream = (identity_reg_value & SUBMODULE_IDENTIFIER_STREAM_MASK) != 0;

    /* Allocate the descriptor writeback array to record the length for each received transfer */
    if (is_axi4_stream && (channels_submodule == DMA_SUBMODULE_C2H_CHANNELS))
    {
        vfio_dma_mapping_align_space (descriptors_mapping);
        ring->stream_writeback =
                vfio_dma_mapping_allocate_space (descriptors_mapping, num_descriptors * sizeof (*ring->stream_writeback),
                        &first_stream_writeback_iova);
    }
    else
    {
        ring->stream_writeback = NULL;
    }

    /* Initialise the ring of descriptors, excluding the length and memory addresses for each descriptor, which are set before use.
     * DMA_DESCRIPTOR_CONTROL_COMPLETED is used to allow pollmode writeback to detect completion of the descriptor. */
    vfio_dma_mapping_align_space (descriptors_mapping);
    ring->descriptors =
            vfio_dma_mapping_allocate_space (descriptors_mapping, num_descriptors * sizeof (ring->descriptors[0]),
                    &first_descriptor_iova);
    for (uint32_t descriptor_index = 0; descriptor_index < num_descriptors; descriptor_index++)
    {
        dma_descriptor_t *const descriptor = &ring->descriptors[descriptor_index];
        const uint32_t next_descriptor_index = (descriptor_index + 1) % num_descriptors;
        const uint64_t next_descriptor_iova = first_descriptor_iova + (next_descriptor_index * sizeof (*descriptor));

        descriptor->magic_nxt_adj_control = DMA_DESCRIPTOR_MAGIC | DMA_DESCRIPTOR_CONTROL_COMPLETED;
        descriptor->len = 0;
        if (ring->stream_writeback != NULL)
        {
            /* For a C2H stream set the address for where the writeback for this stream is stored */
            ring->stream_writeback[descriptor_index].wb_magic_status = 0;
            ring->stream_writeback[descriptor_index].length = 0;
            descriptor->src_adr = first_stream_writeback_iova + (descriptor_index * sizeof (ring->stream_writeback[0]));
        }
        else
        {
            descriptor->src_adr = 0;
        }
        descriptor->dst_adr = 0;
        descriptor->nxt_adr = next_descriptor_iova;
    }

    /* Initialise the write back to monitor completed descriptors */
    vfio_dma_mapping_align_space (descriptors_mapping);
    ring->completed_descriptor_count =
            vfio_dma_mapping_allocate_space (descriptors_mapping, sizeof (*ring->completed_descriptor_count),
                    &completed_descriptor_count_iova);
    write_split_reg64 (ring->x2x_channel_regs, X2X_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET, completed_descriptor_count_iova);
    ring->completed_descriptor_count->sts_err_compl_descriptor_count = 0;

    /* For the first descriptor set it's address in the DMA control registers.
     * Number of extra descriptors is set to zero as are no trying to optimise the descriptor fetching. */
    write_split_reg64 (ring->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_ADDRESS_OFFSET, first_descriptor_iova);
    write_reg32 (ring->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_ADJACENT_OFFSET, 0);

    /* Set channel control to enable pollmode write back and logging of all errors */
    uint32_t all_errors =
            X2C_CHANNEL_CONTROL_IE_DESC_ERROR |
            X2X_CHANNEL_CONTROL_IE_READ_ERROR |
            X2X_CHANNEL_CONTROL_IE_INVALID_LENGTH |
            X2X_CHANNEL_CONTROL_IE_MAGIC_STOPPED |
            X2X_CHANNEL_CONTROL_IE_ALIGN_MISMATCH;
    if (ring->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
    {
        all_errors |= H2C_CHANNEL_CONTROL_IE_WRITE_ERROR;
    }
    write_reg32 (ring->x2x_channel_regs, X2X_CHANNEL_CONTROL_RW_OFFSET,
            X2X_CHANNEL_CONTROL_POLLMODE_WB_ENABLE | all_errors);

    /* Enable use of descriptor crediting */
    const uint32_t credit_enable_low_bit = (ring->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT : SGDMA_DESCRIPTOR_C2H_DSC_CREDIT_ENABLE_LOW_BIT;
    write_reg32 (ring->sgdma_common_regs, SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET, 1U << (credit_enable_low_bit + channel_id));

    /* Set the channel running, with no available credits so no actual DMA transfers yet */
    const uint32_t halt_low_bit = (ring->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            SGDMA_DESCRIPTOR_H2C_DSC_HALT_LOW_BIT : SGDMA_DESCRIPTOR_C2H_DSC_HALT_LOW_BIT;
    write_reg32 (ring->sgdma_common_regs, SGDMA_DESCRIPTOR_CONTROL_W1C_OFFSET, 1U << (halt_low_bit + channel_id));
    write_reg32 (ring->x2x_channel_regs, X2X_CHANNEL_CONTROL_W1S_OFFSET, X2X_CHANNEL_CONTROL_RUN);
}


/**
 * @brief When enabled by command line options apply an offset the allocated IOVAs to test IOMMU error handling
 * @param[in/out] descriptors_mapping Used for DMA descriptors. IOVA has read/write access.
 *                                    Write access needed for completion write-back.
 * @param[in/out] h2c_data_mapping Used for data transfers from the host to card. IOVA has read access.
 * @param[in/out] c2h_data_mapping Used for data transfers from the card to host. IOVA has write access.
 * @param[out] offsets_applied Set true if offsets have been applied.
 */
static void apply_iova_offsets (vfio_dma_mapping_t *const descriptors_mapping,
                                vfio_dma_mapping_t *const h2c_data_mapping,
                                vfio_dma_mapping_t *const c2h_data_mapping,
                                bool *const offsets_applied)
{
    *offsets_applied = false;
    if (arg_apply_iova_offsets)
    {
        if ((descriptors_mapping->buffer.allocation_type == VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY) ||
            (h2c_data_mapping->buffer.allocation_type    == VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY) ||
            (c2h_data_mapping->buffer.allocation_type    == VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY)   )
        {
            printf ("Applying IOVA offsets is disabled when physical memory is used, as may crash the PC\n");
        }
        else
        {
            printf ("Changing descriptors IOVA 0x%" PRIx64 "..0x%" PRIx64,
                    descriptors_mapping->iova, descriptors_mapping->iova + (descriptors_mapping->buffer.size - 1));
            descriptors_mapping->iova += arg_descriptors_iova_offset;
            printf (" -> 0x%" PRIx64 "..0x%" PRIx64 "\n",
                    descriptors_mapping->iova, descriptors_mapping->iova + (descriptors_mapping->buffer.size - 1));

            printf ("Changing h2c_data IOVA 0x%" PRIx64 "..0x%" PRIx64,
                    h2c_data_mapping->iova, h2c_data_mapping->iova + (h2c_data_mapping->buffer.size - 1));
            h2c_data_mapping->iova += arg_h2c_data_iova_offset;
            printf (" -> 0x%" PRIx64 "..0x%" PRIx64 "\n",
                    h2c_data_mapping->iova, h2c_data_mapping->iova + (h2c_data_mapping->buffer.size - 1));

            printf ("Changing c2h_data IOVA 0x%" PRIx64 "..0x%" PRIx64,
                    c2h_data_mapping->iova, c2h_data_mapping->iova + (c2h_data_mapping->buffer.size - 1));
            c2h_data_mapping->iova += arg_c2h_data_iova_offset;
            printf (" -> 0x%" PRIx64 "..0x%" PRIx64 "\n",
                    c2h_data_mapping->iova, c2h_data_mapping->iova + (c2h_data_mapping->buffer.size - 1));
            *offsets_applied = true;
        }
    }
}


/**
 * @brief Remove any IOVA offsets applied by apply_iova_offsets(), to allow the mappings to be freed
 * @param[in/out] descriptors_mapping Used for DMA descriptors.
 * @param[in/out] h2c_data_mapping Used for data transfers from the host to card.
 * @param[in/out] c2h_data_mapping Used for data transfers from the card to host.
 * @param[in] offsets_applied Set true if offsets need to be removed.
 */
static void remove_iova_offsets (vfio_dma_mapping_t *const descriptors_mapping,
                                 vfio_dma_mapping_t *const h2c_data_mapping,
                                 vfio_dma_mapping_t *const c2h_data_mapping,
                                 const bool offsets_applied)
{
    if (offsets_applied)
    {
        descriptors_mapping->iova -= arg_descriptors_iova_offset;
        h2c_data_mapping->iova -= arg_h2c_data_iova_offset;
        c2h_data_mapping->iova -= arg_c2h_data_iova_offset;
    }
}


/**
 * @brief Perform DMA tests on a DMA bridge with a memory mapped user interface.
 * @details A ring of descriptors is created and the DMA set to run.
 *          Descriptor credits are used to start the DMA transfers, where the descriptors are only populated
 *          just before descriptor credits are added.
 *          This is to investigate allowing the DMA to run and effectively make new descriptors available to the DMA engine.
 *          Only tests a single channel ID.
 * @param[in/out] designs The identified designs
 * @param[in] design_index Which FPGA design containing a DMA bridge to test
 * @return Returns true if the test has passed
 */
static bool test_memory_mapped_descriptor_rings (fpga_designs_t *const designs, const uint32_t design_index)
{
    const size_t page_size_bytes = (size_t) getpagesize ();
    const size_t page_size_words = page_size_bytes / sizeof (uint32_t);
    const uint32_t channel_id = 0;
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    bool iova_offsets_applied;
    descriptor_ring_t h2c_ring;
    descriptor_ring_t c2h_ring;
    size_t word_index;
    uint32_t descriptor_offset;
    bool c2h_descriptors_completed;
    bool success;

    fpga_design_t *const design = &designs->designs[design_index];
    vfio_device_t *const vfio_device = &designs->vfio_devices.devices[design_index];

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    uint8_t *const mapped_registers_base = get_dma_mapped_registers_base (design);
    if (mapped_registers_base == NULL)
    {
        return false;
    }

    /* Determine the total number of descriptors to test, with each one transferring one page of memory.
     * Attempts to select enough descriptors to transfer around the ring of descriptors for 3 iterations,
     * but may be less according to the amount of memory accessible by the DMA bridge. */
    const uint32_t num_pages_in_dma_bridge_memory = (uint32_t) (design->dma_bridge_memory_size_bytes / page_size_bytes);
    const uint32_t num_descriptors_per_ring = X2X_SGDMA_MAX_DESCRIPTOR_CREDITS;
    const uint32_t requested_ring_iterations = 3;
    const uint32_t requested_total_descriptors = num_descriptors_per_ring * requested_ring_iterations;
    const uint32_t total_descriptors = (num_pages_in_dma_bridge_memory < requested_total_descriptors) ?
            num_pages_in_dma_bridge_memory : requested_total_descriptors;

    const size_t total_memory_bytes = total_descriptors * page_size_bytes;
    const size_t total_memory_words = total_descriptors * page_size_words;

    /* Create read/write mapping for DMA descriptors, one ring for H2C and C2H host directions */
    const uint32_t num_rings = 1 /* H2C */ + 1 /* C2H */;
    const size_t total_descriptor_bytes_per_ring =
            vfio_align_cache_line_size (num_descriptors_per_ring * sizeof (dma_descriptor_t)) +
            vfio_align_cache_line_size (sizeof (completed_descriptor_count_writeback_t));
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping, num_rings * total_descriptor_bytes_per_ring,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Read mapping used by device, with each transfer limited to the maximum of the memory and command line argument */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, total_memory_bytes,
            VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Write mapping used by device, with each transfer limited to the maximum of the memory and command line argument */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, total_memory_bytes,
            VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    if ((descriptors_mapping.buffer.vaddr == NULL) ||
        (h2c_data_mapping.buffer.vaddr    == NULL) ||
        (c2h_data_mapping.buffer.vaddr    == NULL))
    {
        return false;
    }

    apply_iova_offsets (&descriptors_mapping, &h2c_data_mapping, &c2h_data_mapping, &iova_offsets_applied);

    uint32_t *const host_words = h2c_data_mapping.buffer.vaddr;
    uint32_t *const card_words = c2h_data_mapping.buffer.vaddr;
    uint32_t host_test_pattern = 0;
    uint32_t card_test_pattern = 0;

    /* Initialise the host memory buffers:
     * - host_words contains the test pattern to write in card memory
     * - card_words is initialised to the inverse of that expected, to check that does get transferred from the card to host */
    success = true;
    printf ("Testing %zu bytes of card memory, using rings with %" PRIu32 " descriptors, and a total of %" PRIu32 " descriptors\n",
            total_memory_bytes, num_descriptors_per_ring, total_descriptors);
    for (word_index = 0; word_index < total_memory_words; word_index++)
    {
        host_words[word_index] = host_test_pattern;
        card_words[word_index] = ~host_test_pattern;
        linear_congruential_generator (&host_test_pattern);
    }

    /* Initialise the rings, but don't populate the descriptors to actually perform DMA transfers */
    initialise_descriptor_ring (&h2c_ring, mapped_registers_base, DMA_SUBMODULE_H2C_CHANNELS, channel_id,
            num_descriptors_per_ring, &descriptors_mapping);
    initialise_descriptor_ring (&c2h_ring, mapped_registers_base, DMA_SUBMODULE_C2H_CHANNELS, channel_id,
            num_descriptors_per_ring, &descriptors_mapping);

    /* Perform the test, using DMA descriptors to transfer the test pattern:
     * a. From Host to Card Memory using h2c_ring
     * b. From Card to Host Memory using c2h_ring */
    uint32_t remaining_descriptors = total_descriptors;
    start_test_timeout ();
    while (success && (remaining_descriptors > 0))
    {
        const uint32_t nominal_descriptors_per_iteration = 5;
        const uint32_t num_descriptors_this_iteration = (remaining_descriptors < nominal_descriptors_per_iteration) ?
                remaining_descriptors : nominal_descriptors_per_iteration;

        /* Populate the H2C descriptors for this iteration, and make credits available for all in one write */
        for (descriptor_offset = 0; descriptor_offset < num_descriptors_this_iteration; descriptor_offset++)
        {
            dma_descriptor_t *const descriptor = &h2c_ring.descriptors[h2c_ring.next_descriptor_index];
            const uint64_t data_offset = (h2c_ring.started_descriptor_count + descriptor_offset) * page_size_bytes;

            descriptor->len = (uint32_t) page_size_bytes;
            descriptor->src_adr = h2c_data_mapping.iova + data_offset;
            descriptor->dst_adr = data_offset;
            h2c_ring.next_descriptor_index = (h2c_ring.next_descriptor_index + 1) % h2c_ring.num_descriptors;
        }
        write_reg32 (h2c_ring.x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, num_descriptors_this_iteration);
        h2c_ring.started_descriptor_count += num_descriptors_this_iteration;

        /* As the H2C descriptors complete, populate the C2H descriptors and make credits available
         * to transfer the test pattern from the card back to the host. */
        descriptor_offset = 0;
        while (success && (descriptor_offset < num_descriptors_this_iteration))
        {
            const uint32_t sts_err_compl_descriptor_count =
                    __atomic_load_n (&h2c_ring.completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);
            const uint32_t h2c_completed_descriptor_count =
                    sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;

            while (c2h_ring.started_descriptor_count < h2c_completed_descriptor_count)
            {
                dma_descriptor_t *const descriptor = &c2h_ring.descriptors[c2h_ring.next_descriptor_index];
                const uint64_t data_offset = c2h_ring.started_descriptor_count * page_size_bytes;

                descriptor->len = (uint32_t) page_size_bytes;
                descriptor->src_adr = data_offset;
                descriptor->dst_adr = c2h_data_mapping.iova + data_offset;
                c2h_ring.next_descriptor_index = (c2h_ring.next_descriptor_index + 1) % c2h_ring.num_descriptors;
                write_reg32 (c2h_ring.x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, 1);
                c2h_ring.started_descriptor_count++;
                descriptor_offset++;
            }

            if (h2c_completed_descriptor_count < h2c_ring.started_descriptor_count)
            {
                check_for_test_timeout (&success, "H2C descriptors to complete (started %" PRIu32 " completed %" PRIu32 " channel_status 0x%" PRIx32 ")",
                        h2c_ring.started_descriptor_count, h2c_completed_descriptor_count,
                        read_reg32 (h2c_ring.x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET));
            }
        }

        /* Wait for the C2H descriptors to complete */
        c2h_descriptors_completed = false;
        while (success && (!c2h_descriptors_completed))
        {
            const uint32_t sts_err_compl_descriptor_count =
                    __atomic_load_n (&c2h_ring.completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);
            const uint32_t c2h_completed_descriptor_count =
                    sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;

            if (c2h_completed_descriptor_count == c2h_ring.started_descriptor_count)
            {
                c2h_descriptors_completed = true;
            }
            else
            {
                check_for_test_timeout (&success, "C2H descriptors to complete (started %" PRIu32 " completed %" PRIu32 " channel_status 0x%" PRIx32 ")",
                        c2h_ring.started_descriptor_count, c2h_completed_descriptor_count,
                        read_reg32 (c2h_ring.x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET));
            }
        };

        remaining_descriptors -= num_descriptors_this_iteration;
    }

    /* Stop the channel running at the end of the test */
    write_reg32 (h2c_ring.x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);
    write_reg32 (c2h_ring.x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);

    /* Check the test pattern has been successfully transfered to the card words.
     * Done even if the failed to complete with a timeout, to indicate how much of the test pattern was successfully written. */
    bool pattern_valid = true;
    for (word_index = 0; pattern_valid && (word_index < total_memory_words); word_index++)
    {
        if (card_words[word_index] != card_test_pattern)
        {
            printf ("card_words[%zu] actual=0x%08" PRIx32 " expected=0x%08" PRIx32 "\n",
                    word_index, card_words[word_index], card_test_pattern);
            pattern_valid = false;
            success = false;
        }
        else
        {
            linear_congruential_generator (&card_test_pattern);
        }
    }

    remove_iova_offsets (&descriptors_mapping, &h2c_data_mapping, &c2h_data_mapping, iova_offsets_applied);
    free_vfio_dma_mapping (&c2h_data_mapping);
    free_vfio_dma_mapping (&h2c_data_mapping);
    free_vfio_dma_mapping (&descriptors_mapping);

    return success;
}


/**
 * @brief Perform DMA tests on a DMA bridge with a AXI4 Stream interface.
 * @details This uses two AXI4 Stream interfaces looped back in the FPGA to test transferring variable length
 *          messages via descriptor rings.
 *          The receive descriptor ring uses a number of fixed size buffers, and the messages can split across
 *          multiple buffers with the final buffer for a message partially populated.
 *
 *          The transmit ring has the same number and size of buffers as the receive descriptor ring.
 *
 *          The receive ring is kept toped-up with a full set of credits, as with a stream interface can have
 *          descriptors waiting to receive data.
 *
 *          As the receive ring descriptors aren't changed while running the test, could potentially make
 *          use of Nxt_adj to optimise descriptor fetching. Albeit this is a functional test which doesn't
 *          measure any performance.
 * @param[in/out] designs The identified designs
 * @param[in] design_index Which FPGA design containing a DMA bridge to test
 * @param[in] h2c_channel_id Which DMA bridge channel to use for AXI4 stream writes (Host To Card)
 * @param[in] c2h_channel_id Which DMA bridge channel to use for AXI4 stream reads (Card To Host)
 * @return Returns true if the test has passed
 */
static bool test_stream_descriptor_rings (fpga_designs_t *const designs, const uint32_t design_index,
                                          const uint32_t h2c_channel_id, const uint32_t c2h_channel_id)
{
    const uint32_t page_size_bytes = (uint32_t) getpagesize ();
    const uint32_t page_size_words = page_size_bytes / sizeof (uint32_t);
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_data_mapping;
    vfio_dma_mapping_t c2h_data_mapping;
    bool iova_offsets_applied;
    descriptor_ring_t h2c_ring;
    descriptor_ring_t c2h_ring;
    uint32_t descriptor_offset;
    uint32_t remaining_message_words;
    uint32_t word_index;
    bool h2c_completed;
    bool dma_completion_success;
    bool test_pattern_success;
    uint32_t message_index;

    fpga_design_t *const design = &designs->designs[design_index];
    vfio_device_t *const vfio_device = &designs->vfio_devices.devices[design_index];

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    uint8_t *const mapped_registers_base = get_dma_mapped_registers_base (design);
    if (mapped_registers_base == NULL)
    {
        return false;
    }

    /* This test transmits variable length messages using the streams.
     * Each descriptor is used to transfer a maximum of one page.
     * The message length starts just below the length of one page and is incremented for each message,
     * which means most messages are split across multiple descriptors. */
    const uint32_t num_descriptors_per_ring = X2X_SGDMA_MAX_DESCRIPTOR_CREDITS;
    const uint32_t min_ring_iterations = 3;
    const uint32_t total_messages = min_ring_iterations * num_descriptors_per_ring;

    const size_t total_memory_bytes = total_messages * page_size_bytes;

    /* Create read/write mapping for DMA descriptors, one ring for H2C and C2H host directions */
    const size_t total_descriptor_bytes_per_h2c_ring =
            vfio_align_cache_line_size (num_descriptors_per_ring * sizeof (dma_descriptor_t)) +
            vfio_align_cache_line_size (sizeof (completed_descriptor_count_writeback_t));
    const size_t total_descriptor_bytes_per_c2h_ring = total_descriptor_bytes_per_h2c_ring +
            vfio_align_cache_line_size (num_descriptors_per_ring * sizeof (c2h_stream_writeback_t));
    allocate_vfio_dma_mapping (vfio_device, &descriptors_mapping,
            total_descriptor_bytes_per_h2c_ring + total_descriptor_bytes_per_c2h_ring,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Read mapping used by device, with each transfer limited to the maximum of the memory and command line argument */
    allocate_vfio_dma_mapping (vfio_device, &h2c_data_mapping, total_memory_bytes,
            VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Write mapping used by device, with each transfer limited to the maximum of the memory and command line argument */
    allocate_vfio_dma_mapping (vfio_device, &c2h_data_mapping, total_memory_bytes,
            VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    if ((descriptors_mapping.buffer.vaddr == NULL) ||
        (h2c_data_mapping.buffer.vaddr    == NULL) ||
        (c2h_data_mapping.buffer.vaddr    == NULL))
    {
        return false;
    }

    apply_iova_offsets (&descriptors_mapping, &h2c_data_mapping, &c2h_data_mapping, &iova_offsets_applied);

    /* Initialise the rings, but don't populate the descriptors to actually perform DMA transfers */
    initialise_descriptor_ring (&h2c_ring, mapped_registers_base, DMA_SUBMODULE_H2C_CHANNELS, h2c_channel_id,
            num_descriptors_per_ring, &descriptors_mapping);
    initialise_descriptor_ring (&c2h_ring, mapped_registers_base, DMA_SUBMODULE_C2H_CHANNELS, c2h_channel_id,
            num_descriptors_per_ring, &descriptors_mapping);

    /* Initialise the C2H descriptors to point at the ring of buffers of fixed sizes, and start the receive DMA */
    for (uint32_t descriptor_index = 0; descriptor_index < c2h_ring.num_descriptors; descriptor_index++)
    {
        dma_descriptor_t *const descriptor = &c2h_ring.descriptors[descriptor_index];

        descriptor->len = (uint32_t) page_size_bytes;
        descriptor->dst_adr = c2h_data_mapping.iova + (descriptor_index * page_size_bytes);
    }
    write_reg32 (c2h_ring.x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, c2h_ring.num_descriptors);
    c2h_ring.started_descriptor_count += c2h_ring.num_descriptors;

    /* Perform a test using messages of increasing length */
    uint32_t message_length_words = page_size_words - 1;
    size_t total_message_words = 0;
    size_t total_message_descriptors = 0;
    uint32_t *const host_words = h2c_data_mapping.buffer.vaddr;
    uint32_t *const card_words = c2h_data_mapping.buffer.vaddr;
    uint32_t host_test_pattern = 0;
    uint32_t card_test_pattern = 0;
    uint32_t num_processed_c2h_descriptors = 0;
    dma_completion_success = true;
    test_pattern_success = true;
    start_test_timeout ();
    message_index = 0;
    while (dma_completion_success && (message_index < total_messages))
    {
        const uint32_t num_descriptors_for_message = (message_length_words + (page_size_words - 1)) / page_size_words;

        /* Populate all descriptors for the message, and then transmit the message */
        remaining_message_words = message_length_words;
        for (descriptor_offset = 0; descriptor_offset < num_descriptors_for_message; descriptor_offset++)
        {
            const uint32_t word_offset = page_size_words * h2c_ring.next_descriptor_index;
            const uint32_t num_words_in_descriptor = (remaining_message_words < page_size_words) ?
                    remaining_message_words : page_size_words;
            dma_descriptor_t *const descriptor = &h2c_ring.descriptors[h2c_ring.next_descriptor_index];

            descriptor->src_adr = h2c_data_mapping.iova + (h2c_ring.next_descriptor_index * page_size_bytes);
            descriptor->len = num_words_in_descriptor * sizeof (uint32_t);
            if (num_words_in_descriptor == remaining_message_words)
            {
                descriptor->magic_nxt_adj_control |= DMA_DESCRIPTOR_CONTROL_EOP;
            }
            else
            {
                descriptor->magic_nxt_adj_control &= ~DMA_DESCRIPTOR_CONTROL_EOP;
            }
            for (word_index = 0; word_index < num_words_in_descriptor; word_index++)
            {
                host_words[word_offset + word_index] = host_test_pattern;
                linear_congruential_generator (&host_test_pattern);
            }
            h2c_ring.next_descriptor_index = (h2c_ring.next_descriptor_index + 1) % h2c_ring.num_descriptors;
            remaining_message_words -= num_words_in_descriptor;
        }
        write_reg32 (h2c_ring.x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, num_descriptors_for_message);
        h2c_ring.started_descriptor_count += num_descriptors_for_message;

        /* Receive the message, split over one or more descriptors */
        remaining_message_words = message_length_words;
        for (descriptor_offset = 0; dma_completion_success && (descriptor_offset < num_descriptors_for_message); descriptor_offset++)
        {
            const uint32_t word_offset = page_size_words * c2h_ring.next_descriptor_index;
            const uint32_t num_words_in_descriptor = (remaining_message_words < page_size_words) ?
                    remaining_message_words : page_size_words;
            const bool expected_eop = num_words_in_descriptor == remaining_message_words;
            const uint32_t expected_length = num_words_in_descriptor * sizeof (uint32_t);
            c2h_stream_writeback_t *const stream_writeback = &c2h_ring.stream_writeback[c2h_ring.next_descriptor_index];

            /* Wait for the next C2H descriptor to complete */
            bool c2h_descriptor_populated = false;
            while (dma_completion_success && (!c2h_descriptor_populated))
            {
                const uint32_t sts_err_compl_descriptor_count =
                        __atomic_load_n (&c2h_ring.completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);
                const uint32_t c2h_completed_descriptor_count =
                        sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;

                if (c2h_completed_descriptor_count > num_processed_c2h_descriptors)
                {
                    c2h_descriptor_populated = true;
                }
                else
                {
                    check_for_test_timeout (&dma_completion_success, "C2H descriptor to complete (processed %" PRIu32 " completed %" PRIu32 " channel_status 0x%" PRIx32 ")",
                            num_processed_c2h_descriptors, c2h_completed_descriptor_count,
                            read_reg32 (c2h_ring.x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET));

                    if (!dma_completion_success)
                    {
                        /* The H2C descriptor has to complete before the C2H descriptor can complete.
                         * In the event the the C2H descriptor fails to complete, also report the status of the H2C descriptor. */
                        const uint32_t sts_err_compl_descriptor_count =
                                __atomic_load_n (&h2c_ring.completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);
                        const uint32_t h2c_completed_descriptor_count =
                                sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;

                        if (h2c_completed_descriptor_count == h2c_ring.started_descriptor_count)
                        {
                            printf ("H2C had completed\n");
                        }
                        else
                        {
                            printf ("Test timeout waiting for H2C descriptors to complete (started %" PRIu32 " completed %" PRIu32 " channel_status 0x%" PRIx32 ")\n",
                                    h2c_ring.started_descriptor_count, h2c_completed_descriptor_count,
                                    read_reg32 (h2c_ring.x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET));
                        }
                    }
                }
            }

            if (dma_completion_success)
            {
                /* A receive descriptor is available. Check:
                 * a. The stream writeback has the expected magic value.
                 * b. The stream writeback length is the expected value (may be a partially filled descriptor)
                 * c. The End-Of-Packet indication indicates is set only for the last descriptor of a message.
                 * d. The data contents contains the expected test pattern. */
                const bool actual_eop = (stream_writeback->wb_magic_status & CH2_STREAM_WB_EOP) != 0;

                if ((stream_writeback->wb_magic_status & C2H_STREAM_WB_MAGIC_MASK) != C2H_STREAM_WB_MAGIC)
                {
                    printf ("Incorrect stream wb_magic_status 0x%" PRIx32 "\n", stream_writeback->wb_magic_status);
                    dma_completion_success = false;
                }
                else if (actual_eop != expected_eop)
                {
                    printf ("Incorrect EOP actual %d expected %d\n", actual_eop, expected_eop);
                    dma_completion_success = false;
                }
                else if (stream_writeback->length != expected_length)
                {
                    printf ("Incorrect length actual %" PRIu32 " expected %" PRIu32 "\n",
                            stream_writeback->length, expected_length);
                    dma_completion_success = false;
                }
                else
                {
                    /* Check the test pattern has been successfully transfered to the card words.
                     * Stops checking the test pattern after the first failure, but allow DMA to continue.
                     * This is tell the difference between errors which cause the transferred data to be incorrect,
                     * v.s.errors which cause the DMA to fail to complete. */
                    for (word_index = 0; test_pattern_success && (word_index < num_words_in_descriptor); word_index++)
                    {
                        if (card_words[word_offset + word_index] != card_test_pattern)
                        {
                            printf ("card_words[%" PRIu32 "] actual=0x%08" PRIx32 " expected=0x%08" PRIx32 "\n",
                                    word_index, card_words[word_index], card_test_pattern);
                            test_pattern_success = false;
                        }
                        else
                        {
                            linear_congruential_generator (&card_test_pattern);
                        }
                    }
                }
            }

            if (dma_completion_success)
            {
                /* Clear the writeback for the H2C descriptor and re-start it */
                stream_writeback->wb_magic_status = 0;
                stream_writeback->length = 0;
                c2h_ring.next_descriptor_index = (c2h_ring.next_descriptor_index + 1) % c2h_ring.num_descriptors;
                write_reg32 (c2h_ring.x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, 1);
                c2h_ring.started_descriptor_count++;
                num_processed_c2h_descriptors++;
                remaining_message_words -= num_words_in_descriptor;
            }
        }

        /* Ensure the H2C descriptors have completed. This is not expected to have to wait. */
        h2c_completed = false;
        while (dma_completion_success && (!h2c_completed))
        {
            const uint32_t sts_err_compl_descriptor_count =
                    __atomic_load_n (&h2c_ring.completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);
            const uint32_t h2c_completed_descriptor_count =
                    sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;

            if (h2c_completed_descriptor_count == h2c_ring.started_descriptor_count)
            {
                h2c_completed = true;
            }
            else
            {
                check_for_test_timeout (&dma_completion_success, "H2C descriptors to complete (started %" PRIu32 " completed %" PRIu32 " channel_status 0x%" PRIx32 ")",
                        h2c_ring.started_descriptor_count, h2c_completed_descriptor_count,
                        read_reg32 (h2c_ring.x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET));
            }
        }

        total_message_words += message_length_words;
        total_message_descriptors += num_descriptors_for_message;
        message_length_words++;

        if (dma_completion_success)
        {
            message_index++;
        }
    }

    /* Stop the channel running at the end of the test */
    write_reg32 (h2c_ring.x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);
    write_reg32 (c2h_ring.x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);

    const bool success = dma_completion_success && test_pattern_success;
    if (success)
    {
        printf ("Successfully sent %" PRIu32 " messages from Ch%" PRIu32 "->%" PRIu32 " with a total of %zu words in %zu descriptors\n",
                total_messages, h2c_channel_id, c2h_channel_id, total_message_words, total_message_descriptors);
    }
    else
    {
        printf ("Failed after %" PRIu32 " out of %" PRIu32 " messages transferred by DMA\n",
                message_index, total_messages);
    }

    remove_iova_offsets (&descriptors_mapping, &h2c_data_mapping, &c2h_data_mapping, iova_offsets_applied);
    free_vfio_dma_mapping (&c2h_data_mapping);
    free_vfio_dma_mapping (&h2c_data_mapping);
    free_vfio_dma_mapping (&descriptors_mapping);

    return success;
}


/**
 * @brief Perform a test of applying a VFIO reset, to see the effect on a sample of DMA bridge registers.
 * @param[in/out] designs The identified designs
 * @param[in] design_index Which FPGA design containing a DMA bridge to test
 */
static void test_vfio_reset (fpga_designs_t *const designs, const uint32_t design_index)
{
    const uint32_t channel_id = 0;
    fpga_design_t *const design = &designs->designs[design_index];
    vfio_device_t *const vfio_device = &designs->vfio_devices.devices[design_index];
    const char *const iteration_names[] =
    {
        "After VFIO open",
        "After setting values",
        "After VFIO reset"
    };

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    uint8_t *const mapped_registers_base = get_dma_mapped_registers_base (design);
    if (mapped_registers_base == NULL)
    {
        return;
    }

    uint8_t *const x2x_channel_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (DMA_SUBMODULE_H2C_CHANNELS, channel_id)];
    uint8_t *const x2x_sgdma_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (DMA_SUBMODULE_H2C_SGDMA, channel_id)];
    uint8_t *const sgdma_common_regs = &mapped_registers_base[DMA_SUBMODULE_BAR_START_OFFSET (DMA_SUBMODULE_SGDMA_COMMON)];

    /* Report the register values at different stages */
    for (uint32_t iteration = 0; iteration < 3; iteration++)
    {
        printf ("%s:\n", iteration_names[iteration]);
        printf ("  control %08" PRIx32 "  status %08" PRIx32 "  credits %04" PRIx32 "  addr %016" PRIx64 "\n",
                read_reg32 (x2x_channel_regs, X2X_CHANNEL_CONTROL_RW_OFFSET),
                read_reg32 (x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET),
                read_reg32 (x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET),
                read_split_reg64 (x2x_channel_regs, X2X_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET));

        switch (iteration)
        {
        case 0:
            /* Initialise the register values */
            {
                /* Halt descriptor fetches for the channel as need to set the channel running to test adding credits,
                 * but this test doesn't setup any actual descriptors. */
                write_reg32 (sgdma_common_regs, SGDMA_DESCRIPTOR_CONTROL_W1S_OFFSET, 1U << (SGDMA_DESCRIPTOR_H2C_DSC_HALT_LOW_BIT + channel_id));

                /* Enable reporting of all errors */
                const uint32_t all_errors =
                        X2C_CHANNEL_CONTROL_IE_DESC_ERROR |
                        X2X_CHANNEL_CONTROL_IE_READ_ERROR |
                        X2X_CHANNEL_CONTROL_IE_INVALID_LENGTH |
                        X2X_CHANNEL_CONTROL_IE_MAGIC_STOPPED |
                        X2X_CHANNEL_CONTROL_IE_ALIGN_MISMATCH |
                        H2C_CHANNEL_CONTROL_IE_WRITE_ERROR;
                write_reg32 (x2x_channel_regs, X2X_CHANNEL_CONTROL_RW_OFFSET, all_errors);

                /* Enable descriptor credits for the channel */
                write_reg32 (sgdma_common_regs, SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET,
                        1U << (SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT + channel_id));

                /* Write some credits */
                write_reg32 (x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, 0x1fd);

                /* Set a write back address */
                write_split_reg64 (x2x_channel_regs, X2X_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET, 0xfeedabbadeadbeef);

                /* Set the channel running, but with actual descriptor fetches halted */
                write_reg32 (x2x_channel_regs, X2X_CHANNEL_CONTROL_W1S_OFFSET, X2X_CHANNEL_CONTROL_RUN);
            }
            break;

        case 1:
            reset_vfio_device (vfio_device);
            break;
        }
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    bool success;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Process any FPGA designs which have a DMA bridge */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->dma_bridge_present)
        {
            if (arg_test_a32_dma_capability)
            {
                designs.vfio_devices.devices[design_index].dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A32;
            }

            if (design->dma_bridge_memory_size_bytes > 0)
            {
                printf ("Testing DMA bridge bar %u memory size 0x%zx\n", design->dma_bridge_bar, design->dma_bridge_memory_size_bytes);
            }
            else
            {
                printf ("Testing DMA bridge bar %u AXI Stream\n", design->dma_bridge_bar);
            }

            if (arg_test_vfio_reset)
            {
                test_vfio_reset (&designs, design_index);
            }

            success = test_dma_credit_incrementing (design);

            if (success)
            {
                if (design->dma_bridge_memory_size_bytes > 0)
                {
                    success = test_memory_mapped_descriptor_rings (&designs, design_index);
                }
                else
                {
                    /* Assumes the DMA bridge has two streams, which are cross-connected internally in the FPGA.
                     * Test both pairs of streams. */
                    success = test_stream_descriptor_rings (&designs, design_index, 0, 1) &&
                            test_stream_descriptor_rings (&designs, design_index, 1, 0);
                }
            }

            printf ("Test: %s\n", success ? "PASS" : "FAIL");
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
