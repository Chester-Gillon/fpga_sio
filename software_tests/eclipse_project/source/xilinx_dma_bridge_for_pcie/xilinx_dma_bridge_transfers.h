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


/* Defines the configuration used for control DMA transfers for either one H2C or C2C DMA channel.
 * This is provided by the caller of the API, and read-only as transfers are performed. */
typedef struct
{
    /* The amount of memory addressed by the DMA/Bridge Subsystem, which also indicates the assumed DMA interface option:
     * a. A non-zero value means "AXI Memory Mapped".
     * b. A zero values means "AXI Stream". */
    size_t dma_bridge_memory_size_bytes;
    /* The minimum aligned size used for the DMA descriptors, for when multiple chained descriptors are needed due to
     * DMA_DESCRIPTOR_MAX_LEN. */
    uint32_t min_size_alignment;
    /* The number of descriptors to create in a ring, allowing for descriptors to be populated and queued for transfers. */
    uint32_t num_descriptors;
    /* DMA_SUBMODULE_H2C_CHANNELS or DMA_SUBMODULE_C2H_CHANNELS to identify which direction of DMA transfers being used */
    uint32_t channels_submodule;
    /* Which channel is to being used for the transfers */
    uint32_t channel_id;
    /* When non-zero, during initialisation the descriptors are set to a separate buffer for each descriptor,
     * creating an array of buffers. */
    size_t bytes_per_buffer;
    /* When bytes_per_buffer is non zero, gives the starting host offset in data_mapping for the first buffer */
    uint64_t host_buffer_start_offset;
    /* When bytes_per_buffer and dma_bridge_memory_size_bytes is non-zero, gives the starting card offset
     * for the first buffer. */
    uint64_t card_buffer_start_offset;
    /* Controls how descriptors are queued for a C2H channel for an AXI stream, when bytes_per_buffer is non-zero:
     * - When false credits must be issued to perform DMA.
     * - When true the DMA runs continuously without needing to add credits. This means the application must keep up with
     *   the received data, otherwise data in the host memory may be overwritten before being processed. */
    bool c2h_stream_continuous;
    /* Optional timeout for the DMA transfers. Negative value disables the timeout. */
    int64_t timeout_seconds;
    /* Used to obtain access to the memory mapped BAR containing the DMA control registers */
    vfio_device_t *vfio_device;
    /* Which BAR in the vfio_device contains the DMA control registers */
    uint32_t bar_index;
    /* Used to allocate space for DMA descriptors. May be used by multiple channels. */
    vfio_dma_mapping_t *descriptors_mapping;
    /* The data mapping for the host memory used by the transfer. Used to obtain the host virtual address and
     * DMA IOVA at different offsets within the mapping. */
    const vfio_dma_mapping_t *data_mapping;
    /* Points an an overall test success status which is set false when failed is set true.
     * This allows a test to monitor a single boolean to track the overall success over multiple transfers. */
    bool *overall_success;
} x2x_transfer_configuration_t;


/* Defines the context used to control DMA transfers for either one H2C or C2C DMA channel.
 *
 * The vfio_dma_mapping_t is placed in the context since:
 * a. Allows descriptors to be allocated per channel, which simplifies the code.
 * b. Allows the data DMA mapping to test different VFIO access. E.g.:
 *    - For C2H transfers only allow DMA write access to host memory
 *    - For H2C transfers only allow DMA read access to host memory
 */
typedef struct
{
    /* The configuration for the channel */
    x2x_transfer_configuration_t configuration;
    /* Set true when the DMA transfers have failed, after detecting an error. Once set no more transfers are started */
    bool failed;
    /* Set true if x2x_finalise_transfer_context() encounters a timeout waiting for the channel to be become idle.
     * Added as an additional error flag, since if a DMA channel suffers a timeout during a transfer, then clearing
     * the Run bit may leave the channel busy. PG195 suggests when the Run bit is clear, the DMA channel waits to
     * complete the transfer which if a transfer has hung then likely won't complete. */
    bool timeout_awaiting_idle_at_finalisation;
    /* Describes the error which caused failed to be set */
    char error_message[512];
    /* The DMA interface option, which changes some of the register and descriptor settings:
     * - false means "AXI Memory Mapped".
     * - true means "AXI Stream". */
    bool is_axi_stream;
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
    /* The ring of descriptors */
    dma_descriptor_t *descriptors;
    /* For the C2H of a Stream Interface for each descriptor used to write back the length information */
    c2h_stream_writeback_t *stream_writeback;
    /* Host memory where the completed descriptor count is written to, to poll for completion */
    completed_descriptor_count_writeback_t *completed_descriptor_count;
    /* Array for each descriptor which records how many adjacent descriptors were started for a single transfer.
     * Used when checking for completed transfers. */
    uint32_t *num_descriptors_per_transfer;
    /* The running count of how many descriptors which have been started for transfers.
     * This wraps at COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK so can be compared against the descriptor count write back */
    uint32_t num_descriptors_started;
    /* The number of descriptors which are currently in use */
    uint32_t num_in_use_descriptors;
    /* The number of descriptors which have completed, and are pending notification of completion to the caller.
     * These are still consider in-use as far as starting new transfers is concerned. */
    uint32_t num_pending_completed_descriptors;
    /* The previous completed descriptor count from the DMA engine, used to detect when descriptors have completed */
    uint32_t previous_num_completed_descriptors;
    /* The index of the descriptor in the ring which is to be started next */
    uint32_t next_started_descriptor_index;
    /* The index of the descriptor in the ring which is to be checked for completion next */
    uint32_t next_completed_descriptor_index;
    /* When true a timeout is enabled waiting for the transfer to complete */
    bool timeout_enabled;
    /* The absolute CLOCK_MONOTONIC time at which the transfer is timed out */
    int64_t abs_timeout;
} x2x_transfer_context_t;


void x2x_record_failure (x2x_transfer_context_t *const context, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
void x2x_assert (x2x_transfer_context_t *const context, const bool assertion, const char *const assertion_message);
#define X2X_ASSERT(context,assertion) x2x_assert (context, assertion, #assertion)
size_t x2x_get_descriptor_allocation_size (const x2x_transfer_configuration_t *const configuration);
void x2x_get_num_channels (vfio_device_t *const vfio_device, const uint32_t bar_index, const size_t dma_bridge_memory_size_bytes,
                           uint32_t *const num_h2c_channels, uint32_t *const num_c2h_channels);
void x2x_initialise_transfer_context (x2x_transfer_context_t *const context,
                                      const x2x_transfer_configuration_t *const configuration);
void x2x_finalise_transfer_context (x2x_transfer_context_t *const context);
uint32_t x2x_get_num_free_descriptors (x2x_transfer_context_t *const context);
void x2x_start_populated_descriptors (x2x_transfer_context_t *const context);
void *x2x_get_next_h2c_buffer (x2x_transfer_context_t *const context);
void x2x_start_next_c2h_buffer (x2x_transfer_context_t *const context);
void *x2x_poll_completed_transfer (x2x_transfer_context_t *const context, size_t *const transfer_len, bool *const end_of_packet);

#endif /* XILINX_DMA_BRIDGE_TRANSFERS_H_ */
