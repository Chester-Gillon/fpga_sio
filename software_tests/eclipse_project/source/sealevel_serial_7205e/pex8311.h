/*
 * @file pex8311.h
 * @date 8 Apr 2023
 * @author Chester Gillon
 * @brief Contains definitions for a PEX8311 PCI Express-to-Generic Local Bus Bridge
 */

#ifndef PEX8311_H_
#define PEX8311_H_

#include "stdint.h"
#include "vfio_access.h"


/* BAR which contains the internal shared memory */
#define PEX8311_SHARED_MEMORY_BAR_INDEX 0

/* The size of the internal shared memory in the PEX 8311 */
#define PEX8311_SHARED_MEMORY_SIZE_BYTES (8 * 1024)

/* Offset to the internal shared memory in the PEX8311_SHARED_MEMORY_BAR_INDEX */
#define PEX8311_SHARED_MEMORY_START_OFFSET 0x8000

/* 16-bit Prefetchable Memory Base register, in either PCI configuration space or memory mapped */
#define PEX_PECS_PREBASE 0x24

#define PEX_PECS_PREBASE_CAPABILITY_MASK 0xf
#define PEX_PECS_PREBASE_CAPABILITY_32_BIT 0
#define PEX_PECS_PREBASE_CAPABILITY_64_BIT 1


/* BAR for PCI Express Base Address for Memory Accesses to Local, Runtime, DMA, and Messaging Queue Registers */
#define PEX_LCS_MMIO_BAR_INDEX 0

/* BARs in PCI Express space to access local bus */
#define PEX_LOCAL_SPACE0_BAR_INDEX 2
#define PEX_LOCAL_SPACE1_BAR_INDEX 3

/* Direct Slave Local Address Space 0 and 1 Local Base Address (Remap) registers as PCI addresses.
 * Used to find the local bus base address for peripherals to allow them to be accessed by the PEX8311 DMA */
#define PEX_LCS_LAS0BA 0x04
#define PEX_LCS_LAS1BA 0xF4

/* Mask containing the Direct Slave Local Address bits, assuming mapped into Memory Space.
 * When are mapped into I/O space 2 more address bits are used. */
#define PEX_LCS_LASxBA_ADDR_MASK 0xFFFFFFF0

/* Local Address Space 0/Expansion ROM Bus Region Descriptor */
#define PEX_LCS_LBRD0 0x18

/* Local Address Space 1 Bus Region Descriptor */
#define PEX_LCS_LBRD1 0xF8

/* Mask for the PEX_LCS_LBRD0 and PEX_LCS_LBRD1 which defines settings related to bus parameters which can also be
 * used for DMA:
 * - Bits 1:0 Local Bus Data Width
 * - Bits 5:2 Internal Wait State Counter
 * - Bit    6 READY#/TA# Input Enable
 * - Bit    7 Continuous Burst Enable
 */
#define PEX_LCS_LBRDx_BUS_PARAMETERS_MASK 0xFF

/* DMA mode registers for each DMA channel */
#define PEX_LCS_DMAMODE0 0x80
#define PEX_LCS_DMAMODE1 0x94

#define PEX_LCS_DMAMODEx_LOCAL_BURST_ENABLE                       (1 <<  8)
#define PEX_LCS_DMAMODEx_SCATTER_GATHER_MODE                      (1 <<  9)
#define PEX_LCS_DMAMODEx_DONE_INTERRUPT_ENABLE                    (1 << 10)
#define PEX_LCS_DMAMODEx_LOCAL_ADDRESSING_MODE_INCREMENT          (0 << 11)
#define PEX_LCS_DMAMODEx_LOCAL_ADDRESSING_MODE_CONSTANT           (1 << 11)
#define PEX_LCS_DMAMODEx_DEMAND_MODE                              (1 << 12)
#define PEX_LCS_DMAMODEx_MEMORY_WRITE_AND_INVALIDATE_MODE         (1 << 13)
#define PEX_LCS_DMAMODEx_EOT_ENABLE                               (1 << 14)
#define PEX_LCS_DMAMODEx_TERMINATE_MODE_SLOW                      (0 << 15)
#define PEX_LCS_DMAMODEx_TERMINATE_MODE_FAST                      (1 << 15)
#define PEX_LCS_DMAMODEx_CLEAR_COUNT_MODE                         (1 << 16)
#define PEX_LCS_DMAMODEx_INTERRUPT_SELECT                         (1 << 17)
#define PEX_LCS_DMAMODEx_DAC_CHAIN_LOAD                           (1 << 18)
#define PEX_LCS_DMAMODEx_EOT_END_LINK                             (1 << 19)
#define PEX_LCS_DMAMODEx_RING_MANAGEMENT_VALID_MODE_ENABLE        (1 << 20)
#define PEX_LCS_DMAMODEx_RING_MANAGEMENT_VALID_STOP_CONTROL_POLL  (0 << 21)
#define PEX_LCS_DMAMODEx_RING_MANAGEMENT_VALID_STOP_CONTROL_STOPS (1 << 21)

/* DMA channel PCI Express Address registers */
#define PEX_LCS_DMAPADR0 0x84
#define PEX_LCS_DMAPADR1 0x98

/* DMA channel Local Address registers */
#define PEX_LCS_DMALADR0 0x88
#define PEX_LCS_DMALADR1 0x9C

/* DMA channel descriptor pointer registers */
#define PEX_LCS_DMADPR0 0x90
#define PEX_LCS_DMADPR1 0xA4

#define PEX_LCS_DMADPRx_LOCATION_PCI_EXPRESS_ADDRESS_SPACE (1 << 0)
#define PEX_LCS_DMADPRx_END_OF_CHAIN                       (1 << 1)
#define PEX_LCS_DMADPRx_INTERRUPT_AFTER_TERMINAL_COUNT     (1 << 2)
#define PEX_LCS_DMADPRx_DIRECTION_MASK                     (1 << 3)
#define PEX_LCS_DMADPRx_DIRECTION_PCI_TO_LOCAL             (0 << 3)
#define PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI             (1 << 3)

/* DMA channel transfer size (bytes) registers */
#define PEX_LCS_DMASIZ0 0x8C
#define PEX_LCS_DMASIZ1 0xA0

/* Maximum DMA transfer size given 23 bits used for the size */
#define PEX_MAX_DMA_TRANSFER_SIZE_BYTES ((1 << 23) - 1)

/* DMA channel Command/Status registers (which are 8-bits) */
#define PEX_LCS_DMACSR0 0xA8
#define PEX_LCS_DMACSR1 0xA9

#define PEX_LCS_DMACSRx_ENABLE          (1 << 0)
#define PEX_LCS_DMACSRx_START           (1 << 1)
#define PEX_LCS_DMACSRx_ABORT           (1 << 2)
#define PEX_LCS_DMACSRx_CLEAR_INTERRUPT (1 << 3)
#define PEX_LCS_DMACSRx_DONE            (1 << 4)

/* DMA channel PCI Express Dual Address Cycle Upper Address registers */
#define PEX_LCS_DMADAC0 0xB4
#define PEX_LCS_DMADAC1 0xB8

/* Mode/DMA Arbitration register */
#define PEX_LCS_MARBR 0x08

#define PEX_LCS_MARBR_LOCAL_BUS_LATENCY_TIMER_MASK  0x000000FF
#define PEX_LCS_MARBR_LOCAL_BUS_LATENCY_TIMER_SHIFT 0
#define PEX_LCS_MARBR_LOCAL_BUS_PAUSE_TIMER_MASK    0x0000FF00
#define PEX_LCS_MARBR_LOCAL_BUS_PAUSE_TIMER_SHIFT   8
#define PEX_LCS_MARBR_LOCAL_BUS_LATENCY_TIMER_ENABLE (1 << 16)
#define PEX_LCS_MARBR_LOCAL_BUS_PAUSE_TIMER_ENABLE   (1 << 17)
#define PEX_LCS_MARBR_LOCAL_BUS_BREQI_ENABLE         (1 << 18)
#define PEX_LCS_MARBR_DMA_CHANNEL_PRIORITY_MASK     0x00180000
#define PEX_LCS_MARBR_DMA_CHANNEL_PRIORITY_ROTATIONAL (0 << 19)
#define PEX_LCS_MARBR_DMA_CHANNEL_PRIORITY_CH0        (1 << 19)
#define PEX_LCS_MARBR_DMA_CHANNEL_PRIORITY_CH1        (2 << 19)
#define PEX_LCS_MARBR_LOCAL_BUS_DIRECT_SLAVE_RELEASE_BUS_MODE_MASK (1 << 21)
#define PEX_LCS_MARBR_DIRECT_SLAVE_INTERNAL_LOCK_INPUT_ENABLE (1 << 22)
#define PEX_LCS_MARBR_PCI_COMPLIANCE_ENABLE           (1 << 24)
#define PEX_LCS_MARBR_PCI_NO_WRITE_MODE               (1 << 25)
#define PEX_LCS_MARBR_PCI_READ_WITH_WRITE_FLUSH_MODE  (1 << 26)
#define PEX_LCS_MARBR_C_AND_J_MODE_GATE_LOCAL_BUS_LATENCY_TIMER_WITH_BREQI (1 << 27)
#define PEX_LCS_MARBR_PCI_NO_READ_FLUSH_MODE          (1 << 28)
#define PEX_LCS_MARBR_DEVICE_AND_VENDOR_ID_SELECT     (1 << 29)
#define PEX_LCS_MARBR_DIRECT_MASTER_WRITE_FIFO_FULL_STATUS_FLAG (1 << 30)
#define PEX_LCS_MARBR_M_MODE_BIGEND_WAIT_IO_SELECT    (1 << 31)


/* Defines one DMA descriptor in host memory for the PEX8311 "Ring Management DMA Scatter/Gather Mode Descriptor Initialization"
 * using PCI Express Short Format. */
typedef struct
{
    /* Bits 22:0  are the number of bytes to transfer during a DMA operation
     * Bits 30:23 are reserved
     * Bit  31    is DMA Channel 0 Ring Management Valid
     */
    uint32_t transfer_size_bytes;
    /* Indicates from where in PCI Express Memory space DMA transfers (Reads or Writes) start.
     * I.e. constrained to below 4-GB Address Boundary space. */
    uint32_t pci_express_address_low;
    /* Indicates from where in Local Memory space DMA transfers (Reads or Writes) start. */
    uint32_t first_local_address;
    /* Bits 31:4 are the DMA Channel 0 Next Descriptor Address, meaning the descriptor needs 16 byte alignment.
     * Bits 3:0 are PEX_LCS_DMADPRx flags. */
    uint32_t next_descriptor_address;
} pex_ring_dma_descriptor_short_format_t;

/* Flag for transfer_size_bytes in pex_ring_dma_descriptor_short_format_t */
#define PEX_XFER_SIZE_RING_MANAGEMENT_VALID (1 << 31)


/* Defines the content used to manage a ring of DMA descriptors for one DMA channel of a PEX8311. */
typedef struct
{
    /* The number of descriptors in the ring */
    uint32_t num_descriptors;
    /* The allocated array of descriptors in host memory. Number of elements is num_descriptors */
    pex_ring_dma_descriptor_short_format_t *descriptors;
    /* Mapped to the PCI Express Base Address of the PEX8311 Local Configuration Space registers */
    uint8_t *lcs;
    /* Offset to the DMA Channel Command/Status register used for the channel */
    uint32_t dmacsr_offset;
    /* Index of the descriptor which the host queues next */
    uint32_t host_descriptor_index;
    /* Index of the descriptor which is polled for completion by DMA */
    uint32_t dma_descriptor_index;
    /* The number of descriptors which are currently in use */
    uint32_t num_in_use_descriptors;
    /* Set true when have seen num_in_use_descriptors drop to zero by polling the descriptors, but are waiting to poll the
     * DMSCSR indicate the DMA engine is idle before can start a further DMA transfer. */
    bool awaiting_dmacsr_idle;
} pex_dma_ring_context_t;


/* Defines the context used to manage DMA block mode for for one DMA channel of a PEX8311 */
typedef struct
{
    /* Mapped to the PCI Express Base Address of the PEX8311 Local Configuration Space registers */
    uint8_t *lcs;
    /* Offset to the DMA registers used for the channel */
    uint32_t dmacsr_offset;
    uint32_t dmapadr_offset;
    uint32_t dmaladr_offset;
    uint32_t dmasiz_offset;
    uint32_t dmadac_offset;
    uint32_t dmadpr_offset;
} pex_dma_block_context_t;


void pex_dump_lcs_registers (const uint8_t *const lcs, const char *const point_of_dump);
bool pex_check_ring_dma_iova_constraints (const vfio_dma_mapping_t *const mapping);
void pex_initialise_dma_ring (pex_dma_ring_context_t *const ring,
                              uint8_t *const lcs,
                              const uint32_t dma_channel,
                              const uint32_t num_descriptors,
                              vfio_dma_mapping_t *const mapping);
void pex_update_descriptor_in_ring (pex_dma_ring_context_t *const ring,
                                    const uint32_t transfer_size_bytes,
                                    const uint32_t pci_express_address_low,
                                    const uint32_t first_local_address,
                                    const uint32_t direction);
void pex_start_dma_ring (pex_dma_ring_context_t *const ring);
bool pex_poll_dma_ring_completion (pex_dma_ring_context_t *const ring);
void pex_initialise_dma_block (pex_dma_block_context_t *const block,
                               uint8_t *const lcs,
                               const uint32_t dma_channel);
void pex_start_dma_block (pex_dma_block_context_t *const block,
                          const uint32_t transfer_size_bytes,
                          const uint64_t pci_express_address,
                          const uint32_t first_local_address,
                          const uint32_t direction);
bool pex_poll_dma_block_completion (pex_dma_block_context_t *const block);

#endif /* PEX8311_H_ */
