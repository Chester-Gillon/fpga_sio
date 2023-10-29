/*
 * @file xilinx_dma_bridge_transfers.h
 * @date 4 Feb 2023
 * @author Chester Gillon
 * @brief Provides transfers between the Host and Card using the Xilinx "DMA/Bridge Subsystem for PCI Express"
 */

#ifndef XILINX_DMA_BRIDGE_TRANSFERS_H_
#define XILINX_DMA_BRIDGE_TRANSFERS_H_

#include <stdbool.h>

#include "vfio_access.h"
#include "xilinx_dma_bridge_host_interface.h"


/* Defines the context used to control DMA transfers for either one H2C or C2C DMA channel.
 *
 * The vfio_dma_mapping_t is placed in the context since:
 * a. Allows descriptors to be allocated per channel, which simplifies the code.
 * b. Allows the data DMA mapping to test different VFIO access. E.g..:
 *    - For C2H transfers only allow DMA write access to host memory
 *    - For H2C transfers only allow DMA read access to host memory
 */
typedef struct
{
    /* DMA_SUBMODULE_H2C_CHANNELS or DMA_SUBMODULE_C2H_CHANNELS to identify which direction of DMA transfers being used */
    uint32_t channels_submodule;
    /* Which channel this context is being used to transfer */
    uint32_t channel_id;
    /* Used for the data transfers */
    vfio_dma_mapping_t data_mapping;
    /* Mapped base of the H2C Channel or C2H Channel registers for the DMA transfers */
    uint8_t *x2x_channel_regs;
    /* Mapped base of the H2C SGDMA or C2H SGDMA registers for the DMA transfers */
    uint8_t *x2x_sgdma_regs;
    /* Mapped base of the SGDMA Common registers for the DMA transfers */
    uint8_t *sgdma_common_regs;
    /* The byte alignment that the source and destination addresses must align to. */
    uint32_t addr_alignment;
    /* The minimum granularity of DMA transfers in bytes */
    uint32_t len_granularity;
    /* The number of address bits configured in the DMA engine */
    uint32_t num_address_bits;
    /* The number of descriptors to create transfer data_mapping.size bytes, allowing for the DMA_DESCRIPTOR_MAX_LEN of one descriptor */
    uint32_t num_descriptors;
    /* Array of descriptors used to create the transfer */
    dma_descriptor_t *descriptors;
    /* Host memory where the completed descriptor count is written to, to poll for completion */
    completed_descriptor_count_writeback_t *completed_descriptor_count;
    /* When true a timeout is enabled waiting for the transfer to complete */
    bool timeout_enabled;
    /* The absolute CLOCK_MONOTONIC time at which the transfer is timed out */
    int64_t abs_timeout;
} x2x_transfer_context_t;


/* The status for polling for completion of a DMA transfer */
typedef enum
{
    /* The transfer is still in progress */
    X2X_TRANSFER_STATUS_IN_PROGRESS,
    /* The transfer has completed */
    X2X_TRANSFER_STATUS_COMPLETE,
    /* The transfer has timed out */
    X2X_TRANSFER_STATUS_TIMEOUT,
    /* An error was indicated in the Completed Descriptor Count Writeback */
    X2X_TRANSFER_STATUS_ERROR
} x2x_transfer_status_t;


bool initialise_x2x_transfer_context (x2x_transfer_context_t *const context,
                                      vfio_device_t *const vfio_device, const uint32_t bar_index,
                                      const uint32_t channels_submodule, const uint32_t channel_id,
                                      const uint32_t min_size_alignment,
                                      vfio_dma_mapping_t *const descriptors_mapping,
                                      const vfio_dma_mapping_t *const data_mapping);
void x2x_transfer_set_card_start_address (x2x_transfer_context_t *const context, const uint64_t card_start_address);
bool x2x_start_transfer (x2x_transfer_context_t *const context, const int64_t timeout_seconds);
x2x_transfer_status_t x2x_poll_transfer_completion (x2x_transfer_context_t *const context);

#endif /* XILINX_DMA_BRIDGE_TRANSFERS_H_ */
