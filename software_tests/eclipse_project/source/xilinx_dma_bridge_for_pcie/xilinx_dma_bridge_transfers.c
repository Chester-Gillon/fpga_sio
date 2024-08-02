/*
 * @file xilinx_dma_bridge_transfers.c
 * @date 4 Feb 2023
 * @author Chester Gillon
 * @brief Provides transfers between the Host and Card using the Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details
 *   Uses VFIO to be able to perform the DMA entirely in user space, in polling mode.
 *
 *   Implements support for Memory Mapped and stream AXI4 endpoints.
 *
 *   Creates a "ring" of DMA descriptors to allow multiple transfers to be outstanding at once. The DMA engine is
 *   left running continuously and either:
 *   a. Descriptors are started by issuing credits to the DMA engine.
 *   b. For a CH2 AXI stream the DMA engine can be configured to continuously perform transfers to a ring of fixed
 *      size buffers without software interaction. In this case the software has to keep up with the completed
 *      transfers so the data in the host buffers isn't overwritten before it has been processed.
 *
 *   The version in the identifier register is not checked. This file has been written based upon PG195 (v4.1)
 */

#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"

#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>


/**
 * @brief Record a DMA transfer failure, setting the error message on the first failure
 * @param[in/out] context The transfer content to record the failure on
 * @param[in] format printf style format string for error message.
 * @param[in] ... printf arguments
 */
void x2x_record_failure (x2x_transfer_context_t *const context, const char *format, ...)
{
    if (!context->failed)
    {
        va_list args;

        va_start (args, format);
        vsnprintf (context->error_message, sizeof (context->error_message), format, args);
        va_end (args);
        context->failed = true;
        *context->configuration.overall_success = false;
    }
}


/**
 * @brief Check an assertion which detects a programming error
 * @param[in/out] context The transfer content to record an assertion failure on
 * @param[in] assertion Must be true to indicate correct operation
 */
void x2x_assert (x2x_transfer_context_t *const context, const bool assertion, const char *const assertion_message)
{
    if (!assertion)
    {
        x2x_record_failure (context, "Assertion failed: %s", assertion_message);
    }
}


/**
 * @brief Check the identity register value for one DMA submodule
 * @details This is a sanity check that the DMA control registers have been mapped correctly
 * @param[in/out] context The transfer content to check the identity for
 * @param[in] expected_submodule The expected submodule
 */
static void x2x_check_dma_submodule_identity (x2x_transfer_context_t *const context, const uint32_t expected_submodule)
{
    const uint8_t *submodule_regs;

    /* Select the submodule register base to use */
    switch (expected_submodule)
    {
    case DMA_SUBMODULE_H2C_CHANNELS:
    case DMA_SUBMODULE_C2H_CHANNELS:
        submodule_regs = context->x2x_channel_regs;
        break;

    case DMA_SUBMODULE_H2C_SGDMA:
    case DMA_SUBMODULE_C2H_SGDMA:
        submodule_regs = context->x2x_sgdma_regs;
        break;

    case DMA_SUBMODULE_SGDMA_COMMON:
        submodule_regs = context->sgdma_common_regs;
        break;

    default:
        X2X_ASSERT (context, false);
        return;
        break;
    }

    const uint32_t identity_reg_value = read_reg32 (submodule_regs, SUBMODULE_IDENTIFIER_OFFSET);
    const uint32_t subsystem_ip =
            (identity_reg_value & SUBMODULE_IDENTIFIER_SUBSYSTEM_MASK) >> SUBMODULE_IDENTIFIER_SUBSYSTEM_SHIFT;
    const uint32_t actual_submodule = (identity_reg_value & SUBMODULE_IDENTIFIER_TARGET_MASK) >> SUBMODULE_IDENTIFIER_TARGET_SHIFT;
    const bool is_axi4_stream = (identity_reg_value & SUBMODULE_IDENTIFIER_STREAM_MASK) != 0;
    const uint32_t actual_channel_id =
            (identity_reg_value & SUBMODULE_IDENTIFIER_CHANNEL_ID_MASK) >> SUBMODULE_IDENTIFIER_CHANNEL_ID_SHIFT;

    if (subsystem_ip != SUBMODULE_IDENTIFIER_SUBSYSTEM_ID)
    {
        x2x_record_failure (context, "For expected_submodule %" PRIu32 " unexpected subsystem ID 0x%" PRIx32,
                expected_submodule, subsystem_ip);
    }

    if (actual_submodule != expected_submodule)
    {
        x2x_record_failure (context, "expected_submodule %" PRIu32 ", but actual_submodule %" PRIu32,
                expected_submodule, actual_submodule);
    }

    switch (expected_submodule)
    {
    case DMA_SUBMODULE_H2C_CHANNELS:
    case DMA_SUBMODULE_C2H_CHANNELS:
    case DMA_SUBMODULE_H2C_SGDMA:
    case DMA_SUBMODULE_C2H_SGDMA:
        /* Validate per-channel submodule */
        if (is_axi4_stream != context->is_axi_stream)
        {
            x2x_record_failure (context, "For submodule %" PRIu32 " endpoint is AXI4 %s, but expected AXI4 %s",
                    expected_submodule,
                    is_axi4_stream ? "stream" : "memory mapped",
                    context->is_axi_stream ? "stream" : "memory mapped");
        }

        if (actual_channel_id != context->configuration.channel_id)
        {
            x2x_record_failure (context, "expected_submodule %" PRIu32 " actual_channel_id %" PRIu32 " expected_channel_id %" PRIu32,
                    expected_submodule, actual_channel_id, context->configuration.channel_id);
        }
        break;
    }
}


/**
 * @brief Perform initialisation for channel control register mapping which doesn't use any descriptor ring information.
 * @param[out] context The partially initialised context. failed set means didn't didn't find the expected register identities.
 * @param[in] configuration The configuration to be used for the DMA transfers.
 */
static void x2x_initialise_transfer_register_mapping (x2x_transfer_context_t *const context,
                                                      const x2x_transfer_configuration_t *const configuration)
{
    const uint32_t sgdma_channels_submodule = (configuration->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            DMA_SUBMODULE_H2C_SGDMA : DMA_SUBMODULE_C2H_SGDMA;

    /* Store the caller supplied information in the context */
    memset (context, 0, sizeof (*context));
    context->configuration = *configuration;
    context->is_axi_stream = context->configuration.dma_bridge_memory_size_bytes == 0;

    /* Initially no failure detected */
    context->failed = false;
    context->timeout_awaiting_idle_at_finalisation = false;

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    const size_t dma_control_base_offset = 0x0;
    const size_t dma_control_frame_size = 0x10000;
    uint8_t *const mapped_registers_base =
            map_vfio_registers_block (context->configuration.vfio_device, context->configuration.bar_index,
                    dma_control_base_offset, dma_control_frame_size);
    if (mapped_registers_base == NULL)
    {
        x2x_record_failure (context, "BAR[%" PRIu32 " size of 0x%llx too small for DMA/Bridge Subsystem for PCI Express",
                context->configuration.bar_index,
                context->configuration.vfio_device->regions_info[context->configuration.bar_index].size);
        return;
    }

    /* Set the mapped base of the DMA control registers used for the channel */
    context->x2x_channel_regs = &mapped_registers_base
            [DMA_CHANNEL_BAR_START_OFFSET (context->configuration.channels_submodule, context->configuration.channel_id)];
    context->x2x_sgdma_regs = &mapped_registers_base
            [DMA_CHANNEL_BAR_START_OFFSET (sgdma_channels_submodule, context->configuration.channel_id)];
    context->sgdma_common_regs = &mapped_registers_base[DMA_SUBMODULE_BAR_START_OFFSET (DMA_SUBMODULE_SGDMA_COMMON)];

    /* Verify the identity of the DMA submodules used for the channel */
    x2x_check_dma_submodule_identity (context, context->configuration.channels_submodule);
    x2x_check_dma_submodule_identity (context, sgdma_channels_submodule);
    x2x_check_dma_submodule_identity (context, DMA_SUBMODULE_SGDMA_COMMON);
    if (context->failed)
    {
        return;
    }

    /* Obtain the alignment requirements of the DMA engine */
    const uint32_t alignment_reg_value = read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_ALIGNMENTS_OFFSET);
    context->addr_alignment =
            (alignment_reg_value & X2X_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_MASK) >> X2X_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_SHIFT;
    context->len_granularity =
            (alignment_reg_value & X2X_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_MASK) >> X2X_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_SHIFT;
    context->num_address_bits =
            (alignment_reg_value & X2X_CHANNEL_ALIGNMENTS_ADDRESS_BITS_MASK) >> X2X_CHANNEL_ALIGNMENTS_ADDRESS_BITS_SHIFT;
}


/**
 * @brief Get the number of channels configured in a Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details This probes increasing channel IDs to determine how many respond with the expected identification values.
 *          The IP allows the number of channels in the H2C and C2H direction to be configured independently which is the
 *          reason for the two outputs.
 *
 *          The dma_bridge_memory_size_bytes parameter is required, as used to determine the expected identification values
 *          depending upon if the channels are configured for memory access or AXI streams.
 *
 *          Only reads the channel ID registers, so safe to be called if a the DMA channels are already actively performing
 *          transfers.
 * @param[in/out] vfio_device Used to obtain access to the memory mapped BAR containing the DMA control registers
 * @param[in] bar_index Which BAR in the vfio_device contains the DMA control registers
 * @param[in] dma_bridge_memory_size_bytes The amount of memory addressed by the DMA/Bridge Subsystem:
 *                                         - Non-zero means "AXI memory mapped" channels are expected.
 *                                         - Zero means ""AXI Stream" channels are expected.
 * @param[out] num_h2c_channels The number of configured H2C channels
 * @param[out] num_c2h_channels The number of configured C2H channels
 * @param[out] h2c_transfers, c2h_transfers
 *             If non-NULL contains the partial transfer contexts from determining the number of channels.
 *             This contains:
 *             a. The mapped register base addresses
 *             b. The alignment requirements
 */
void x2x_get_num_channels (vfio_device_t *const vfio_device, const uint32_t bar_index, const size_t dma_bridge_memory_size_bytes,
                           uint32_t *const num_h2c_channels, uint32_t *const num_c2h_channels,
                           x2x_transfer_context_t h2c_transfers[const X2X_MAX_CHANNELS],
                           x2x_transfer_context_t c2h_transfers[const X2X_MAX_CHANNELS])
{
    x2x_transfer_context_t context;
    bool success;
    x2x_transfer_configuration_t configuration =
    {
        .vfio_device = vfio_device,
        .bar_index = bar_index,
        .dma_bridge_memory_size_bytes = dma_bridge_memory_size_bytes,
        .overall_success = &success
    };

    *num_h2c_channels = 0;
    configuration.channel_id = 0;
    configuration.channels_submodule = DMA_SUBMODULE_H2C_CHANNELS;
    x2x_initialise_transfer_register_mapping (&context, &configuration);
    while (!context.failed && (*num_h2c_channels < X2X_MAX_CHANNELS))
    {
        if (h2c_transfers != NULL)
        {
            h2c_transfers[*num_h2c_channels] = context;
        }
        (*num_h2c_channels)++;
        configuration.channel_id++;
        x2x_initialise_transfer_register_mapping (&context, &configuration);
    }

    *num_c2h_channels = 0;
    configuration.channel_id = 0;
    configuration.channels_submodule = DMA_SUBMODULE_C2H_CHANNELS;
    x2x_initialise_transfer_register_mapping (&context, &configuration);
    while (!context.failed && (*num_c2h_channels < X2X_MAX_CHANNELS))
    {
        if (c2h_transfers != NULL)
        {
            c2h_transfers[*num_c2h_channels] = context;
        }
        (*num_c2h_channels)++;
        configuration.channel_id++;
        x2x_initialise_transfer_register_mapping (&context, &configuration);
    }
}


/**
 * @brief Get the size in bytes to be allocated for descriptors for a particular configuration.
 * @details Used to allow
 * @param[in] configuration The configuration to be used for the DMA transfers.
 * @return The number of bytes of descriptors required for the configuration
 */
size_t x2x_get_descriptor_allocation_size (const x2x_transfer_configuration_t *const configuration)
{
    /* The ring of descriptors */
    size_t allocation_size = vfio_align_cache_line_size (configuration->num_descriptors * sizeof (dma_descriptor_t));

    /* Used to monitor descriptors as they complete */
    allocation_size += vfio_align_cache_line_size (sizeof (completed_descriptor_count_writeback_t));

    /* For a C2H AXI stream, for each descriptor a writeback is allocated to store the amount of data written */
    if ((configuration->dma_bridge_memory_size_bytes == 0) && (configuration->channels_submodule == DMA_SUBMODULE_C2H_CHANNELS))
    {
        allocation_size += vfio_align_cache_line_size (configuration->num_descriptors * sizeof (c2h_stream_writeback_t));
    }

    return allocation_size;
}


/**
 * @brief Get the number of descriptors required for a given transfer length, allowing for the maximum length of one descriptor
 * @param[in] len The transfer length in bytes
 * @return The number of descriptors required for len
 */
uint32_t x2x_num_descriptors_for_transfer_len (const size_t len)
{
    return (uint32_t) ((len + (X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN - 1)) / X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN);
}


/**
 * @brief Perform validation checks on the configuration for performing DMA transfers using one H2C or 2CH channel
 * @param[in/out] context Contains the configuration to validate
 */
static void x2x_validate_transfer_configuration (x2x_transfer_context_t *const context)
{
    /* Minimum number of descriptors requires is one */
    if (context->configuration.num_descriptors == 0)
    {
        x2x_record_failure (context, "No descriptors specified");
    }

    /* When using an array of fixed size buffers, each buffer can't exceed the length of one descriptor,
     * since there is one buffer per descriptor */
    if (context->configuration.bytes_per_buffer > DMA_DESCRIPTOR_MAX_LEN)
    {
        x2x_record_failure (context, "bytes_per_buffer exceeds the maximum for one descriptor");
    }

    /* Perform validation specific to c2h_stream_continuous mode */
    if (context->configuration.c2h_stream_continuous)
    {
        /* Check has only been enabled for a C2H AXI stream. Allows conditional tests on just c2h_stream_continuous */
        if (!context->is_axi_stream || (context->configuration.channels_submodule != DMA_SUBMODULE_C2H_CHANNELS))
        {
            x2x_record_failure (context, "c2h_stream_continuous can only be used on an AXI stream C2H channel");
        }

        /* C2H stream continuous mode requires buffers to be specified (optional in other modes) */
        if (context->configuration.bytes_per_buffer == 0)
        {
            x2x_record_failure (context, "bytes_per_buffer must be specified to use h2c_stream_continuous mode");
        }
    }
    else
    {
        /* For a mode which uses descriptor credits, check can't exceed the maximum.
         * This is to avoid getting the number of queued transfers out of step with the DMA engine. */
        if (context->configuration.num_descriptors > X2X_SGDMA_MAX_DESCRIPTOR_CREDITS)
        {
            x2x_record_failure (context, "num_descriptors exceeds the maximum number of credits");
        }
    }

    /* Perform validation when buffers set at initialisation */
    if (context->configuration.bytes_per_buffer != 0)
    {
        /* Validate alignment */
        if ((context->configuration.bytes_per_buffer % context->addr_alignment) != 0)
        {
            x2x_record_failure (context, "The configuration bytes_per_buffer doesn't meet the addr_alignment");
        }

        if ((!context->is_axi_stream) && ((context->configuration.card_buffer_start_offset % context->addr_alignment) != 0))
        {
            x2x_record_failure (context, "The configuration card_buffer_start_offset doesn't meet the addr_alignment");
        }

        /* Check host buffer is large enough */
        const size_t required_host_buffer_size = context->configuration.host_buffer_start_offset +
                (context->configuration.num_descriptors * context->configuration.bytes_per_buffer);
        if (context->configuration.data_mapping->buffer.size < required_host_buffer_size)
        {
            x2x_record_failure (context, "Host buffer too small");
        }

        if (!context->is_axi_stream)
        {
            /* When memory mapped check the card memory is large enough */
            const size_t required_card_memory_size = context->configuration.card_buffer_start_offset +
                    (context->configuration.num_descriptors * context->configuration.bytes_per_buffer);
            if (context->configuration.dma_bridge_memory_size_bytes < required_card_memory_size)
            {
                x2x_record_failure (context, "Card memory too small");
            }
        }
    }
}


/**
 * @brief Initialise the context for performing DMA transfers using one H2C or 2CH channel
 * @param[out] context The initialised context. failed is set to indicate the initialisation failed.
 * @param[in] configuration The configuration to be used for the DMA transfers.
 */
void x2x_initialise_transfer_context (x2x_transfer_context_t *const context,
                                      const x2x_transfer_configuration_t *const configuration)
{
    uint64_t first_descriptor_iova;
    uint64_t first_stream_writeback_iova;
    uint64_t completed_descriptor_count_iova;

    /* Perform initialisation for channel control register mapping which doesn't use any descriptor ring information.
     * This validates the control registers for the channel are found with the expected identification values. */
    x2x_initialise_transfer_register_mapping (context, configuration);
    if (context->failed)
    {
        return;
    }

    /* Initialise to no descriptors used */
    context->num_descriptors_started = 0;
    context->num_in_use_descriptors = 0;
    context->num_pending_completed_descriptors = 0;
    context->previous_num_completed_descriptors = 0;
    context->next_started_descriptor_index = 0;
    context->next_completed_descriptor_index = 0;
    context->num_descriptors_per_transfer =
            calloc (context->configuration.num_descriptors, sizeof (*context->num_descriptors_per_transfer));

    /* Timeout can be changed for each transfer started */
    context->timeout_enabled = false;
    context->abs_timeout = 0;

    /* Use the minimum size alignment specified in the arguments */
    if (context->configuration.min_size_alignment > context->addr_alignment)
    {
        context->addr_alignment = context->configuration.min_size_alignment;
    }

    /* Validate the configuration, after the alignment has been determined */
    x2x_validate_transfer_configuration (context);
    if (context->failed)
    {
        return;
    }

    /* Check the channel is idle. Should be idle since:
     * a. Opening a VFIO device asserts a reset.
     * b. The DMA engine is stopped by x2x_finalise_transfer_context() before this function is called to re-initialise a DMA channel */
    const uint32_t channel_status = read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET);
    if ((channel_status & X2X_CHANNEL_STATUS_BUSY) != 0)
    {
        x2x_record_failure (context, "Error: Attempting to initialise when DMA channel busy");
    }

    /* When the channel is idle, there should be zero available credits */
    const uint32_t available_credits = read_reg32 (context->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET);
    if (available_credits != 0)
    {
        x2x_record_failure (context, "Error: Attempting to initialise DMA channel when %" PRIu32 " available credits", available_credits);
    }

    /* Allocate the descriptor writeback array to record the length for each received transfer */
    if (context->is_axi_stream && (context->configuration.channels_submodule == DMA_SUBMODULE_C2H_CHANNELS))
    {
        vfio_dma_mapping_align_space (context->configuration.descriptors_mapping);
        context->stream_writeback =
                vfio_dma_mapping_allocate_space (context->configuration.descriptors_mapping,
                        context->configuration.num_descriptors * sizeof (*context->stream_writeback),
                        &first_stream_writeback_iova);
        X2X_ASSERT (context, context->stream_writeback != NULL);
    }
    else
    {
        context->stream_writeback = NULL;
    }

    /* Initialise the ring of descriptors, excluding the length and memory addresses for each descriptor, which are set before use.
     * DMA_DESCRIPTOR_CONTROL_COMPLETED is used to allow pollmode writeback to detect completion of the descriptor. */
    vfio_dma_mapping_align_space (context->configuration.descriptors_mapping);
    context->descriptors =
            vfio_dma_mapping_allocate_space (context->configuration.descriptors_mapping,
                    context->configuration.num_descriptors * sizeof (context->descriptors[0]),
                    &first_descriptor_iova);
    X2X_ASSERT (context, context->descriptors != NULL);
    if (context->failed)
    {
        return;
    }

    for (uint32_t descriptor_index = 0; descriptor_index < context->configuration.num_descriptors; descriptor_index++)
    {
        dma_descriptor_t *const descriptor = &context->descriptors[descriptor_index];
        const uint32_t next_descriptor_index = (descriptor_index + 1) % context->configuration.num_descriptors;
        const uint64_t next_descriptor_iova = first_descriptor_iova + (next_descriptor_index * sizeof (*descriptor));

        /* Calculate the fixed buffer addresses, or zero if not used */
        const uint64_t buffer_offset = (descriptor_index * context->configuration.bytes_per_buffer);
        const uint64_t host_buffer_address = (context->configuration.bytes_per_buffer > 0) ?
                context->configuration.data_mapping->iova + context->configuration.host_buffer_start_offset + buffer_offset : 0;
        const uint64_t card_buffer_address =
                ((context->configuration.bytes_per_buffer > 0) && (context->configuration.dma_bridge_memory_size_bytes > 0)) ?
                context->configuration.card_buffer_start_offset + buffer_offset : 0;

        /* DMA_DESCRIPTOR_CONTROL_COMPLETED is used to allow pollmode writeback to detect completion of the descriptor.
         * Nxt_adj is set to zero:
         * a. To prevent pre-fetching of descriptors which not yet been populated.
         * b. Since wouldn't work around the end of the ring.
         * c. Not sure how Nxt_adj interacts with descriptor credits.
         *
         * Nxt_adj might be a usable optimisation when operating with a AXI stream and using fixed size buffers. */
        descriptor->magic_nxt_adj_control = DMA_DESCRIPTOR_MAGIC | DMA_DESCRIPTOR_CONTROL_COMPLETED;

        /* When using fixed buffers on a H2C stream set the end of packet bit for each descriptor,
         * as each buffer contains a single packet (message). */
        if (context->is_axi_stream &&
            (context->configuration.bytes_per_buffer > 0) && (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS))
        {
            descriptor->magic_nxt_adj_control |= DMA_DESCRIPTOR_CONTROL_EOP;
        }

        /* Set the length to that in the configuration, which may be changed before use */
        descriptor->len = (uint32_t) context->configuration.bytes_per_buffer;

        /* Set source address for the descriptor, dependent upon the channel configuration */
        if (context->stream_writeback != NULL)
        {
            /* For a C2H stream set the address for where the writeback for this stream is stored */
            context->stream_writeback[descriptor_index].wb_magic_status = 0;
            context->stream_writeback[descriptor_index].length = 0;
            descriptor->src_adr = first_stream_writeback_iova + (descriptor_index * sizeof (context->stream_writeback[0]));
            descriptor->dst_adr = host_buffer_address;
        }
        else if (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
        {
            /* H2C transfer */
            descriptor->src_adr = host_buffer_address;
            descriptor->dst_adr = card_buffer_address;
        }
        else
        {
            /* C2H transfer */
            descriptor->src_adr = card_buffer_address;
            descriptor->dst_adr = host_buffer_address;
        }

        /* The descriptors are linked in a ring */
        descriptor->nxt_adr = next_descriptor_iova;

        /* For c2h_stream_continuous initialise to 1 descriptor per transfer to allow x2x_poll_completed_transfer()
         * to work, as the software doesn't start the transfers. */
        context->num_descriptors_per_transfer[descriptor_index] = context->configuration.c2h_stream_continuous ? 1 : 0;
    }

    /* Initialise the write back to monitor completed descriptors */
    vfio_dma_mapping_align_space (context->configuration.descriptors_mapping);
    context->completed_descriptor_count = vfio_dma_mapping_allocate_space (context->configuration.descriptors_mapping,
            sizeof (*context->completed_descriptor_count), &completed_descriptor_count_iova);
    X2X_ASSERT (context, context->completed_descriptor_count != NULL);
    if (context->failed)
    {
        return;
    }
    write_split_reg64 (context->x2x_channel_regs, X2X_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET, completed_descriptor_count_iova);
    context->completed_descriptor_count->sts_err_compl_descriptor_count = 0;

    /* Set channel control to enable pollmode write back and logging of all errors */
    uint32_t all_errors =
            X2C_CHANNEL_CONTROL_IE_DESC_ERROR |
            X2X_CHANNEL_CONTROL_IE_READ_ERROR |
            X2X_CHANNEL_CONTROL_IE_INVALID_LENGTH |
            X2X_CHANNEL_CONTROL_IE_MAGIC_STOPPED |
            X2X_CHANNEL_CONTROL_IE_ALIGN_MISMATCH;
    if (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
    {
        all_errors |= H2C_CHANNEL_CONTROL_IE_WRITE_ERROR;
    }
    write_reg32 (context->x2x_channel_regs, X2X_CHANNEL_CONTROL_RW_OFFSET,
            X2X_CHANNEL_CONTROL_POLLMODE_WB_ENABLE | all_errors);

    /* For the first descriptor in the ring set it's address in the DMA control registers.
     * Number of extra descriptors is set to zero as are no trying to optimise the descriptor fetching. */
    write_split_reg64 (context->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_ADDRESS_OFFSET, first_descriptor_iova);
    write_reg32 (context->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_ADJACENT_OFFSET, 0);

    /* Clear descriptor halt flag for the channel */
    const uint32_t halt_low_bit = (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            SGDMA_DESCRIPTOR_H2C_DSC_HALT_LOW_BIT : SGDMA_DESCRIPTOR_C2H_DSC_HALT_LOW_BIT;
    write_reg32 (context->sgdma_common_regs, SGDMA_DESCRIPTOR_CONTROL_W1C_OFFSET,
            1U << (halt_low_bit + context->configuration.channel_id));

    /* Enable credits for all modes except c2h_stream_continuous */
    const uint32_t credit_enable_low_bit = (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT : SGDMA_DESCRIPTOR_C2H_DSC_CREDIT_ENABLE_LOW_BIT;
    write_reg32 (context->sgdma_common_regs,
            context->configuration.c2h_stream_continuous ?
                    SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1C_OFFSET : SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET,
            1U << (credit_enable_low_bit + context->configuration.channel_id));

    /* Set the channel running:
     * a. For c2h_stream_continuous can start to process descriptors, as soon as data is available on the stream.
     * b. For other modes there are no available credits so no actual DMA transfers yet. */
    write_reg32 (context->x2x_channel_regs, X2X_CHANNEL_CONTROL_W1S_OFFSET, X2X_CHANNEL_CONTROL_RUN);
}


/**
 * @brief Finalise a context for performing DMA, which stops the DMA engine, and frees some resources.
 * @details Doesn't free resources allocated with VFIO, since the VFIO mappings may be shared by more than one context.
 * @param[out] context The context to finalise.
 */
void x2x_finalise_transfer_context (x2x_transfer_context_t *const context)
{
    /* Clear the Run bit to stop the DMA engine */
    write_reg32 (context->x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);

    /* Wait until the channel to be become idle, with a timeout.
     * Description for the Run bit contains:
     *   "Reset to 0 to stop transfer; if the engine is busy it completes the current descriptor." */
    const int64_t abs_timeout = get_monotonic_time () + 1000000000;
    bool timed_out = false;
    bool is_idle = false;
    uint32_t channel_status;
    do
    {
        channel_status = read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET);
        if ((channel_status & X2X_CHANNEL_STATUS_BUSY) == 0)
        {
            is_idle = true;
        }
        else
        {
            if (get_monotonic_time () > abs_timeout)
            {
                /* Only need to flag this timeout specifically if a previous failure has already been recorded */
                context->timeout_awaiting_idle_at_finalisation = context->failed;

                timed_out = true;
                x2x_record_failure (context, "Timeout waiting to become idle after clearing Run bit");
            }
        }
    } while (!timed_out && !is_idle);

    /* Release allocations in the context which are host memory only. I.e. not mapped with VFIO */
    free (context->num_descriptors_per_transfer);
    context->num_descriptors_per_transfer = NULL;
}


/**
 * @brief Poll for descriptors completing.
 * @details This is also the point at which check for errors with the transfer due to either:
 *          a. An error reported by the DMA bridge in the descriptor count write back.
 *          b. A timeout, when there are descriptors started but not yet completed.
 * @param[in/out] context The context to poll for descriptor completion.
 */
static void x2x_poll_for_descriptor_completion (x2x_transfer_context_t *const context)
{
    const char *detected_failure = NULL;

    const uint32_t sts_err_compl_descriptor_count =
            __atomic_load_n (&context->completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);
    const uint32_t num_completed_descriptors = sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;
    const uint32_t num_new_completions =
            (num_completed_descriptors - context->previous_num_completed_descriptors) & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;

    context->previous_num_completed_descriptors = num_completed_descriptors;
    context->num_pending_completed_descriptors += num_new_completions;

    if ((sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_STS_ERR) != 0)
    {
        /* The DMA bridge has reported an error for the channel */
        detected_failure = "Error reported in descriptor write back";
    }
    else if (context->timeout_enabled && (num_completed_descriptors != context->num_descriptors_started))
    {
        /* When a timeout has been enabled, and there are some in use descriptors check for a timeout */
        const int64_t now = get_monotonic_time ();

        if (now > context->abs_timeout)
        {
            detected_failure = "Timeout";
        }
    }

    /* Record when a failure has been detected, along with diagnostic information */
    if (detected_failure != NULL)
    {
        const uint32_t channel_status = read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET);

        x2x_record_failure (context, "%s: channel_status=0x%" PRIx32
                " num_descriptors_started=%" PRIu32 " num_completed_descriptors=%" PRIu32
                " next_started_descriptor_index=%" PRIu32 " next_completed_descriptor_index=%" PRIu32
                " channel_id=%" PRIu32
                " direction=%s"
                " device=%s",
                detected_failure, channel_status,
                context->num_descriptors_started, num_completed_descriptors,
                context->next_started_descriptor_index, context->next_completed_descriptor_index,
                context->configuration.channel_id,
                context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS ? "H2C" : "C2H",
                context->configuration.vfio_device->device_name);
    }
}


/**
 * @brief Get the number of free descriptors on a transfer context
 * @param[in/out] context The context to get the number of free descriptors for
 *                        Out as may record an error.
 * @return The current number of free descriptors
 */
uint32_t x2x_get_num_free_descriptors (x2x_transfer_context_t *const context)
{
    x2x_poll_for_descriptor_completion (context);

    const uint32_t num_free_descriptors = context->configuration.num_descriptors - context->num_in_use_descriptors;

    return num_free_descriptors;
}


/**
 * @brief Start the DMA transfers for descriptors which have been populated.
 * @param[in/out] context The context to start the descriptors for
 */
void x2x_start_populated_descriptors (x2x_transfer_context_t *const context)
{
    const uint32_t num_descriptors_in_transfer = context->num_descriptors_per_transfer[context->next_started_descriptor_index];
    X2X_ASSERT (context, num_descriptors_in_transfer > 0);

    context->next_started_descriptor_index = (context->next_started_descriptor_index + num_descriptors_in_transfer) %
            context->configuration.num_descriptors;
    context->num_descriptors_started += num_descriptors_in_transfer;
    context->num_descriptors_started &= COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK;
    write_reg32 (context->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET, num_descriptors_in_transfer);

    /* Start a timeout if configured */
    context->timeout_enabled = context->configuration.timeout_seconds >= 0;
    if (context->timeout_enabled)
    {
        const int64_t nsecs_per_sec = 1000000000;

        context->abs_timeout = get_monotonic_time() + (context->configuration.timeout_seconds * nsecs_per_sec);
    }
}


/**
 * @brief When fixed size buffers are being used, get the next H2C buffer to populate with data
 * @param[in/out] context The context to get the buffer for
 * @return The pointer to the host data for the buffer, or NULL if all buffers are currently in use for transfers.
 */
void *x2x_get_next_h2c_buffer (x2x_transfer_context_t *const context)
{
    X2X_ASSERT (context, (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) &&
            (context->configuration.bytes_per_buffer > 0));

    const uint32_t num_free_descriptors = x2x_get_num_free_descriptors (context);
    void *next_buffer = NULL;

    if (num_free_descriptors > 0)
    {
        uint32_t *const num_descriptors_in_transfer = &context->num_descriptors_per_transfer[context->next_started_descriptor_index];

        /* Check any descriptors set from a previous call have been started */
        X2X_ASSERT (context, *num_descriptors_in_transfer == 0);

        if (!context->failed)
        {
            const uint64_t buffer_start_offset = context->configuration.host_buffer_start_offset +
                    (context->next_started_descriptor_index * context->configuration.bytes_per_buffer);
            uint8_t *const buffer_data = context->configuration.data_mapping->buffer.vaddr;

            *num_descriptors_in_transfer = 1;
            context->num_in_use_descriptors += *num_descriptors_in_transfer;
            next_buffer = &buffer_data[buffer_start_offset];
        }
    }

    return next_buffer;
}


/**
 * @brief When fixed size buffers are being used, start the DMA transfer for the next C2H buffer.
 * @details No effect if no free descriptor
 * @param[in/out] context The context to start the next buffer for
 */
void x2x_start_next_c2h_buffer (x2x_transfer_context_t *const context)
{
    X2X_ASSERT (context, (context->configuration.channels_submodule == DMA_SUBMODULE_C2H_CHANNELS) &&
            (context->configuration.bytes_per_buffer > 0));

    const uint32_t num_free_descriptors = x2x_get_num_free_descriptors (context);

    if (num_free_descriptors > 0)
    {
        uint32_t *const num_descriptors_in_transfer = &context->num_descriptors_per_transfer[context->next_started_descriptor_index];

        /* Check any descriptors set from a previous call have been started */
        X2X_ASSERT (context, *num_descriptors_in_transfer == 0);

        if (!context->failed)
        {
            *num_descriptors_in_transfer = 1;
            context->num_in_use_descriptors += *num_descriptors_in_transfer;
            x2x_start_populated_descriptors (context);
        }
    }
}


/**
 * @brief Populate a memory mapped transfer, by setting one or more descriptors to cover the length of the transfer
 * @details To actually start the transfer, x2x_start_populated_descriptors() needs to be called.
 *
 *          This function checks if there are enough free descriptors for the transfer, but doesn't check if the
 *          host or card addresses are covered by any existing outstanding transfers. It is the responsibility
 *          of the caller to avoid any overlapping transfers to the same range of addresses.
 * @param[in/out] context The context to populate the transfer for.
 * @param[in] len The required transfer length in bytes
 * @param[in] host_buffer_offset The start offset of the transfer in the host buffer
 * @param[in] card_buffer_offset The start offset of the transfer in the card memory
 * @return Returns either:
 *         - A pointer to the start of the transfer in host memory if were sufficient free descriptors to populate the transfer.
 *         - NULL if not currently sufficient free descriptors.
 */
void *x2x_populate_memory_transfer (x2x_transfer_context_t *const context, const size_t len,
                                    const uint64_t host_buffer_offset, const uint64_t card_buffer_offset)
{
    const uint32_t num_descriptors_required = x2x_num_descriptors_for_transfer_len (len);

            /* Only valid to be called for memory mapped channels, since sets the card addresses */
    X2X_ASSERT (context, !context->is_axi_stream &&
            /* Since this function modifies the descriptors it is only valid to be called when fixed size buffers aren't used,
             * since also calling the API functions which operate on fixed size buffers assume the descriptors aren't modified */
            (context->configuration.bytes_per_buffer == 0) &&
            /* Validate that the number of descriptors required for the transfer doesn't exceed the number configured,
             * since otherwise this function could never set a transfer. */
            (num_descriptors_required <= context->configuration.num_descriptors) &&
            /* Validate that not attempting to access off the end of the host buffer */
            ((host_buffer_offset + len) <= context->configuration.data_mapping->buffer.size) &&
            /* Validate that not attempting to access off the end of the card memory */
            ((card_buffer_offset + len) <= context->configuration.dma_bridge_memory_size_bytes));

    const uint32_t num_free_descriptors = x2x_get_num_free_descriptors (context);
    void *host_buffer = NULL;

    if (num_free_descriptors >= num_descriptors_required)
    {
        uint32_t *const num_descriptors_in_transfer = &context->num_descriptors_per_transfer[context->next_started_descriptor_index];

        /* Check any descriptors set from a previous call have been started */
        X2X_ASSERT (context, *num_descriptors_in_transfer == 0);

        if (!context->failed)
        {
            uint8_t *const buffer_data = context->configuration.data_mapping->buffer.vaddr;
            size_t bytes_added_to_descriptors = 0;

            /* Update one or more descriptors for the transfer with the addresses and length,
             * allowing for the transfer length to exceed the maximum length of a single descriptor. */
            for (uint32_t descriptor_offset = 0; descriptor_offset < num_descriptors_required; descriptor_offset++)
            {
                const size_t remaining_len = len - bytes_added_to_descriptors;
                const size_t this_descriptor_len = (remaining_len < X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN) ?
                        remaining_len : X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN;
                const uint32_t descriptor_index = (context->next_started_descriptor_index + descriptor_offset) %
                        context->configuration.num_descriptors;
                dma_descriptor_t *const descriptor = &context->descriptors[descriptor_index];
                const uint64_t host_buffer_address =
                        context->configuration.data_mapping->iova + host_buffer_offset + bytes_added_to_descriptors;
                const uint64_t card_buffer_address = card_buffer_offset + bytes_added_to_descriptors;

                descriptor->len = (uint32_t) this_descriptor_len;
                if (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
                {
                    /* H2C transfer */
                    descriptor->src_adr = host_buffer_address;
                    descriptor->dst_adr = card_buffer_address;
                }
                else
                {
                    /* C2H transfer */
                    descriptor->src_adr = card_buffer_address;
                    descriptor->dst_adr = host_buffer_address;
                }
                bytes_added_to_descriptors += this_descriptor_len;
            }

            host_buffer = &buffer_data[host_buffer_offset];
            *num_descriptors_in_transfer = num_descriptors_required;
            context->num_in_use_descriptors += num_descriptors_required;
        }
    }

    return host_buffer;
}


/**
 * @brief Populate a AXI4 stream transfer, by setting one or more descriptors to cover the length of the transfer
 * @details To actually start the transfer, x2x_start_populated_descriptors() needs to be called.
 *
 *          This function checks if there are enough free descriptors for the transfer, but doesn't check if the
 *          host or card addresses are covered by any existing outstanding transfers. It is the responsibility
 *          of the caller to avoid any overlapping transfers to the same range of host buffer addresses.
 * @param[in/out] context The context to populate the transfer for.
 * @param[in] len The required transfer length in bytes
 * @param[in] host_buffer_offset The start offset of the transfer in the host buffer
 * @return Returns either:
 *         - A pointer to the start of the transfer in host memory if were sufficient free descriptors to populate the transfer.
 *         - NULL if not currently sufficient free descriptors.
 */
void *x2x_populate_stream_transfer (x2x_transfer_context_t *const context, const size_t len,
                                    const uint64_t host_buffer_offset)
{
    const uint32_t num_descriptors_required = x2x_num_descriptors_for_transfer_len (len);

            /* Only valid to be called for AXI stream mapped channels*/
    X2X_ASSERT (context, context->is_axi_stream &&
            /* Since this function modifies the descriptors it is only valid to be called when fixed size buffers aren't used,
             * since also calling the API functions which operate on fixed size buffers assume the descriptors aren't modified */
            (context->configuration.bytes_per_buffer == 0) &&
            /* Validate that the number of descriptors required for the transfer doesn't exceed the number configured,
             * since otherwise this function could never set a transfer. */
            (num_descriptors_required <= context->configuration.num_descriptors) &&
            /* Validate that not attempting to access off the end of the host buffer */
            ((host_buffer_offset + len) <= context->configuration.data_mapping->buffer.size));

    if (context->configuration.channels_submodule == DMA_SUBMODULE_C2H_CHANNELS)
    {
        /* For a C2H stream the length must fit a single descriptor. Otherwise if the data for the transfer is split into
         * multiple packets the data wouldn't be consecutive in host memory. */
        X2X_ASSERT (context, num_descriptors_required == 1);
    }

    const uint32_t num_free_descriptors = x2x_get_num_free_descriptors (context);
    void *host_buffer = NULL;

    if (num_free_descriptors >= num_descriptors_required)
    {
        uint32_t *const num_descriptors_in_transfer = &context->num_descriptors_per_transfer[context->next_started_descriptor_index];

        /* Check any descriptors set from a previous call have been started */
        X2X_ASSERT (context, *num_descriptors_in_transfer == 0);

        if (!context->failed)
        {
            uint8_t *const buffer_data = context->configuration.data_mapping->buffer.vaddr;
            size_t bytes_added_to_descriptors = 0;

            /* Update one or more descriptors for the transfer with the host address and length,
             * allowing for the transfer length to exceed the maximum length of a single descriptor. */
            for (uint32_t descriptor_offset = 0; descriptor_offset < num_descriptors_required; descriptor_offset++)
            {
                const size_t remaining_len = len - bytes_added_to_descriptors;
                const bool is_final_descriptor = remaining_len <= X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN;
                const size_t this_descriptor_len = is_final_descriptor ?
                        remaining_len : X2X_CACHE_LINE_ALIGNED_MAX_DESCRIPTOR_LEN;
                const uint32_t descriptor_index = (context->next_started_descriptor_index + descriptor_offset) %
                        context->configuration.num_descriptors;
                dma_descriptor_t *const descriptor = &context->descriptors[descriptor_index];
                const uint64_t host_buffer_address =
                        context->configuration.data_mapping->iova + host_buffer_offset + bytes_added_to_descriptors;

                descriptor->len = (uint32_t) this_descriptor_len;
                if (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
                {
                    /* H2C transfer */
                    descriptor->src_adr = host_buffer_address;
                }
                else
                {
                    /* C2H transfer */
                    descriptor->dst_adr = host_buffer_address;
                }
                if (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
                {
                    /* For a H2C stream set End of Packet only on the final descriptor used for a transfer */
                    if (is_final_descriptor)
                    {
                        descriptor->magic_nxt_adj_control |= DMA_DESCRIPTOR_CONTROL_EOP;
                    }
                    else
                    {
                        descriptor->magic_nxt_adj_control &= ~DMA_DESCRIPTOR_CONTROL_EOP;
                    }
                }
                bytes_added_to_descriptors += this_descriptor_len;
            }

            host_buffer = &buffer_data[host_buffer_offset];
            *num_descriptors_in_transfer = num_descriptors_required;
            context->num_in_use_descriptors += num_descriptors_required;
        }
    }

    return host_buffer;
}


/**
 * @brief Poll for the next completed transfer
 * @details For a C2H transfer this needs to be called to know when the data in the completed transfer is available in the
 *          host memory.
 *          For a H2C transfer this needs to be called to determine when the transfer has completed, so that the descriptors
 *          and host memory can be re-used for a further transfer.
 * @param[in/out] context The context to poll for the next completed transfer on
 * @param[out] transfer_len If non-NULL then set to the number of data bytes in the completed transfer.
 *                          For a C2H AXI stream this is needed to get the actual number of bytes, which may be less
 *                          than the buffer size.
 *                          For other transfers types is optional, as returns the same length as when the transfer was started.
 * @param[out] end_of_packet For a C2H AXI stream set to true when the completed transfer was terminated by end of packet
 * @return Indicates if a transfer has completed:
 *         - Non NULL means a transfer has completed, and points at the host data for the completed transfer
 *         - NULL means there is no completed transfer.
 */
void *x2x_poll_completed_transfer (x2x_transfer_context_t *const context, size_t *const transfer_len, bool *const end_of_packet)
{
    void *completed_data = NULL;
    uint32_t *const num_descriptors_in_transfer = &context->num_descriptors_per_transfer[context->next_completed_descriptor_index];

    if (*num_descriptors_in_transfer > 0)
    {
        x2x_poll_for_descriptor_completion (context);

        if (!context->failed && (context->num_pending_completed_descriptors >= *num_descriptors_in_transfer))
        {
            /* Use host IOVA from the oldest completed descriptor to get to the start of the data in host memory */
            uint32_t num_descriptors_in_transfer = context->num_descriptors_per_transfer[context->next_completed_descriptor_index];
            const dma_descriptor_t *const descriptor = &context->descriptors[context->next_completed_descriptor_index];
            const uint64_t host_iova = (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
                    descriptor->src_adr : descriptor->dst_adr;
            const uint64_t buffer_offset = host_iova - context->configuration.data_mapping->iova;
            uint8_t *const buffer_data = context->configuration.data_mapping->buffer.vaddr;

            /* Return the transfer length and end of packet indication if requested */
            if (transfer_len != NULL)
            {
                if ((context->configuration.channels_submodule == DMA_SUBMODULE_C2H_CHANNELS) && context->is_axi_stream)
                {
                    /* For a CH2 AXI stream use the values from the stream write back */
                    c2h_stream_writeback_t *const stream_writeback = &context->stream_writeback[context->next_completed_descriptor_index];

                    if ((stream_writeback->wb_magic_status & C2H_STREAM_WB_MAGIC_MASK) != C2H_STREAM_WB_MAGIC)
                    {
                        x2x_record_failure (context, "Incorrect stream wb_magic_status 0x%" PRIx32, stream_writeback->wb_magic_status);
                    }

                    *transfer_len = stream_writeback->length;
                    *end_of_packet = (stream_writeback->wb_magic_status & CH2_STREAM_WB_EOP) != 0;
                }
                else
                {
                    /* Return the transfer length at that set in the descriptors, summing over one or more descriptors */
                    *transfer_len = 0;
                    for (uint32_t descriptor_offset = 0; descriptor_offset < num_descriptors_in_transfer; descriptor_offset++)
                    {
                        const uint32_t descriptor_index = (context->next_completed_descriptor_index + descriptor_offset) %
                                context->configuration.num_descriptors;

                        (*transfer_len) += context->descriptors[descriptor_index].len;
                    }
                }
            }

            if (!context->failed)
            {
                /* Return the pointer to data in the completed transfer, and indicate the descriptors are no longer in use */
                completed_data = &buffer_data[buffer_offset];
                context->num_pending_completed_descriptors -= num_descriptors_in_transfer;
                X2X_ASSERT (context, context->num_pending_completed_descriptors < context->configuration.num_descriptors);
                if (!context->configuration.c2h_stream_continuous)
                {
                    context->num_in_use_descriptors -= num_descriptors_in_transfer;
                    X2X_ASSERT (context, context->num_in_use_descriptors < context->configuration.num_descriptors);
                    context->num_descriptors_per_transfer[context->next_completed_descriptor_index] = 0;
                }
                context->next_completed_descriptor_index = (context->next_completed_descriptor_index + num_descriptors_in_transfer) %
                        context->configuration.num_descriptors;
            }
        }
    }

    return completed_data;
}
