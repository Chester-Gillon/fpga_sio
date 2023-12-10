/*
 * @file xilinx_dma_bridge_host_interface.h
 * @date 29 Jan 2023
 * @author Chester Gillon
 * @brief Defines the interface to the Xilinx "DMA/Bridge Subsystem for PCI Express", from the point of view of the host
 * @details
 *  This is descriptor layout and subset of the register definitions used for DMA tests in user space via VFIO access.
 *  Details taken from
 *    https://www.xilinx.com/content/dam/xilinx/support/documents/ip_documentation/xdma/v4_1/pg195-pcie-dma.pdf
 *
 *  Defines register bits as macros for use on integers, rather than using bit fields, to allow atomic operations.
 *
 *  For 64-bit registers (e.g. addresses) uses uint64_t as assumes running on a little-endian host and simplifies
 *  the code rather than having to write to two 32-bit least significant and most significant fields.
 *
 *  To reduce duplication, where registers are common to the H2C (Host To Card) and C2H (Card To Host) directions
 *  have prefixed the names with X2X to indicate the register definitions can be used common to both directions.
 */

#ifndef XILINX_DMA_BRIDGE_HOST_INTERFACE_H_
#define XILINX_DMA_BRIDGE_HOST_INTERFACE_H_

#include <stdint.h>

/* Defines one DMA descriptor */
#define DMA_DESCRIPTOR_MAGIC (0xad4bU << 16)

#define DMA_DESCRIPTOR_CONTROL_EOP       (1U << 4)
#define DMA_DESCRIPTOR_CONTROL_COMPLETED (1U << 1)
#define DMA_DESCRIPTOR_CONTROL_STOP      (1U << 0)

/* While the features section of pg195 says "256 MB max transfer size per descriptor", given the descriptor length
 * is 28-bits wide assume the maximum length is one byte less. */
#define DMA_DESCRIPTOR_MAX_LEN ((1 << 28) - 1)

typedef struct
{
    /* Contains:
     * - 16 bits : Magic value of DMA_DESCRIPTOR_MAGIC Code to verify that the driver generated descriptor is valid.
     * - 2 bits  : reserved
     * - 6 bits  : Nxt_adj The number of additional adjacent descriptors after the descriptor located at the next descriptor
     *                     address field.
     *                     A block of adjacent descriptors cannot cross a 4k boundary.
     * - 8 bits  : Control:
     *               DMA_DESCRIPTOR_CONTROL_EOP End of packet for stream interface.
     *               DMA_DESCRIPTOR_CONTROL_COMPLETED Set to 1 to interrupt after the engine has completed this descriptor. This
     *                                                requires global IE_DESCRIPTOR_COMPLETED control flag set in the H2C/C2H
     *                                                Channel control register.
     *               DMA_DESCRIPTOR_CONTROL_STOP Set to 1 to stop fetching descriptors for this descriptor list.
     *                                           The stop bit can only be set on the last descriptor of an adjacent block of
     *                                           descriptors.
     */
    uint32_t magic_nxt_adj_control;
    /* Length of the data in bytes. Only least significant 28 bits are used. */
    uint32_t len;
    /* Source address for H2C and memory mapped transfers. Metadata writeback address for C2H stream transfers. */
    uint64_t src_adr;
    /* Destination address for C2H and memory mapped transfers. Not used for H2C stream. */
    uint64_t dst_adr;
    /* Address of the next descriptor in the list */
    uint64_t nxt_adr;
} dma_descriptor_t;


/* Defines a completed descriptor count written back to host memory when DMA poll mode is enabled */
#define COMPLETED_DESCRIPTOR_STS_ERR              0x80000000

#define COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK 0x00ffffff
typedef struct
{
    /* Contains:
     * -  1 bit  : The bitwise OR of any error status bits in the channel Status register.
     *             COMPLETED_DESCRIPTOR_STS_ERR
     * -  7 bits : Reserved
     * - 24 bits : The lower 24 bits of the Complete Descriptor Count register.
     *             Accessed by COMPLETED_DESCRIPTOR_COUNT_WRITEBACK_MASK
     */
    uint32_t sts_err_compl_descriptor_count;
} completed_descriptor_count_writeback_t;


/* Defines the C2H Channel Writeback information which provides the driver current length status of a particular descriptor,
 * when the DMA Stream interface is used. */
#define C2H_STREAM_WB_MAGIC     (0x52b4 << 16U)
#define C2H_STREAM_WB_MAGIC_MASK 0xffff0000U
#define CH2_STREAM_WB_EOP 0x1
typedef struct
{
    /* Contains:
     * 16 bits : WB magic value of C2H_STREAM_WB_MAGIC to verify the C2H writeback is valid.
     * 15 bits : reserved
     * 1 bit   : Set (CH2_STREAM_WB_EOP) to indicate End Of Packet
     */
    uint32_t wb_magic_status;
    /* Length of the data in bytes. */
    uint32_t length;
} c2h_stream_writeback_t;


/* The destination submodule within the DMA */
#define DMA_SUBMODULE_H2C_CHANNELS 0
#define DMA_SUBMODULE_C2H_CHANNELS 1
#define DMA_SUBMODULE_IRQ_BLOCK    2
#define DMA_SUBMODULE_CONFIG       3
#define DMA_SUBMODULE_H2C_SGDMA    4
#define DMA_SUBMODULE_C2H_SGDMA    5
#define DMA_SUBMODULE_SGDMA_COMMON 6
#define DMA_SUBMODULE_MSI_X        8

/* Calculate the offset within a PCIe BAR to the start of a submodule */
#define DMA_SUBMODULE_BAR_START_OFFSET(submodule) ((submodule) << 12)

/* Calculate the offset within a PCIe BAR to the start of one channel for a submodule */
#define DMA_CHANNEL_BAR_START_OFFSET(submodule,channel_id) (DMA_SUBMODULE_BAR_START_OFFSET (submodule) + ((channel_id) << 8))


/* Register at the start of a submodule block, apart from DMA_SUBMODULE_MSI_X, used to identify the submodule */
#define SUBMODULE_IDENTIFIER_OFFSET 0x0

/* Fixed value which identifies the IP */
#define SUBMODULE_IDENTIFIER_SUBSYSTEM_MASK   0xfff00000
#define SUBMODULE_IDENTIFIER_SUBSYSTEM_SHIFT  20
#define SUBMODULE_IDENTIFIER_SUBSYSTEM_ID     0x1fc /* Identity for "DMA/Bridge Subsystem for PCI Express" */

/* Should be value of DMA_SUBMODULE_* which identifies the subsystem */
#define SUBMODULE_IDENTIFIER_TARGET_MASK      0x000f0000
#define SUBMODULE_IDENTIFIER_TARGET_SHIFT     16

/* For DMA_SUBMODULE_H2C_CHANNELS, DMA_SUBMODULE_C2H_CHANNELS, DMA_SUBMODULE_H2C_SGDMA, DMA_SUBMODULE_C2H_SGDMA
 * identifies the AXI4 endpoint type:
 * 1: AXI4-Stream Interface
 * 0: AXI4 Memory Mapped Interface  */
#define SUBMODULE_IDENTIFIER_STREAM_MASK      0x00008000

/* For DMA_SUBMODULE_H2C_CHANNELS, DMA_SUBMODULE_C2H_CHANNELS, DMA_SUBMODULE_H2C_SGDMA, DMA_SUBMODULE_C2H_SGDMA
 * contains the channel_id */
#define SUBMODULE_IDENTIFIER_CHANNEL_ID_MASK  0x00000f00
#define SUBMODULE_IDENTIFIER_CHANNEL_ID_SHIFT 8

/* Contains the version of the "DMA/Bridge Subsystem for PCI Express" IP */
#define SUBMODULE_IDENTIFIER_VERSION_MASK     0x000000ff
#define SUBMODULE_IDENTIFIER_VERSION_SHIFT    0

/* Defines the Host To Card (H2C) and Card To Host (C2H) channel register space.
 * Only difference between the registers for the two directions is:
 * a. In the Read and Write errors which can be reported, since relate to the AXI4 end of the transfers.
 * b. The meaning of Stream Write Back Disable in the Channel Control register. */

#define X2X_CHANNEL_CONTROL_RW_OFFSET  0x4
#define X2X_CHANNEL_CONTROL_W1S_OFFSET 0x8
#define X2X_CHANNEL_CONTROL_W1C_OFFSET 0xC

/* Control bits for X2X_CHANNEL_CONTROL_RW_OFFSET, X2X_CHANNEL_CONTROL_W1S_OFFSET and X2X_CHANNEL_CONTROL_W1C_OFFSET.
 * These registers only differ in access:
 * - X2X_CHANNEL_CONTROL_RW_OFFSET provides read/write access to all bits
 * - X2X_CHANNEL_CONTROL_W1S_OFFSET provides Write 1 to Set access
 * - X2X_CHANNEL_CONTROL_W1C_OFFSET provides Write 1 to Clear access
 *
 * Notes:
 * 1. The ie_* register bits are interrupt enabled. When these bits are set and the corresponding condition is met, status
 *    will be logged in the X2X Channel Status (0x40). When the proper interrupt masks are set (per X2X Channel Interrupt
 *    Enable Mask (0x90)), the interrupt will be generated. */
#define H2C_CHANNEL_CONTROL_STREAM_WRITE_BACK_DISABLE (1 << 27) /* When set write back information for C2H in AXI-Stream
                                                                   mode is disabled, default write back is enabled. */
                                                                /* @todo pg195 uses "C2H" in the description of this H2C register.
                                                                 *       Also, no write back is defined for a DMA H2C Stream.
                                                                 *       Is the bit actually used? */
#define C2H_CHANNEL_CONTROL_STREAM_WRITE_BACK_DISABLE (1 << 27) /* Disables the metadata writeback for C2H AXI4-Stream. No
                                                                   effect if the channel is configured to use AXI Memory Mapped. */
#define X2X_CHANNEL_CONTROL_POLLMODE_WB_ENABLE (1 << 26) /* Poll mode writeback enable.
                                                            When this bit is set the DMA writes back the completed
                                                            descriptor count when a descriptor with the Completed bit
                                                            set, is completed. */
#define X2C_CHANNEL_CONTROL_NON_INC_MODE (1 << 25) /* Non-incrementing address mode. Applies to m_axi_araddr interface only. */
#define X2C_CHANNEL_CONTROL_IE_DESC_ERROR (0x1f << 19) /* Set to all 1s (0x1F) to enable logging of Status.Desc_error
                                                          and to stop the engine if the error is detected. */
#define H2C_CHANNEL_CONTROL_IE_WRITE_ERROR (0x1f << 14) /* Set to all 1s (0x1F) to enable logging of Status.Write_error
                                                           and to stop the engine if the error is detected. */
#define X2X_CHANNEL_CONTROL_IE_READ_ERROR (0x1f << 9) /* Set to all 1s (0x1F) to enable logging of Status.Read_error
                                                         and to stop the engine if the error is detected. */
#define X2X_CHANNEL_CONTROL_IE_IDLE_STOPPED (1 << 6) /* Set to 1 to enable logging of Status.Idle_stopped */
#define X2X_CHANNEL_CONTROL_IE_INVALID_LENGTH (1 << 5) /* Set to 1 to enable logging of Status.Invalid_length */
#define X2X_CHANNEL_CONTROL_IE_MAGIC_STOPPED (1 << 4) /* Set to 1 to enable logging of Status.Magic_stopped */
#define X2X_CHANNEL_CONTROL_IE_ALIGN_MISMATCH (1 << 3) /* Set to 1 to enable logging of Status.Align_mismatch */
#define X2X_CHANNEL_CONTROL_IE_DESCRIPTOR_COMPLETED (1 << 2) /* Set to 1 to enable logging of Status.Descriptor_completed */
#define X2X_CHANNEL_CONTROL_IE_DESCRIPTOR_STOPPED (1 << 1) /* Set to 1 to enable logging of Status.Descriptor_stopped */
#define X2X_CHANNEL_CONTROL_RUN (1 << 0) /* Set to 1 to start the SGDMA engine. Reset to 0 to stop
                                            transfer; if the engine is busy it completes the current descriptor. */

/* X2X Channel Status is defined in two register which differ in access:
 * - X2X_CHANNEL_STATUS_RW1C_OFFSET is Write 1 to Clear
 * - X2X_CHANNEL_STATUS_RC_OFFSET is Clear On Read
 */
#define X2X_CHANNEL_STATUS_RW1C_OFFSET 0x40
#define X2X_CHANNEL_STATUS_RC_OFFSET   0x44

/* X2X channel status bits */
/* X2X_CHANNEL_STATUS_DESCR_ERROR_* Reset (0) on setting the Control register Run bit. */
#define X2X_CHANNEL_STATUS_DESCR_ERROR_UNEXPECTED_COMPLETION (1 << 23)
#define X2X_CHANNEL_STATUS_DESCR_ERROR_HEADER_EP             (1 << 22)
#define X2X_CHANNEL_STATUS_DESCR_ERROR_PARITY_ERROR          (1 << 21)
#define X2X_CHANNEL_STATUS_DESCR_ERROR_COMPLETER_ABORT       (1 << 20)
#define X2X_CHANNEL_STATUS_DESCR_ERROR_UNSUPPORTED_REQUEST   (1 << 19)

/* H2C_CHANNEL_STATUS_WRITE_ERROR_* Reset (0) on setting the Control register Run bit. */
#define H2C_CHANNEL_STATUS_WRITE_ERROR_SLAVE_ERROR  (1 << 15)
#define H2C_CHANNEL_STATUS_WRITE_ERROR_DECODE_ERROR (1 << 14)

/* H2C_CHANNEL_STATUS_READ_ERROR_* Reset (0) on setting the Control register Run bit. */
#define H2C_CHANNEL_STATUS_READ_ERROR_UNEXPECTED_COMPLETION (1 << 13)
#define H2C_CHANNEL_STATUS_READ_ERROR_HEADER_EP             (1 << 12)
#define H2C_CHANNEL_STATUS_READ_ERROR_PARITY_ERROR          (1 << 11)
#define H2C_CHANNEL_STATUS_READ_ERROR_COMPLETER_ERROR       (1 << 10)
#define H2C_CHANNEL_STATUS_READ_ERROR_UNSUPPORTED_REQUEST   (1 <<  9)

/* C2H_CHANNEL_STATUS_READ_ERROR_* Reset (0) on setting the Control register Run bit. */
#define C2H_CHANNEL_STATUS_READ_ERROR_SLAVE_ERROR  (1 << 10)
#define C2H_CHANNEL_STATUS_READ_ERROR_DECODE_ERROR (1 <<  9)

#define X2X_CHANNEL_STATUS_IDLE_STOPPED (1 << 6) /* Reset (0) on setting the Control register Run bit. Set when
                                                    the engine is idle after resetting the Control register Run bit
                                                    if the Control register ie_idle_stopped bit is set. */
#define X2X_CHANNEL_STATUS_INVALID_LENGTH (1 << 5) /* Reset on setting the Control register Run bit. Set when the
                                                      descriptor length is not a multiple of the data width of an
                                                      AXI4-Stream channel and the Control register ie_invalid_length bit is set. */
#define X2X_CHANNEL_STATUS_MAGIC_STOPPED (1 << 4) /* Reset on setting the Control register Run bit. Set when the
                                                     engine encounters a descriptor with invalid magic and
                                                     stopped if the Control register ie_magic_stopped bit is set. */
#define X2X_CHANNEL_STATUS_ALIGN_MISMATCH (1 << 3) /* Source and destination address on descriptor are not
                                                      properly aligned to each other. */
#define X2X_CHANNEL_STATUS_DESCRIPTOR_COMPLETED (1 << 2) /* Reset on setting the Control register Run bit. Set after the
                                                            engine has completed a descriptor with the COMPLETE bit
                                                            set if the Control register ie_descriptor_stopped bit is set. */
#define X2X_CHANNEL_STATUS_DESCRIPTOR_STOPPED (1 << 1) /* Reset on setting Control register Run bit. Set after the
                                                          engine completed a descriptor with the STOP bit set if the
                                                          Control register ie_descriptor_stopped bit is set. */
#define X2X_CHANNEL_STATUS_BUSY (1 << 0) /* Set if the SGDMA engine is busy. Zero when it is idle. */

/* The number of competed descriptors update by the engine after completing each descriptor in the list.
   Reset to 0 on rising edge of Control register Run bit (X2X Channel Control (0x04)). */
#define X2X_CHANNEL_COMPLETED_DESCRIPTOR_COUNT_OFFSET 0x48

#define X2X_CHANNEL_ALIGNMENTS_OFFSET 0x4C
#define X2X_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_MASK 0x00ff0000 /* The byte alignment that the source and destination
                                                                 addresses must align to. This value is dependent on
                                                                 configuration parameters. */
#define X2X_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_SHIFT 16

#define X2X_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_MASK 0x0000ff00 /* The minimum granularity of DMA transfers in bytes. */
#define X2X_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_SHIFT 8

#define X2X_CHANNEL_ALIGNMENTS_ADDRESS_BITS_MASK 0x000000ff /* The number of address bits configured. */
#define X2X_CHANNEL_ALIGNMENTS_ADDRESS_BITS_SHIFT 0

#define X2X_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET 0x88

/* X2X Channel Interrupt Enable Masks at offsets 0x90, 0x94 and 0x98 not defined as use poll mode */

/* X2X Channel Channel Performance Monitor Control (0xC0)
 * X2X Channel Channel Performance Cycle Count (0xC4)
 * X2X Channel Performance Cycle Count (0xC8)
 * X2X Channel Performance Data Count (0xCC)
 * X2X Channel Performance Data Count (0xD0) */


/* IRQ Block registers are not defined as using poll mode */


/* Config Block registers are not defined as don't look necessary to use / change */


/* Defines the H2C SGDMA and C2H SGDMA register space */

/* 64-bit start descriptor address. Dsc_adr[63:0] is the first descriptor address that is fetched after the Control
   register Run bit is set. */
#define X2X_SGDMA_DESCRIPTOR_ADDRESS_OFFSET 0x80

/* dsc_adj[5:0]
 * Number of extra adjacent descriptors after the start descriptor address. */
#define X2X_SGDMA_DESCRIPTOR_ADJACENT_OFFSET 0x88

/* h2c_dsc_credit[9:0]
 * Writes to this register will add descriptor credits for the channel. This register will only be used if it is enabled via the
 * channel's bits in the Descriptor Credit Mode register. Credits are automatically cleared on the falling edge of the
 * channels Control register Run bit or if Descriptor Credit Mode is disabled for the channel. The register can be read
 * to determine the number of current remaining credits for the channel. */
#define X2X_SGDMA_DESCRIPTOR_CREDITS_OFFSET 0x8C

#define X2X_SGDMA_MAX_DESCRIPTOR_CREDITS ((1 << 10) - 1) /* Based upon ten bits to store the number of credits */


/* SGDMA Common registers space */

#define SGDMA_DESCRIPTOR_CONTROL_RW_OFFSET  0x10
#define SGDMA_DESCRIPTOR_CONTROL_W1S_OFFSET 0x14
#define SGDMA_DESCRIPTOR_CONTROL_W1C_OFFSET 0x18

/* Control bits for the SGDMA_DESCRIPTOR_CONTROL_RW_OFFSET, SGDMA_DESCRIPTOR_CONTROL_W1S_OFFSET and
 * SGDMA_DESCRIPTOR_CONTROL_W1C_OFFSET registers. These registers only differ in access:
 * - SGDMA_DESCRIPTOR_CONTROL_RW_OFFSET provides read/write access to all bits
 * - SGDMA_DESCRIPTOR_CONTROL_W1S_OFFSET provides Write 1 to Set access
 * - SGDMA_DESCRIPTOR_CONTROL_W1C_OFFSET provides Write 1 to Clear access */
#define SGDMA_DESCRIPTOR_C2H_DSC_HALT_LOW_BIT 16 /* c2h_dsc_halt[3:0]
                                                    One bit per C2H channel. Set to one to halt descriptor
                                                    fetches for corresponding channel. */
#define SGDMA_DESCRIPTOR_H2C_DSC_HALT_LOW_BIT 0 /* h2c_dsc_halt[3:0]
                                                   One bit per H2C channel. Set to one to halt descriptor
                                                   fetches for corresponding channel. */

#define SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_RW_OFFSET  0x20
#define SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET 0x24
#define SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1C_OFFSET 0x28

/* Control bits for the SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_RW_OFFSET, SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET
 * and SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1C_OFFSET registers. These registers only differ in access:
 * - SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_RW_OFFSET provides read/write access to all bits
 * - SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1S_OFFSET provides Write 1 to Set access
 * - SGDMA_DESCRIPTOR_CREDIT_MODE_ENABLE_W1C_OFFSET Write 1 to Clear access */
#define SGDMA_DESCRIPTOR_H2C_DSC_CREDIT_ENABLE_LOW_BIT 0 /* h2c_dsc_credit_enable [3:0]
                                                            One bit per H2C channel. Set to 1 to enable descriptor
                                                            crediting. For each channel, the descriptor fetch engine will
                                                            limit the descriptors fetched to the number of descriptor
                                                            credits it is given through writes to the channel's Descriptor
                                                            Credit Register. */
#define SGDMA_DESCRIPTOR_C2H_DSC_CREDIT_ENABLE_LOW_BIT 16 /* c2h_dsc_credit_enable [3:0]
                                                             One bit per C2H channel. Set to 1 to enable descriptor
                                                             crediting. For each channel, the descriptor fetch engine will
                                                             limit the descriptors fetched to the number of descriptor
                                                             credits it is given through writes to the channel's Descriptor
                                                             Credit Register. */


/* MSI-X Vector Table and PBA are not defined as using poll mode */

#endif /* XILINX_DMA_BRIDGE_HOST_INTERFACE_H_ */
