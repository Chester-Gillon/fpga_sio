/*
 * @file xilinx_dma_registers.h
 * @date 29 Jan 2023
 * @author Chester Gillon
 * @brief Register definitions for Xilinx "DMA/Bridge Subsystem for PCI Express"
 * @details
 *  This is the subset of the register definitions used for DMA tests in user space via VFIO access.
 *  Register details taken from
 *    https://www.xilinx.com/content/dam/xilinx/support/documents/ip_documentation/xdma/v4_1/pg195-pcie-dma.pdf
 *
 *  Defines register bits as macros for use on integers, rather than using bit fields, to allow atomic operations.
 *
 *  For 64-bit registers (e.g. addresses) uses uint64_t as assumes running on a little-endian host and simplifies
 *  the code rather than having to write to two 32-bit least significant and most significant fields.
 */

#ifndef SOURCE_NITE_OR_LITE_FURY_TESTS_XILINX_DMA_REGISTERS_H_
#define SOURCE_NITE_OR_LITE_FURY_TESTS_XILINX_DMA_REGISTERS_H_

#include <stdint.h>

/* Defines one DMA descriptor */
#define DMA_DESCRIPTOR_MAGIC (0xad4b << 16)

#define DMA_DESCRIPTOR_CONTROL_EOP       (1 << 4)
#define DMA_DESCRIPTOR_CONTROL_COMPLETED (1 << 1)
#define DMA_DESCRIPTOR_CONTROL_STOP      (1 << 0)

/* While the features section of pg195 says "256 MB max transfer size per descriptor", given the descriptor length
 * is 18-bits wide assume the maximum length is one byte less. */
#define DMA_DESCRIPTOR_MAX_LEN ((1 << 18) - 1)

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
    /* Source address for H2C and memory mapped transfers. Metadata writeback address for C2H transfers. */
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


/* Defines the Host To Card (H2C) channel register space */

#define H2C_CHANNEL_IDENTIFIER_OFFSET 0x0

#define H2C_CHANNEL_CONTROL_RW_OFFSET  0x4
#define H2C_CHANNEL_CONTROL_W1S_OFFSET 0x8
#define H2C_CHANNEL_CONTROL_W1C_OFFSET 0xC

/* Control bits for H2C_CHANNEL_CONTROL_RW_OFFSET, H2C_CHANNEL_CONTROL_W1S_OFFSET and H2C_CHANNEL_CONTROL_W1C_OFFSET.
 * These registers only different in:
 * - H2C_CHANNEL_CONTROL_RW_OFFSET provides read/write access to all bits
 * - H2C_CHANNEL_CONTROL_W1S_OFFSET provides Write 1 to Set access
 * - H2C_CHANNEL_CONTROL_W1C_OFFSET provides Write 1 to Clear access
 *
 * Notes:
 * 1. The ie_* register bits are interrupt enabled. When these bits are set and the corresponding condition is met, status
 *    will be logged in the H2C Channel Status (0x40). When the proper interrupt masks are set (per H2C Channel Interrupt
 *    Enable Mask (0x90)), the interrupt will be generated. */
#define H2C_CHANNEL_CONTROL_STREAM_WRITE_BACK_DISABLE (1 << 27) /* When set write back information for C2H in AXI-Stream
                                                                   mode is disabled, default write back is enabled. */
#define H2C_CHANNEL_CONTROL_POLLMODE_WB_ENABLE (1 << 26) /* Poll mode writeback enable.
                                                            When this bit is set the DMA writes back the completed
                                                            descriptor count when a descriptor with the Completed bit
                                                            set, is completed. */
#define H2C_CHANNEL_CONTROL_NON_INC_MODE (1 << 25) /* Non-incrementing address mode. Applies to m_axi_araddr interface only. */
#define H2C_CHANNEL_CONTROL_IE_DESC_ERROR (0x1f << 19) /* Set to all 1s (0x1F) to enable logging of Status.Desc_error
                                                          and to stop the engine if the error is detected. */
#define H2C_CHANNEL_CONTROL_IE_WRITE_ERROR (0x1f << 14) /* Set to all 1s (0x1F) to enable logging of Status.Write_error
                                                           and to stop the engine if the error is detected. */
#define H2C_CHANNEL_CONTROL_IE_READ_ERROR (0x1f << 9) /* Set to all 1s (0x1F) to enable logging of Status.Read_error
                                                         and to stop the engine if the error is detected. */
#define H2C_CHANNEL_CONTROL_IE_IDLE_STOPPED (1 << 6) /* Set to 1 to enable logging of Status.Idle_stopped */
#define H2C_CHANNEL_CONTROL_IE_INVALID_LENGTH (1 << 5) /* Set to 1 to enable logging of Status.Invalid_length */
#define H2C_CHANNEL_CONTROL_IE_MAGIC_STOPPED (1 << 4) /* Set to 1 to enable logging of Status.Magic_stopped */
#define H2C_CHANNEL_CONTROL_IE_ALIGN_MISMATCH (1 << 3) /* Set to 1 to enable logging of Status.Align_mismatch */
#define H2C_CHANNEL_CONTROL_IE_DESCRIPTOR_COMPLETED (1 << 2) /* Set to 1 to enable logging of Status.Descriptor_completed */
#define H2C_CHANNEL_CONTROL_IE_DESCRIPTOR_STOPPED (1 << 1) /* Set to 1 to enable logging of Status.Descriptor_stopped */
#define H2C_CHANNEL_CONTROL_RUN (1 << 0) /* Set to 1 to start the SGDMA engine. Reset to 0 to stop
                                            transfer; if the engine is busy it completes the current descriptor. */

/* H2C Channel Status is defined in two register which differ in access:
 * - H2C_CHANNEL_STATUS_RW1C_OFFSET is Write 1 to Clear
 * - H2C_CHANNEL_STATUS_RC_OFFSET is Clear On Read
 */
#define H2C_CHANNEL_STATUS_RW1C_OFFSET 0x40
#define H2C_CHANNEL_STATUS_RC_OFFSET   0x44

/* H2C channel status bits */
/* H2C_CHANNEL_STATUS_DESCR_ERROR_* Reset (0) on setting the Control register Run bit. */
#define H2C_CHANNEL_STATUS_DESCR_ERROR_UNEXPECTED_COMPLETION (1 << 23)
#define H2C_CHANNEL_STATUS_DESCR_ERROR_HEADER_EP             (1 << 22)
#define H2C_CHANNEL_STATUS_DESCR_ERROR_PARITY_ERROR          (1 << 21)
#define H2C_CHANNEL_STATUS_DESCR_ERROR_COMPLETER_ABORT       (1 << 20)
#define H2C_CHANNEL_STATUS_DESCR_ERROR_UNSUPPORTED_REQUEST   (1 << 19)

/* H2C_CHANNEL_STATUS_WRITE_ERROR_* Reset (0) on setting the Control register Run bit. */
#define H2C_CHANNEL_STATUS_WRITE_ERROR_SLAVE_ERROR  (1 << 15)
#define H2C_CHANNEL_STATUS_WRITE_ERROR_DECODE_ERROR (1 << 14)

/* H2C_CHANNEL_STATUS_READ_ERROR_* Reset (0) on setting the Control register Run bit. */
#define H2C_CHANNEL_STATUS_READ_ERROR_UNEXPECTED_COMPLETION (1 << 13)
#define H2C_CHANNEL_STATUS_READ_ERROR_HEADER_EP             (1 << 12)
#define H2C_CHANNEL_STATUS_READ_ERROR_PARITY_ERROR          (1 << 11)
#define H2C_CHANNEL_STATUS_READ_ERROR_COMPLETER_ERROR       (1 << 10)
#define H2C_CHANNEL_STATUS_READ_ERROR_UNSUPPORTED_REQUEST   (1 <<  9)

#define H2C_CHANNEL_STATUS_IDLE_STOPPED (1 << 6) /* Reset (0) on setting the Control register Run bit. Set when
                                                    the engine is idle after resetting the Control register Run bit
                                                    if the Control register ie_idle_stopped bit is set. */
#define H2C_CHANNEL_STATUS_INVALID_LENGTH (1 << 5) /* Reset on setting the Control register Run bit. Set when the
                                                      descriptor length is not a multiple of the data width of an
                                                      AXI4-Stream channel and the Control register ie_invalid_length bit is set. */
#define H2C_CHANNEL_STATUS_MAGIC_STOPPED (1 << 4) /* Reset on setting the Control register Run bit. Set when the
                                                     engine encounters a descriptor with invalid magic and
                                                     stopped if the Control register ie_magic_stopped bit is set. */
#define H2C_CHANNEL_STATUS_ALIGN_MISMATCH (1 << 3) /* Source and destination address on descriptor are not
                                                      properly aligned to each other. */
#define H2C_CHANNEL_STATUS_DESCRIPTOR_COMPLETED (1 << 2) /* Reset on setting the Control register Run bit. Set after the
                                                            engine has completed a descriptor with the COMPLETE bit
                                                            set if the Control register ie_descriptor_stopped bit is set. */
#define H2C_CHANNEL_STATUS_DESCRIPTOR_STOPPED (1 << 1) /* Reset on setting Control register Run bit. Set after the
                                                          engine completed a descriptor with the STOP bit set if the
                                                          Control register ie_descriptor_stopped bit is set. */
#define H2C_CHANNEL_STATUS_BUSY (1 << 0) /* Set if the SGDMA engine is busy. Zero when it is idle. */

/* The number of competed descriptors update by the engine after completing each descriptor in the list.
   Reset to 0 on rising edge of Control register Run bit (H2C Channel Control (0x04)). */
#define H2C_CHANNEL_COMPLETED_DESCRIPTOR_COUNT_OFFSET 0x48

#define H2C_CHANNEL_ALIGNMENTS_OFFSET 0x4C
#define H2C_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_MASK 0x00ff0000 /* The byte alignment that the source and destination
                                                                 addresses must align to. This value is dependent on
                                                                 configuration parameters. */
#define H2C_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_SHIFT 16

#define H2C_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_MASK 0x0000ff00 /* The minimum granularity of DMA transfers in bytes. */
#define H2C_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_SHIFT 8

#define H2C_CHANNEL_ALIGNMENTS_ADDRESS_BITS_MASK 0x000000ff /* The number of address bits configured. */
#define H2C_CHANNEL_ALIGNMENTS_ADDRESS_BITS_SHIFT 0

#define H2C_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET 0x88

/* H2C Channel Interrupt Enable Masks at offsets 0x90, 0x94 and 0x98 not defined as use poll mode */

/* H2C Channel Channel Performance Monitor Control (0xC0)
 * H2C Channel Channel Performance Cycle Count (0xC4)
 * H2C Channel Performance Cycle Count (0xC8)
 * H2C Channel Performance Data Count (0xCC)
 * H2C Channel Performance Data Count (0xD0) */


/* Defines the Card To Host (C2H) channel register space */

#define C2H_CHANNEL_IDENTIFIER_OFFSET 0x0

#define C2H_CHANNEL_CONTROL_RW_OFFSET  0x4
#define C2H_CHANNEL_CONTROL_W1S_OFFSET 0x8
#define C2H_CHANNEL_CONTROL_W1C_OFFSET 0xC

/* Control bits for C2H_CHANNEL_CONTROL_RW_OFFSET, C2H_CHANNEL_CONTROL_W1S_OFFSET and C2H_CHANNEL_CONTROL_W1C_OFFSET.
 * These registers only different in:
 * - C2H_CHANNEL_CONTROL_RW_OFFSET provides read/write access to all bits
 * - C2H_CHANNEL_CONTROL_W1S_OFFSET provides Write 1 to Set access
 * - C2H_CHANNEL_CONTROL_W1C_OFFSET provides Write 1 to Clear access
 *
 * Notes:
 * 1. The ie_* register bits are interrupt enabled. When these bits are set and the corresponding condition is met, the
 * status will be logged in the C2H Channel Status (0x40). When proper interrupt masks are set (per C2H Channel
 * Interrupt Enable Mask (0x90) ), the interrupt will be generated. */
#define C2H_CHANNEL_CONTROL_STREAM_WRITE_BACK_DISABLE (1 << 27) /* Disables the metadata writeback for C2H AXI4-Stream. No
                                                                   effect if the channel is configured to use AXI Memory Mapped. */
#define C2H_CHANNEL_CONTROL_POLLMODE_WB_ENABLE (1 << 26) /* Poll mode writeback enable.
                                                            When this bit is set the DMA writes back the completed
                                                            descriptor count when a descriptor with the Completed bit
                                                            set, is completed. */
#define C2H_CHANNEL_CONTROL_NON_INC_MODE (1 << 25) /* Non-incrementing address mode. Applies to m_axi_araddr interface only. */
#define C2H_CHANNEL_CONTROL_IE_DESC_ERROR (0x1f << 19) /* Set to all 1s (0x1F) to enable logging of Status.Desc_error
                                                          and to stop the engine if the error is detected. */
#define C2H_CHANNEL_CONTROL_IE_READ_ERROR (0x1f << 9) /* Set to all 1s (0x1F) to enable logging of Status.Read_error
                                                         and to stop the engine if the error is detected. */
#define C2H_CHANNEL_CONTROL_IE_IDLE_STOPPED (1 << 6) /* Set to 1 to enable logging of Status.Idle_stopped */
#define C2H_CHANNEL_CONTROL_IE_INVALID_LENGTH (1 << 5) /* Set to 1 to enable logging of Status.Invalid_length */
#define C2H_CHANNEL_CONTROL_IE_MAGIC_STOPPED (1 << 4) /* Set to 1 to enable logging of Status.Magic_stopped */
#define C2H_CHANNEL_CONTROL_IE_ALIGN_MISMATCH (1 << 3) /* Set to 1 to enable logging of Status.Align_mismatch */
#define C2H_CHANNEL_CONTROL_IE_DESCRIPTOR_COMPLETED (1 << 2) /* Set to 1 to enable logging of Status.Descriptor_completed */
#define C2H_CHANNEL_CONTROL_IE_DESCRIPTOR_STOPPED (1 << 1) /* Set to 1 to enable logging of Status.Descriptor_stopped */
#define C2H_CHANNEL_CONTROL_RUN (1 << 0) /* Set to 1 to start the SGDMA engine. Reset to 0 to stop
                                            transfer; if the engine is busy it completes the current descriptor. */

/* C2H Channel Status is defined in two register which differ in access:
 * - C2H_CHANNEL_STATUS_RW1C_OFFSET is Write 1 to Clear
 * - C2H_CHANNEL_STATUS_RC_OFFSET is Clear On Read
 */
#define C2H_CHANNEL_STATUS_RW1C_OFFSET 0x40
#define C2H_CHANNEL_STATUS_RC_OFFSET   0x44

/* C2H channel status bits */
/* C2H_CHANNEL_STATUS_DESCR_ERROR_* Reset (0) on setting the Control register Run bit. */
#define C2H_CHANNEL_STATUS_DESCR_ERROR_UNEXPECTED_COMPLETION (1 << 23)
#define C2H_CHANNEL_STATUS_DESCR_ERROR_HEADER_EP             (1 << 22)
#define C2H_CHANNEL_STATUS_DESCR_ERROR_PARITY_ERROR          (1 << 21)
#define C2H_CHANNEL_STATUS_DESCR_ERROR_COMPLETER_ABORT       (1 << 20)
#define C2H_CHANNEL_STATUS_DESCR_ERROR_UNSUPPORTED_REQUEST   (1 << 19)

/* C2H_CHANNEL_STATUS_READ_ERROR_* Reset (0) on setting the Control register Run bit. */
#define C2H_CHANNEL_STATUS_READ_ERROR_SLAVE_ERROR  (1 << 10)
#define C2H_CHANNEL_STATUS_READ_ERROR_DECODE_ERROR (1 <<  9)

#define C2H_CHANNEL_STATUS_IDLE_STOPPED (1 << 6) /* Reset (0) on setting the Control register Run bit. Set when
                                                    the engine is idle after resetting the Control register Run bit
                                                    if the Control register ie_idle_stopped bit is set. */
#define C2H_CHANNEL_STATUS_INVALID_LENGTH (1 << 5) /* Reset on setting the Control register Run bit. Set when the
                                                      descriptor length is not a multiple of the data width of an
                                                      AXI4-Stream channel and the Control register ie_invalid_length bit is set. */
#define C2H_CHANNEL_STATUS_MAGIC_STOPPED (1 << 4) /* Reset on setting the Control register Run bit. Set when the
                                                     engine encounters a descriptor with invalid magic and
                                                     stopped if the Control register ie_magic_stopped bit is set. */
#define C2H_CHANNEL_STATUS_ALIGN_MISMATCH (1 << 3) /* Source and destination address on descriptor are not
                                                      properly aligned to each other. */
#define C2H_CHANNEL_STATUS_DESCRIPTOR_COMPLETED (1 << 2) /* Reset on setting the Control register Run bit. Set after the
                                                            engine has completed a descriptor with the COMPLETE bit
                                                            set if the Control register ie_descriptor_stopped bit is set. */
#define C2H_CHANNEL_STATUS_DESCRIPTOR_STOPPED (1 << 1) /* Reset on setting Control register Run bit. Set after the
                                                          engine completed a descriptor with the STOP bit set if the
                                                          Control register ie_descriptor_stopped bit is set. */
#define C2H_CHANNEL_STATUS_BUSY (1 << 0) /* Set if the SGDMA engine is busy. Zero when it is idle. */

/* The number of competed descriptors update by the engine after completing each descriptor in the list.
   Reset to 0 on rising edge of Control register, run bit (C2H Channel Control (0x04)). */
#define C2H_CHANNEL_COMPLETED_DESCRIPTOR_COUNT_OFFSET 0x48

#define C2H_CHANNEL_ALIGNMENTS_OFFSET 0x4C
#define C2H_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_MASK 0x00ff0000 /* The byte alignment that the source and destination
                                                                 addresses must align to. This value is dependent on
                                                                 configuration parameters. */
#define C2H_CHANNEL_ALIGNMENTS_ADDR_ALIGNMENT_SHIFT 16

#define C2H_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_MASK 0x0000ff00 /* The minimum granularity of DMA transfers in bytes. */
#define C2H_CHANNEL_ALIGNMENTS_LEN_GRANULARITY_SHIFT 8

#define C2H_CHANNEL_ALIGNMENTS_ADDRESS_BITS_MASK 0x000000ff /* The number of address bits configured. */
#define C2H_CHANNEL_ALIGNMENTS_ADDRESS_BITS_SHIFT 0

#define C2H_CHANNEL_POLL_MODE_WRITE_BACK_ADDRESS_OFFSET 0x88

/* C2H Channel Interrupt Enable Masks at offsets 0x90, 0x94 and 0x98 not defined as use poll mode */

/* C2H Channel Channel Performance Monitor Control (0xC0)
 * C2H Channel Channel Performance Cycle Count (0xC4)
 * C2H Channel Performance Cycle Count (0xC8)
 * C2H Channel Performance Data Count (0xCC)
 * C2H Channel Performance Data Count (0xD0) */


#endif /* SOURCE_NITE_OR_LITE_FURY_TESTS_XILINX_DMA_REGISTERS_H_ */
