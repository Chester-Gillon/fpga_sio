/*
 * @file xilinx_dma_bridge_transfers.c
 * @date 4 Feb 2023
 * @author Chester Gillon
 * @brief Provides transfers between the Host and Card using the Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details
 *   Uses VFIO to be able to perform the DMA entirely in user space, in polling mode.
 *
 *   Only implements support for Memory Mapped AXI4 endpoints, i.e. doesn't support AXI4 stream endpoints.
 *
 *   For a given H2C or C2H channel only supports one outstanding transfer at once. From PG195 can't see how
 *   can add a new descriptor to the linked-list for a channel while the DMA is running, while avoiding potential
 *   race conditions of the existing linked-list stopping at the same time as trying to append a new descriptor
 *   to the linked-list.
 *
 *   The version in the identifier register is not checked. This file has been written based upon PG195 (v4.1)
 */

#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"

#include <string.h>
#include <inttypes.h>
#include <stdio.h>


/**
 * @brief Check the identity register value for one DMA submodule
 * @details This is a sanity check that the DMA control registers have been mapped correctly
 * @param[in] submodule_regs Base of the memory mapped registers for the submodule
 * @param[in] expected_submodule The expected submodule
 * @param[in] expected_channel_id The expected channel identity, if expected_submodule is a per-channel submodule
 * @return Returns true if the identity register has the expected value, or false otherwise
 */
static bool check_dma_submodule_identity (const uint8_t *const submodule_regs, const uint32_t expected_submodule,
                                          const uint32_t expected_channel_id)
{
    const uint32_t identity_reg_value = read_reg32 (submodule_regs, SUBMODULE_IDENTIFIER_OFFSET);
    const uint32_t subsystem_ip =
            (identity_reg_value & SUBMODULE_IDENTIFIER_SUBSYSTEM_MASK) >> SUBMODULE_IDENTIFIER_SUBSYSTEM_SHIFT;
    const uint32_t actual_submodule = (identity_reg_value & SUBMODULE_IDENTIFIER_TARGET_MASK) >> SUBMODULE_IDENTIFIER_TARGET_SHIFT;
    const bool is_axi4_stream = (identity_reg_value & SUBMODULE_IDENTIFIER_STREAM_MASK) != 0;
    const uint32_t actual_channel_id =
            (identity_reg_value & SUBMODULE_IDENTIFIER_CHANNEL_ID_MASK) >> SUBMODULE_IDENTIFIER_CHANNEL_ID_SHIFT;

    if (subsystem_ip != SUBMODULE_IDENTIFIER_SUBSYSTEM_ID)
    {
        printf ("For expected_submodule %" PRIu32 " unexpected subsystem ID 0x%" PRIx32 "\n", expected_submodule, subsystem_ip);
        return false;
    }

    if (actual_submodule != expected_submodule)
    {
        printf ("expected_submodule %" PRIu32 ", but actual_submodule %" PRIu32 "\n", expected_submodule, actual_submodule);
        return false;
    }

    switch (expected_submodule)
    {
    case DMA_SUBMODULE_H2C_CHANNELS:
    case DMA_SUBMODULE_C2H_CHANNELS:
    case DMA_SUBMODULE_H2C_SGDMA:
    case DMA_SUBMODULE_C2H_SGDMA:
        /* Validate per-channel submodule */
        if (is_axi4_stream)
        {
            printf ("For submodule %" PRIu32 " endpoint is AXI4 stream; this driver only supports AXI4 memory mapped endpoints\n",
                    expected_submodule);
            return false;
        }

        if (actual_channel_id != expected_channel_id)
        {
            printf ("expected_submodule %" PRIu32 " actual_channel_id %" PRIu32 " expected_channel_id %" PRIu32 "\n",
                    expected_submodule, actual_channel_id, expected_channel_id);
            return false;
        }
        break;
    }

    return true;
}


/**
 * @brief Initialise the context for performing DMA transfers using one H2C or 2CH channel
 * @param[out] context The initialised context
 * @param[in/out] vfio_device Used to obtain access to the memory mapped BAR containing the DMA control registers
 * @param[in] bar_index Which BAR in the vfio_device contains the DMA control registers
 * @param[in] channels_submodule Either DMA_SUBMODULE_H2C_CHANNELS or DMA_SUBMODULE_C2H_CHANNELS
 *                               to identify which direction are initialising the context for
 * @param[in] channel_id The identity of the channel are initialising the context for
 * @param[in] min_size_alignment The minimum aligned size used for the DMA descriptors, for when multiple chained descriptors
 *                               are needed due to DMA_DESCRIPTOR_MAX_LEN.
 * @param[in/out] descriptors_mapping Used to allocate space for DMA descriptors
 * @param[in] data_mapping The DMA mapping used for data transfers
 * @return Returns true if the context has been initialised, or false if an error occurred.
 *         An error occurs identification register don't contain the expected content.
 */
bool initialise_x2x_transfer_context (x2x_transfer_context_t *const context,
                                      vfio_device_t *const vfio_device, const uint32_t bar_index,
                                      const uint32_t channels_submodule, const uint32_t channel_id,
                                      const uint32_t min_size_alignment,
                                      vfio_dma_mapping_t *const descriptors_mapping,
                                      const vfio_dma_mapping_t *const data_mapping)
{
    const uint32_t sgdma_channels_submodule = (channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            DMA_SUBMODULE_H2C_SGDMA : DMA_SUBMODULE_C2H_SGDMA;
    bool success;

    /* Check that have been passed a BAR which is large enough to contain the DMA control registers */
    map_vfio_device_bar_before_use (vfio_device, bar_index);
    if (vfio_device->regions_info[bar_index].size < 0x10000)
    {
        printf ("BAR[%" PRIu32 " size of 0x%llx too small for DMA/Bridge Subsystem for PCI Express\n",
                bar_index, vfio_device->regions_info[bar_index].size);
        return false;
    }

    /* Store the caller supplied information in the context */
    memset (context, 0, sizeof (*context));
    context->channels_submodule = channels_submodule;
    context->channel_id = channel_id;
    context->data_mapping = *data_mapping;

    /* Timeout can be changed for each transfer started */
    context->timeout_enabled = false;
    context->abs_timeout = 0;

    /* Set the mapped base of the DMA control registers used for the channel */
    uint8_t *const mapped_registers_base = vfio_device->mapped_bars[bar_index];
    context->x2x_channel_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (channels_submodule, channel_id)];
    context->x2x_sgdma_regs = &mapped_registers_base[DMA_CHANNEL_BAR_START_OFFSET (sgdma_channels_submodule, channel_id)];
    context->sgdma_common_regs = &mapped_registers_base[DMA_SUBMODULE_BAR_START_OFFSET (DMA_SUBMODULE_SGDMA_COMMON)];

    /* Verify the identity of the DMA submodules used for the channel */
    success = check_dma_submodule_identity (context->x2x_channel_regs, channels_submodule, channel_id) &&
            check_dma_submodule_identity (context->x2x_sgdma_regs, sgdma_channels_submodule, channel_id) &&
            check_dma_submodule_identity (context->sgdma_common_regs, DMA_SUBMODULE_SGDMA_COMMON, 0);
    if (!success)
    {
        return false;
    }

    /* Obtain the alignment requirements of the DMA engine */
    const uint32_t alignment_reg_value = read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_ALIGNMENTS_OFFSET);
    context->addr_alignment =
            (alignment_reg_value & X2X_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_MASK) >> X2X_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_SHIFT;
    context->len_granularity =
            (alignment_reg_value & X2X_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_MASK) >> X2X_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_SHIFT;
    context->num_address_bits =
            (alignment_reg_value & X2X_CHANNEL_ALIGNMENTS_ADDRESS_BITS_MASK) >> X2X_CHANNEL_ALIGNMENTS_ADDRESS_BITS_SHIFT;

    /* Use the minimum size alignment specified in the arguments */
    if (min_size_alignment > context->addr_alignment)
    {
        context->addr_alignment = min_size_alignment;
    }

    /* Calculate the number of descriptors needed for context->data_mapping.size */
    const uint32_t aligned_max_descriptor_len = (DMA_DESCRIPTOR_MAX_LEN / context->addr_alignment) * context->addr_alignment;
    context->num_descriptors =
            (uint32_t) ((context->data_mapping.buffer.size + (aligned_max_descriptor_len - 1)) / aligned_max_descriptor_len);

    /* Allocate space for the DMA descriptors and initialise them. card address starts at zero but may be changed
     * before the transfer is started. */
    dma_descriptor_t *descriptor = NULL;
    dma_descriptor_t *previous_descriptor = NULL;
    uint64_t descriptor_iova = 0;
    uint64_t data_iova = data_mapping->iova;
    uint64_t remaining_data_bytes = data_mapping->buffer.size;
    uint64_t card_address = 0;
    vfio_dma_mapping_align_space (descriptors_mapping);
    for (uint32_t descriptor_index = 0; descriptor_index < context->num_descriptors; descriptor_index++)
    {
        descriptor = vfio_dma_mapping_allocate_space (descriptors_mapping, sizeof (*descriptor), &descriptor_iova);
        if (descriptor == NULL)
        {
            return false;
        }
        if (descriptor_index == 0)
        {
            context->descriptors = descriptor;
            /* For the first descriptor set it's address in the DMA control registers.
             * Number of extra descriptors is set to zero as are no trying to optimise the descriptor fetching. */
            write_split_reg64 (context->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_ADDRESS_OFFSET, descriptor_iova);
            write_reg32 (context->x2x_sgdma_regs, X2X_SGDMA_DESCRIPTOR_ADJACENT_OFFSET, 0);
        }
        else
        {
            /* Chain the previous descriptor to this one */
            previous_descriptor->nxt_adr = descriptor_iova;
        }
        descriptor->magic_nxt_adj_control = DMA_DESCRIPTOR_MAGIC;
        descriptor->len =
                (uint32_t) ((remaining_data_bytes < aligned_max_descriptor_len) ? remaining_data_bytes : aligned_max_descriptor_len);
        if (context->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
        {
            descriptor->src_adr = data_iova;
            descriptor->dst_adr = card_address;
        }
        else
        {
            descriptor->src_adr = card_address;
            descriptor->dst_adr = data_iova;
        }
        descriptor->nxt_adr = 0;

        card_address += descriptor->len;
        data_iova += descriptor->len;
        remaining_data_bytes -= descriptor->len;
        previous_descriptor = descriptor;
    }

    /* Set Stop flag on final descriptor, and the completed flag to allow pollmode writeback */
    descriptor->magic_nxt_adj_control |= DMA_DESCRIPTOR_CONTROL_STOP | DMA_DESCRIPTOR_CONTROL_COMPLETED;

    /* Allocate completed descriptor count, and set it's address in the DMA control registers */
    vfio_dma_mapping_align_space (descriptors_mapping);
    context->completed_descriptor_count =
            vfio_dma_mapping_allocate_space (descriptors_mapping, sizeof (*context->completed_descriptor_count), &descriptor_iova);
    if (context->completed_descriptor_count == NULL)
    {
        return false;
    }
    write_split_reg64 (context->x2x_channel_regs, X2X_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET, descriptor_iova);
    context->completed_descriptor_count->sts_err_compl_descriptor_count = 0;

    /* Set channel control to enable pollmode write back and logging of all errors */
    uint32_t all_errors =
            X2C_CHANNEL_CONTROL_IE_DESC_ERROR |
            X2X_CHANNEL_CONTROL_IE_READ_ERROR |
            X2X_CHANNEL_CONTROL_IE_INVALID_LENGTH |
            X2X_CHANNEL_CONTROL_IE_MAGIC_STOPPED |
            X2X_CHANNEL_CONTROL_IE_ALIGN_MISMATCH;
    if (context->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
    {
        all_errors |= H2C_CHANNEL_CONTROL_IE_WRITE_ERROR;
    }
    write_reg32 (context->x2x_channel_regs, X2X_CHANNEL_CONTROL_RW_OFFSET,
            X2X_CHANNEL_CONTROL_POLLMODE_WB_ENABLE | all_errors);

    /* Disable the use of descriptor crediting, as just let the transfer run to completion */
    const uint32_t credit_enable_low_bit = (context->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT : SGDMA_DESCRIPTOR_C2H_DSC_CREDIT_ENABLE_LOW_BIT;
    write_reg32 (context->sgdma_common_regs, SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1C_OFFSET, 1U << (credit_enable_low_bit + channel_id));

    /* Clear descriptor halt flag for the channel */
    const uint32_t halt_low_bit = (context->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ?
            SGDMA_DESCRIPTOR_H2C_DSC_HALT_LOW_BIT : SGDMA_DESCRIPTOR_C2H_DSC_HALT_LOW_BIT;
    write_reg32 (context->sgdma_common_regs, SGDMA_DESCRIPTOR_CONTROL_W1C_OFFSET, 1U << (halt_low_bit + channel_id));

    return success;
}


/**
 * @brief Update the card start address in an initialised DMA transfer context, by updating the DMA descriptors
 * @param[in/out] context The transfer context to update
 * @param[in] card_start_address The card start address to use for the transfer.
 *                               This the AXI4 memory mapped address inside the card.
 */
void x2x_transfer_set_card_start_address (x2x_transfer_context_t *const context, const uint64_t card_start_address)
{
    uint64_t card_address = card_start_address;

    for (uint32_t descriptor_index = 0; descriptor_index < context->num_descriptors; descriptor_index++)
    {
        dma_descriptor_t *const descriptor = &context->descriptors[descriptor_index];

        if (context->channels_submodule == DMA_SUBMODULE_H2C_CHANNELS)
        {
            descriptor->dst_adr = card_address;
        }
        else
        {
            descriptor->src_adr = card_address;
        }
        card_address += descriptor->len;
    }
}


/**
 * @brief Start a DMA transfer for a previously initialised context
 * @param[in/out] context The context to start the DMA transfer for
 * @param[in] timeout_seconds Optional timeout for the DMA transfers. Negative value disables the timeout.
 * @return Returns true if the transfer was started, or false if an error
 */
bool x2x_start_transfer (x2x_transfer_context_t *const context, const int64_t timeout_seconds)
{
    const uint32_t channel_status = read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET);

    if ((channel_status & X2X_CHANNEL_STATUS_BUSY) != 0)
    {
        printf ("Error: Attempting to start transfer when DMA channel busy\n");
        return false;
    }

    /* Set a timeout if requested */
    context->timeout_enabled = timeout_seconds >= 0;
    if (context->timeout_enabled)
    {
        const int64_t nsecs_per_sec = 1000000000;

        context->abs_timeout = get_monotonic_time() + (timeout_seconds * nsecs_per_sec);
    }

    /* Clear any previous completed descriptor count */
    context->completed_descriptor_count->sts_err_compl_descriptor_count = 0;

    /* Start the transfer */
    write_reg32 (context->x2x_channel_regs, X2X_CHANNEL_CONTROL_W1S_OFFSET, X2X_CHANNEL_CONTROL_RUN);

    return true;
}


/**
 * @brief Poll for completion of a DMA transfer started by a previous call to x2x_start_transfer()
 * @param[in] context The context to poll for completion of
 * @return Returns the status of the transfer, possibly indicating an error
 */
x2x_transfer_status_t x2x_poll_transfer_completion (x2x_transfer_context_t *const context)
{
    const uint32_t sts_err_compl_descriptor_count =
            __atomic_load_n (&context->completed_descriptor_count->sts_err_compl_descriptor_count, __ATOMIC_ACQUIRE);

    if ((sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_STS_ERR) != 0)
    {
        /* @todo This Attempts to detect when the transfers stopped due to an error, but based upon putting some deliberate
         *       errors in the descriptors didn't cause an error to be indicated in the writeback data. */
        printf ("Transfer completed with error. channel_status=0x%" PRIx32 " with %" PRIu32 " out of %" PRIu32 " descriptors completed\n",
                read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET),
                (sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK), context->num_descriptors);
        return X2X_TRANSFER_STATUS_ERROR;
    }
    else if ((sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK) == context->num_descriptors)
    {
        /* Transfer completed. Need to clear the Run bit to stop the DMA engine to allow a further transfer to be started */
        write_reg32 (context->x2x_channel_regs, X2X_CHANNEL_CONTROL_W1C_OFFSET, X2X_CHANNEL_CONTROL_RUN);
        return X2X_TRANSFER_STATUS_COMPLETE;
    }
    else if (context->timeout_enabled)
    {
        /* Check for a timeout */
        const int64_t now = get_monotonic_time ();

        if (now > context->abs_timeout)
        {
            printf ("Transfer timed out. channel_status=0x%" PRIx32 " with %" PRIu32 " out of %" PRIu32 " descriptors completed\n",
                    read_reg32 (context->x2x_channel_regs, X2X_CHANNEL_STATUS_RW1C_OFFSET),
                    (sts_err_compl_descriptor_count & COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK), context->num_descriptors);
            return X2X_TRANSFER_STATUS_TIMEOUT;
        }
        else
        {
            return X2X_TRANSFER_STATUS_IN_PROGRESS;
        }
    }
    else
    {
        /* Transfer still in progress */
        return X2X_TRANSFER_STATUS_IN_PROGRESS;
    }
}
