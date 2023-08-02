/*
 * @file xilinx_quad_spi_host_interface.h
 * @date 8 Jul 2023
 * @author Chester Gillon
 * @brief Defines the interface to the Xilinx "AXI Quad Serial Peripheral Interface (SPI) core", from the point of view of the host.
 * @details
 *  This is subset of register definitions used for user space access via VFIO.
 *  Details taken from https://www.xilinx.com/support/documentation/ip_documentation/axi_quad_spi/v3_2/pg153-axi-quad-spi.pdf
 */

#ifndef XILINX_QUAD_SPI_HOST_INTERFACE_H_
#define XILINX_QUAD_SPI_HOST_INTERFACE_H_

/* The Software Reset Register (SRR) permits resetting the core independently of other cores in the system.
 * The only allowed operation on this register is a write of XSPI_SOFTWARE_RESET_VALUE, which resets the AXI Quad SPI core. */
#define XSPI_SOFTWARE_RESET_OFFSET 0x40
#define XSPI_SOFTWARE_RESET_VALUE 0x0000000a


/* The SPI Control Register (SPICR) allows programmer control over various aspects of the AXI Quad SPI core. */
#define XSPI_CONTROL_OFFSET 0x60

/* LSB first:

   This bit selects LSB first data transfer format.
   The default transfer format is MSB first.
   When set to:
   0 = MSB first transfer format.
   1 = LSB first transfer format.

Note:   In Dual/Quad SPI mode, only the MSB first mode of the core is allowed. */
#define XSPI_CONTROL_LSB_FIRST_MASK (1u << 9)

/* Master transaction inhibit:

   This bit inhibits master transactions.
   This bit has no effect on slave operation.
   When set to:
   0 = Master transactions enabled.
   1 = Master transactions disabled.

Note:   This bit immediately inhibits the transaction. Setting this bit while transfer is in progress would result in
        unpredictable outcome. */
#define XSPI_CONTROL_MASTER_TRANSACTION_INHIBIT_MASK (1u << 8)

/* Manual slave select assertion enable:

   This bit forces the data in the slave select register to be asserted on the slave select output anytime the device is
   configured as a master and the device is enabled (SPE asserted).
   This bit has no effect on slave operation.
   When set to:
   0 = Slave select output asserted by master core logic.
   1 = Slave select output follows data in slave select register.

Note:   The manual slave assertion mode is supported in standard SPI mode only. */
#define XSPI_CONTROL_MANUAL_SLAVE_SELECT_ASSERTION_ENABLE_MASK (1u << 7)

/* Receive FIFO reset:

   When written to 1, this bit forces a reset of the receive FIFO to the empty condition.
   One AXI clock cycle after reset, this bit is again set to 0.
   When set to:
   0 = Receive FIFO normal operation.
   1 = Reset receive FIFO pointer. */
#define XSPI_CONTROL_RX_FIFO_RESET_MASK (1u << 6)

/* Transmit FIFO reset:

   When written to 1, this bit forces a reset of the transmit FIFO to the empty condition.
   One AXI clock cycle after reset, this bit is again set to 0.
   When set to:
   0 = Transmit FIFO normal operation.
   1 = Reset transmit FIFO pointer. */
#define XSPI_CONTROL_TX_FIFO_RESET_MASK (1u << 5)

/* Clock phase:
   Setting this bit selects one of two fundamentally different transfer formats. */
#define XSPI_CONTROL_CPHA_MASK (1u << 4)

/* Clock polarity

   Setting this bit defines clock polarity.
   When set to:
   0 = Active-High clock; SCK idles Low.
   1 = Active-Low clock; SCK idles High. */
#define XSPI_CONTROL_CPOL_MASK (1u << 3)

/* Master (SPI master mode):

   Setting this bit configures the SPI device as a master or a slave.
   When set to:
   0 = Slave configuration.
   1 = Master configuration.

Note:   In dual/quad SPI mode only the master mode of the core is allowed.
Note:   Standard Slave mode is not supported for SCK ratio = 2. */
#define XSPI_CONTROL_MASTER_MASK (1u << 2)

/* SPI system enable:

   Setting this bit to 1 enables the SPI devices as noted here.
   When set to:
   0 = SPI system disabled. Both master and slave outputs are in 3-state and slave inputs are ignored.
   1 = SPI system enabled. Master outputs active (for example, IO0 (MOSI) and SCK in idle state) and
       slave outputs become active if SS becomes asserted. The master starts transferring when transmit data is available. */
#define XSPI_CONTROL_SPE_MASK (1u << 1)

/* Local loopback mode:

   Enables local loopback operation and is functional only in standard SPI master mode.
   When set to:
   0 = Normal operation.
   1 = Loopback mode. The transmitter output is internally connected to the receiver input.
       The receiver and transmitter operate normally, except that received data (from remote slave) is ignored. */
#define XSPI_CONTROL_LOOP_MASK (1u << 0)


/* The SPI Status Register (SPISR) is a read-only register that provides the status of some aspects of the AXI Quad SPI core
 * to the programmer. */
#define XSPI_STATUS_OFFSET 0x64

/* Command error flag.

   When set to:
   0 = Default.
   1 = When the core is configured in dual/quad SPI mode and the first entry in the SPI DTR FIFO (after reset) do not match with
      the supported command list for the particular memory, this bit is set.

Note:   Command error is only applicable when the core is configured either in dual or quad mode in legacy or
        enhanced mode AXI4 interface. */
#define XSPI_STATUS_COMMAND_ERROR_MASK (1u << 10)

/* Loopback error flag.

   When set to:
   0= Default. The loopback bit in the control register is at default state.
   1 = When the SPI command, address, and data bits are set to be transferred in other than standard SPI protocol mode and this
       bit is set in control register (SPICR).

Note:   Loopback is only allowed when the core is configured in standard mode. Other modes setting of the bit causes an error
        and the interrupt bit is set in legacy or enhanced mode AXI4 interface. */
#define XSPI_STATUS_LOOPBACK_ERROR_MASK (1u << 9)

/* MSB error flag.

   When set to:
   0= Default.
   1 = This bit is set when the core is configured to transfer the SPI transactions in either dual or quad SPI mode and
       LSB first bit is set in the control register (SPICR).

Note:   In dual/quad SPI mode, only the MSB first mode of the core is allowed. MSB error flag is only applicable
        when the core is configured either in dual or quad mode in legacy or enhanced mode AXI4 interface. */
#define XSPI_STATUS_MSB_ERROR_MASK (1u << 8)

/* Slave mode error flag.

   When set to:
   1 = This bit is set when the core is configured with dual or quad SPI mode and the master is set to 0 in the
       control register (SPICR).
   0 = Master mode is set in the control register (SPICR).

Note:   Quad SPI mode, only the master mode of the core is allowed. Slave mode error flag is only applicable when
        the core is configured either in dual or quad mode in legacy or enhanced AXI4 mode interface. */
#define XSPI_STATUS_SLAVE_MODE_ERROR_MASK (1u << 7)

/* CPOL_CPHA_Error flag.

   When set to:
   0 = Default.
   1 = The CPOL and CPHA are set to 01 or 10.

   When the SPI memory is chosen as either Winbond, Micron or Spansion or Macronix and CPOL and CPHA are configured as 01 or 10,
   this bit is set.

   These memories support CPOL=CPHA mode in 00 or in 11 mode. CPOL_CPHA_Error flag is only applicable when the core is configured
   either in dual or quad mode in legacy or enhanced mode AXI4 interface. */
#define XSPI_STATUS_CPOL_CPHA_ERROR_MASK (1u << 6)

/* Slave_Mode_Select flag.

   This flag is asserted when the core is configured in slave mode. Slave_Mode_Select is activated as soon as the master SPI core
   asserts the chip select pin for the core.
   1 = Default in standard mode.
   0 = Asserted when core configured in slave mode and selected by external SPI master. */
#define XSPI_STATUS_SLAVE_MODE_SELECT_MASK (1u << 5)

/* Mode-fault error flag.

   This flag is set if the SS signal goes active while the SPI device is configured as a master.
   MODF is automatically cleared by reading the SPISR. A Low-to-High MODF transition generates a single-cycle strobe interrupt.
   0 = No error.
   1 = Error condition detected. */
#define XSPI_STATUS_MODF_MASK (1u << 4)

/* Transmit full.

   When a transmit FIFO exists, this bit is set High when the transmit FIFO is full.

Note:   When FIFOs do not exist, this bit is set High when an AXI write to the transmit register has been made
        (this option is available only in standard SPI mode). This bit is cleared when the SPI transfer is completed. */
#define XSPI_STATUS_TX_FULL_MASK (1u << 3)

/* Transmit empty.

   When a transmit FIFO exists, this bit is set to High when the transmit FIFO is empty.
   This bit goes High as soon as the TX FIFO becomes empty.
   While this bit is High, the last byte of the data that is to be transmitted would still be in the pipeline.
   The occupancy of the FIFO is decremented with the completion of each SPI transfer.

Note:   When FIFOs do not exist, this bit is set with the completion of an SPI transfer
        (this option is available only in standard SPI mode).
        Either with or without FIFOs, this bit is cleared on an AXI write to the FIFO or transmit register.
        For Dual/Quad SPI mode, the FIFO is always present in the core. */
#define XSPI_STATUS_TX_EMPTY_MASK (1u << 2)

/* Receive full.

   When a receive FIFO exists, this bit is set High when the receive FIFO is full.
   The occupancy of the FIFO is incremented with the completion of each SPI transaction.

Note:   When FIFOs do not exist, this bit is set High when an SPI transfer has completed
        (this option is available only in standard SPI mode). Rx_Empty and Rx_Full are complements in this case. */
#define XSPI_STATUS_RX_FULL_MASK (1u << 1)

/* Receive Empty.

   When a receive FIFO exists, this bit is set High when the receive FIFO is empty.
   The occupancy of the FIFO is decremented with each FIFO read operation.

Note:   When FIFOs do not exist, this bit is set High when the receive register has been read
        (this option is available only in standard SPI mode). This bit is cleared at the end of a successful SPI transfer.
        For dual/quad SPI mode, the FIFO is always present in the core. */
#define XSPI_STATUS_RX_EMPTY_MASK (1u << 0)

/* Mask for the different error bits */
#define XSPI_STATUS_ERRORS_MASK (XSPI_STATUS_COMMAND_ERROR_MASK    | \
                                 XSPI_STATUS_LOOPBACK_ERROR_MASK   | \
                                 XSPI_STATUS_MSB_ERROR_MASK        | \
                                 XSPI_STATUS_SLAVE_MODE_ERROR_MASK | \
                                 XSPI_STATUS_CPOL_CPHA_ERROR_MASK  | \
                                 XSPI_STATUS_MODF_MASK)

/* Mask for the different FIFO status bits */
#define XSPI_STATUS_FIFOS_MASK (XSPI_STATUS_TX_FULL_MASK | XSPI_STATUS_TX_EMPTY_MASK| \
                                XSPI_STATUS_RX_FULL_MASK | XSPI_STATUS_RX_EMPTY_MASK)


/* N-bit SPI transmit data. N can be 8, 16 or 32.

   N = 8 when the Transfer Width parameter is 8.
   N = 16 when the Transfer Width parameter is 16.
   N = 32 when the Transfer Width parameter is 32. */
#define XSPI_DATA_TRANSMIT_OFFSET 0x68


/* N-bit SPI receive data. N can be 8, 16 or 32.

   N = 8 when the Transfer Width parameter is 8.
   N = 16 when the Transfer Width parameter is 16.
   N = 32 when the Transfer Width parameter is 32. */
#define XSPI_DATA_RECEIVE_OFFSET 0x6C


/* Active-Low, one-hot encoded slave select vector of length N-bits. N must be the data bus width (32-bit).

   The slaves are numbered right to left starting at zero with the LSB.
   The slave numbers correspond to the indexes of signal SS. */
#define XSPI_SLAVE_SELECT_OFFSET 0x70


/* The binary value plus 1 yields the occupancy. */
#define XSPI_TRANSMIT_FIFO_OCCUPANCY_OFFSET 0x74


/* The binary value plus 1 yields the occupancy. */
#define XSPI_RECEIVE_FIFO_OCCUPANCY_OFFSET 0x78


/* Haven't defined the interrupt registers as intend to use poll-mode only.
 * There is a "Data receive register/FIFO overrun" which can be indicated only an interrupt register which might be useful
 * for debugging / diagnostics. */


/* Haven't defined the XIP registers as only intended to access FPGA configuration flash */


/* The subset of Quad SPI memory opcodes which are supported by the Quad SPI core and the used Quad SPI flash devices.
 * Qualification with a manufacturer name means the opcode can vary between manufacturers. */
#define XSPI_OPCODE_READ_STATUS_REGISTER                           0x05
#define XSPI_OPCODE_SUBSECTOR_ERASE_4_BYTE_ADDRESS                 0x21
#define XSPI_OPCODE_SPANSION_READ_CONFIGURATION_REGISTER           0x35
#define XSPI_OPCODE_READ_SERIAL_FLASH_DISCOVERABLE_PARAMETERS      0x5A
#define XSPI_OPCODE_READ_VOLATILE_CONFIGURATION_REGISTER           0x85
#define XSPI_OPCODE_READ_IDENTIFICATION_ID                         0x9F
#define XSPI_OPCODE_MICRON_READ_NONVOLATILE_CONFIGURATION_REGISTER 0xB5
#define XSPI_OPCODE_SECTOR_ERASE_4_BYTE_ADDRESS                    0xDC
#define XSPI_OPCODE_QUAD_IO_READ_4_BYTE_ADDRESS                    0xEC
#define XSPI_OPCODE_SPANSION_MODE_BIT_RESET                        0xFF

#endif /* XILINX_QUAD_SPI_HOST_INTERFACE_H_ */
