/*
 * @file xilinx_axi_iic_host_interface.h
 * @date 25 Feb 2023
 * @author Chester Gillon
 * @brief Defines the interface to the Xilinx "AXI IIC Bus Interface" from the point of view of the host.
 * @details
 *  This is subset of register definitions used for user space access via VFIO.
 *  Details taken from https://www.xilinx.com/support/documents/ip_documentation/axi_iic/v2_1/pg090-axi-iic.pdf
 */

#ifndef XILINX_AXI_IIC_HOST_INTERFACE_H_
#define XILINX_AXI_IIC_HOST_INTERFACE_H_

/* Global Interrupt Enable Register is not defined as are using polling */

/* Used to determine which interrupt events from the AXI IIC core need servicing */
#define IIC_INTERRUPT_STATUS_REGISTER_OFFSET 0x020

#define IIC_ISR_ARBITRATION_LOST_MASK                       (1U << 0)
#define IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK (1U << 1)
#define IIC_ISR_TRANSMIT_FIFO_EMPTY_MASK                    (1U << 2)
#define IIC_ISR_RECEIVE_FIFO_FULL_MASK                      (1U << 3)
#define IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK                    (1U << 4)
#define IIC_ISR_ADDRESSED_AS_SLAVE_MASK                     (1U << 5)
#define IIC_ISR_NOT_ADDRESSED_AS_SLAVE_MASK                 (1U << 6)
#define IIC_ISR_TRANSMIT_FIFO_HALF_EMPTY_MASK               (1 << 7)

/* Used to enable or disable interrupts. Uses the same IIC_ISR_*MASK as IIC_INTERRUPT_STATUS_REGISTER_OFFSET */
#define IIC_INTERRUPT_ENABLE_REGISTER_OFFSET 0x028

/* Firmware can write to the SOFTR to initialize all of the AXI IIC core registers to their default
   states. To accomplish this, firmware must write the reset key (RKEY) value of 0xA to the least
   significant nibble of the 32-bit word. After recognizing a write of 0xA the proc_common
   soft_reset module issues a pulse four clocks long to reset the AXI IIC core. At the end of the
   pulse the SOFTR acknowledges the AXI transaction. That prevents anything further from
   happening while the reset occurs. */
#define IIC_SOFT_RESET_REGISTER_OFFSET 0x040
#define IIC_SOFT_RESET_KEY 0xA /* Firmware must write a value of 0xA to this field to
                                  cause a soft reset of the Interrupt registers of AXI IIC controller.
                                  Writing any other value results in an AXI transaction
                                  acknowledgement with SLVERR and no reset occurs. */

/* Writing to the Control register configures the AXI IIC core operation mode and simultaneously allows for IIC
 * transactions to be initiated. */
#define IIC_CONTROL_REGISTER_OFFSET 0x100

#define IIC_CR_EN_MASK (1U << 0) /* AXI IIC Enable. This bit must be set before any other CR bits have any effect.
                                    0 = resets and disables the AXI IIC controller but not the registers or FIFOs
                                    1 = enables the AXI IIC controller */
#define IIC_CR_TX_FIFO_RESET_MASK (1U << 1) /* Transmit FIFO Reset. This bit must be set to flush the FIFO if either
                                               (a) arbitration is lost or (b) if a transmit error occurs.
                                               0 = transmit FIFO normal operation
                                               1 = resets the transmit FIFO */
#define IIC_CR_MSMS_MASK (1U << 2) /* Master/Slave Mode Select. When this bit is changed from 0 to 1, the
                                      AXI IIC bus interface generates a START condition in master mode. When
                                      this bit is cleared, a STOP condition is generated and the AXI IIC bus
                                      interface switches to slave mode. When this bit is cleared by the
                                      hardware, because arbitration for the bus has been lost, a STOP
                                      condition is not generated */
#define IIC_CR_TX_MASK (1U << 3) /* Transmit/Receive Mode Select. This bit selects the direction of master/ slave transfers.
                                    0 = selects an AXI IIC receive
                                    1 = selects an AXI IIC transmit
                                    This bit does not control the Read/Write bit that is sent on the bus with
                                    the address. The Read/Write bit that is sent with an address must be the
                                    LSB of the address written into the TX_FIFO. */
#define IIC_CR_TXAK_MASK (1U << 4) /* Transmit Acknowledge Enable. This bit specifies the value driven onto
                                      the sda line during acknowledge cycles for both master and slave receivers.
                                      0 = ACK bit = 0 – acknowledge
                                      1 = ACK bit = 1 – not-acknowledge
                                      Because master receiver indicates the end of data reception by not
                                      acknowledging the last byte of the transfer, this bit is used to end a
                                      master receiver transfer. As a slave, this bit must be set prior to receiving
                                      the byte to signal a not-acknowledge. */
#define IIC_CR_RSTA_MASK (1U << 5) /* Repeated Start. Writing a 1 to this bit generates a repeated START
                                      condition on the bus if the AXI IIC bus interface is the current bus
                                      master. Attempting a repeated START at the wrong time, if the bus is
                                      owned by another master, results in a loss of arbitration. This bit is reset
                                      when the repeated start occurs. This bit must be set prior to writing the
                                      new address to the TX_FIFO or DTR. */
#define IIC_CR_GC_EN_MASK (1U << 6) /* General Call Enable. Setting this bit High allows the AXI IIC to respond
                                       to a general call address.
                                       0 = General Call Disabled
                                       1 = General Call Enabled */

/* This register contains the status of the AXI IIC core interface. This register is read only */
#define IIC_STATUS_REGISTER_OFFSET 0x104

#define IIC_SR_ABGC_MASK (1U << 0) /* Addressed By a General Call. This bit is set to 1 when another
                                      master has issued a general call and the general call enable bit is set to 1, CR(6) = 1. */
#define IIC_SR_AAS_MASK (1U << 1) /* Addressed as Slave. When the address on the IIC bus matches the
                                     slave address in the Address register (ADR), the IIC bus interface
                                     is being addressed as a slave and switches to slave mode. If 10-bit
                                     addressing is selected this device only responds to a 10-bit
                                     address or general call if enabled. This bit is cleared when a stop
                                     condition is detected or a repeated start occurs.
                                     0 = indicates not being addressed as a slave
                                     1 = indicates being addressed as a slave */
#define IIC_SR_BB_MASK (1U << 2) /* Bus Busy. This bit indicates the status of the IIC bus. This bit is set
                                    when a START condition is detected and cleared when a STOP condition is detected.
                                    0 = indicates the bus is idle
                                    1 = indicates the bus is busy */
#define IIC_SR_SRW_MASK (1U << 3) /* Slave Read/Write. When the IIC bus interface has been
                                     addressed as a slave (AAS is set), this bit indicates the value of the
                                     read/write bit sent by the master. This bit is only valid when a
                                     complete transfer has occurred and no other transfers have been initiated.
                                     0 = indicates master writing to slave
                                     1 = indicates master reading from slave */
#define IIC_SR_TX_FIFO_FULL_MASK (1U << 4) /* Transmit FIFO full. This bit is set High when the transmit FIFO is full. */
#define IIC_SR_RX_FIFO_FULL_MASK (1U << 5) /* Receive FIFO full. This bit is set High when the receive FIFO is full.
                                              This bit is set only when all 16 locations in the FIFO are full,
                                              regardless of the compare value field of the RX_FIFO_PIRQ register. */
#define IIC_SR_RX_FIFO_EMPTY_MASK (1U << 6) /* Receive FIFO empty. This is set High when the receive FIFO is empty. */
#define IIC_SR_TX_FIFO_EMPTY_MASK (1U << 7) /* Transmit FIFO empty. This bit is set High when the transmit FIFO is empty.
                                               Note: This bit goes High as soon as the TX FIFO becomes empty. At this
                                                     moment, the last byte of Data might still be in output pipeline or might
                                                     be partially transferred. */

/* This is the keyhole address for the FIFO that contains data to be transmitted on the IIC bus. */
#define IIC_TX_FIFO_OFFSET 0x108

#define IIC_TX_FIFO_DATA_MASK 0xFF /* AXI IIC Transmit Data. If the dynamic stop bit is used and the
                                      AXI IIC is a master receiver, the value is the number of bytes to receive. */
#define IIC_TX_FIFO_START_MASK (1U << 8) /* Start. The dynamic start bit can be used to send a start or
                                            repeated start sequence on the IIC bus. A start sequence is
                                            generated if the MSMS = 0, a repeated start sequence is
                                            generated if the MSMS = 1. */
#define IIC_TX_FIFO_STOP_MASK (1U << 9) /* Stop. The dynamic stop bit can be used to send an IIC stop
                                           sequence on the IIC bus after the last byte has been transmitted or received. */

/* Least significant bit used to identify the I2C read or write when during the I2C slave address to the TX FIFO */
#define IIC_TX_FIFO_READ_OPERATION  1 /* Read operation on the I2C bus */
#define IIC_RX_FIFO_WRITE_OPERATION 0 /* Write operation on the I2C bus */

/* This FIFO contains the data received from the IIC bus. The received IIC data is placed in this
   FIFO after each complete transfer. The RX_FIFO_OCY must be equal to the RX_FIFO_PIRQ
   before throttling occurs. The receive FIFO is read only. Reading this FIFO when it is empty
   results in indeterminate data being read. */
#define IIC_RX_FIFO_OFFSET 0x10C

#define IIC_RX_FIFO_DATA_MASK 0xFF

/* Slave Address Register and Slave 10-Bit Address Register are not defined as are only using the IIC as a master */

/* This field contains the occupancy value for the transmit FIFO. */
#define IIC_TX_FIFO_OCY_OFFSET 0x114

/* This field contains the occupancy value for the receive FIFO. */
#define IIC_RX_FIFO_OCY_OFFSET 0x118

/* This field contains the value which causes the receive FIFO Interrupt to be set. */
#define IIC_RX_FIFO_PIRQ_OFFSET 0x120

/* General Purpose Output Register is defined defined as no General Purpose outputs are connected */

/* The following registers are not defined, as leave the default values configured in the Vivado project:
 * - Timing Parameter TSUSTA Register (TSUSTA)
 * - Timing Parameter TSUSTO Register (TSUSTO)
 * - Timing Parameter THDSTA Register (THDSTA)
 * - Timing Parameter TSUDAT Register (TSUDAT)
 * - Timing Parameter TBUF Register (TBUF)
 * - Timing Parameter THIGH Register (THIGH)
 * - Timing Parameter TLOW Register (TLOW)
 * - Timing Parameter THDDAT Register (THDDAT)
 */

#endif /* XILINX_AXI_IIC_HOST_INTERFACE_H_ */
