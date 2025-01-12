/*
 * @file axi_dma_interface.h
 * @date 12 Jan 2025
 * @author Chester Gillon
 * @brief Defines the registers used as the Interface for the Xilinx AXI DMA
 * @details
 *   Register definitions taken from https://docs.amd.com/r/en-US/pg021_axi_dma
 *
 *   Only the sub-set of the registers required for "Direct Register Mode (Simple DMA)" are defined
 *
 */

#ifndef AXI_DMA_INTERFACE_H_
#define AXI_DMA_INTERFACE_H_


/* The base offset for the Memory Map to Stream DMA Channel direction registers */
#define AXI_DMA_MM2S_BASE_OFFSET 0x00

/* The base offset for the Stream to Memory Map DMA Channel direction registers */
#define AXI_DMA_S2MM_BASE_OFFSET 0x30


/* Register definitions which are common to both the MM2S and S2MM directions, as indicated by the use of X2X.
 * The descriptions were copied from the PG021 MM2S register descriptions, but checking the S2MM registers
 * the register definitions are the same in both directions.
 */


/* This register provides control for the Memory Map to Stream DMA Channel */
#define AXI_DMA_X2X_DMACR_OFFSET 0x00

/* Run/Stop control for controlling running and stopping of the DMA channel.
   - 0 = Stop – DMA stops when current (if any) DMA operations are
         complete. For Scatter/Gather Mode pending commands/
         transfers are flushed or completed. AXI4-Stream outs are
         potentially terminated early. Descriptors in the update queue
         are allowed to finish updating to remote memory before engine halt.

         For Direct Register mode pending commands/transfers are flushed or completed. AXI4-Stream outs are potentially terminated.

         The halted bit in the DMA Status register asserts to 1 when the DMA engine is halted. This bit is cleared by AXI DMA hardware
         when an error occurs. The CPU can also choose to clear this bit to stop DMA operations.

   - 1 = Run – Start DMA operations. The halted bit in the DMA Status register deasserts to 0 when the DMA engine begins
         operations. */
#define AXI_DMA_X2X_DMACR_RS (1u << 0)

/* Soft reset for resetting the AXI DMA core. Setting this bit to a 1
   causes the AXI DMA to be reset. Reset is accomplished gracefully.
   Pending commands/transfers are flushed or completed.
   AXI4-Stream outs are potentially terminated early. Setting either
   MM2S_DMACR.Reset = 1 or S2MM_DMACR. Reset = 1 resets the
   entire AXI DMA engine. After completion of a soft reset, all
   registers and bits are in the Reset State.
   - 0 = Normal operation.
   - 1 = Reset in progress. */
#define AXI_DMA_X2X_DMACR_RESET (1u << 2)


/* This register provides the status for the Memory Map to Stream DMA Channel. */
#define AXI_DMA_X2X_DMASR_OFFSET 0x04

/* DMA Channel Halted. Indicates the run/stop state of the DMA channel.
   - 0 = DMA channel running.
   - 1 = DMA channel halted. For Scatter / Gather Mode this bit
         gets set when DMACR.RS = 0 and DMA and Scatter Gather
         (SG) operations have halted. For Direct Register mode
         (C_INCLUDE_SG = 0) this bit gets set when DMACR.RS = 0
         and DMA operations have halted. There can be a lag of time
         between when DMACR.RS = 0 and when DMASR.Halted = 1.

   Note: When halted (RS= 0 and Halted = 1), writing to
   TAILDESC_PTR pointer registers has no effect on DMA
   operations when in Scatter Gather Mode. For Direct Register
   Mode, writing to the LENGTH register has no effect on DMA operations. */
#define AXI_DMA_X2X_DMASR_HALTED (1u << 0)

/* DMA Channel Idle. Indicates the state of AXI DMA operations.
   For Scatter/Gather Mode when IDLE indicates the SG Engine
   has reached the tail pointer for the associated channel and all
   queued descriptors have been processed. Writing to the tail
   pointer register automatically restarts DMA operations. The
   IDLE bit is associated with the BDs. The DMA might be in IDLE
   state, there might be active data on the AXI interface.
   For Direct Register Mode when IDLE indicates the current
   transfer has completed.
   - 0 = Not Idle. For Scatter/Gather Mode, SG has not reached
         tail descriptor pointer and/or DMA operations in progress.
         For Direct Register Mode, transfer is not complete.
   - 1 = Idle. For Scatter/Gather Mode, SG has reached tail
         descriptor pointer and DMA operation paused. for Direct
         Register Mode, DMA transfer has completed and controller is paused.

   Note: This bit is 0 when channel is halted (DMASR.Halted=1).
   This bit is also 0 prior to initial transfer when AXI DMA
   configured for Direct Register Mode. */
#define AXI_DMA_X2X_DMASR_IDLE (1u << 1)

/* - 1= Scatter Gather Enabled
   - 0= Scatter Gather not enabled */
#define AXI_DMA_X2X_DMASR_SGINCLD (1u << 3)


/* DMA Internal Error. This error occurs if the buffer length
   specified in the fetched descriptor is set to 0. Also, when in
   Scatter Gather Mode and using the status app length field,
   this error occurs when the Status AXI4-Stream packet
   RxLength field does not match the S2MM packet being
   received by the S_AXIS_S2MM interface. When Scatter Gather
   is disabled, this error is flagged if any error occurs during
   Memory write or if the incoming packet is bigger than what
   is specified in the DMA length register.
   This error condition causes the AXI DMA to halt gracefully.
   The DMACR.RS bit is set to 0, and when the engine has
   completely shutdown, the DMASR.Halted bit is set to 1.
   - 0 = No DMA Internal Errors
   - 1 = DMA Internal Error detected. */
#define AXI_DMA_X2X_DMASR_DMAINTERR (1u << 4)

/* DMA Decode Error. This error occurs if the address request
   points to an invalid address. This error condition causes the
   AXI DMA to halt gracefully. The DMACR.RS bit is set to 0, and
   when the engine has completely shut down, the
   DMASR.Halted bit is set to 1.
   - 0 = No DMA Decode Errors
   - 1 = DMA Decode Error detected. */
#define AXI_DMA_X2X_DMASR_DMASLVERR (1u << 5)

/* DMA Decode Error. This error occurs if the address request
   points to an invalid address. This error condition causes the
   AXI DMA to halt gracefully. The DMACR.RS bit is set to 0, and
   when the engine has completely shut down, the
   DMASR.Halted bit is set to 1.
   - 0 = No DMA Decode Errors
   - 1 = DMA Decode Error detected. */
#define AXI_DMA_X2X_DMASR_DMADECERR (1u << 6)


/* This register provides the Source Address for reading system memory for the Memory Map to Stream DMA transfer.

   Note: If Data Realignment Engine is included, the Source Address can be at any byte offset.
   If Data Realignment Engine is not included, the Source Address must be MM2S Memory Map data width aligned. */
#define AXI_DMA_X2X_SA_OFFSET 0x18

/* This register provides the upper 32 bits of the Source Address for reading system memory for the
   Memory Map to Stream DMA transfer. This is applicable only when the DMA is configured for an address space greater than 32. */
#define AXI_DMA_X2X_SA_MSB_OFFSET 0x1C


/* This register provides the number of bytes to read from system memory and transfer to MM2S AXI4-Stream.

   Indicates the number of bytes to transfer for the MM2S channel.
   Writing a non-zero value to this register starts the MM2S transfer. */
#define AXI_DMA_X2X_LENGTH_OFFSET 0x28

#endif /* AXI_DMA_INTERFACE_H_ */
