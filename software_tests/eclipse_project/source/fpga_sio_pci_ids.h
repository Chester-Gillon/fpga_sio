/*
 * @file fpga_sio_pci_ids.h
 * @date Sep 15, 2021
 * @author Chester Gillon
 * @brief PCI IDs for the different FPGAs tested
 */

#ifndef SOURCE_FPGA_SIO_PCI_IDS_H_
#define SOURCE_FPGA_SIO_PCI_IDS_H_

#include <pci/pci.h>   /* To avoid duplicate definitions when used with vfio_access.h */
#include <linux/pci.h> /* For PCI_STD_NUM_BARS */

/* Only added to user API pci_regs.h in Linux Kernel v5.5 */
#ifndef PCI_STD_NUM_BARS
#define PCI_STD_NUM_BARS    6   /* Number of standard BARs */
#endif

/* The Vendor ID for the FPGA is left as that for Xilinx.
 * The device ID is left at the default selected by Vivado, which can vary according to the PCIe parameters such as the the
 * maximum link speed. */
#define FPGA_SIO_VENDOR_ID 0x10ee

/* The subsystem vendor ID used for the FPGAs.
 * There is no official reserved / test vendor IDs so just picked one which wasn't currently allocated. */
#define FPGA_SIO_SUBVENDOR_ID 0x0002

/* The FPGA which just exposes memory mapped block RAM in multiple BARs */
#define FPGA_SIO_SUBDEVICE_ID_MEMMAPPED_BLKRAM 0x0001

/* The FPGA which contains a single BAR which contains a AXI IIC Bus Interface PG090 and AXI GPIO PG144 */
#define FPGA_SIO_SUBDEVICE_ID_I2C_PROBE 0x0002

/* The FPGA which uses the DMA/Bridge Subsystem to access block RAM */
#define FPGA_SIO_SUBDEVICE_ID_DMA_BLKRAM 0x0003

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access 1GB of DDR3 memory.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_DDR3 0x0004

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access 8GB of DDR3 memory.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only).
 * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
 *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
#define FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_DDR3 0x0005

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access 1GB of DDR3 memory.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC - internal sensors and one external input. */
#define FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_DDR3 0x0006

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Loopback two AXI streams via a AXI4-Stream Switch with register based routing.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only).
 * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
 *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
#define FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_LOOPBACK 0x0007

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Loopback two AXI streams via a AXI4-Stream Switch with register based routing.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC - internal sensors and one external input. */
#define FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_LOOPBACK 0x0008

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Loopback two AXI streams via a AXI4-Stream Switch with register based routing.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_LOOPBACK 0x0009

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Loopback four AXI streams via a AXI4-Stream Switch with register based routing.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK 0x000A

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access 2MB of URAM.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_RAM 0x000B

/* The FPGA which uses the Queue DMA Subsystem for PCI Express to:
 * a. Implement 4 physical functions.
 * b. Access 2MB of internal memory connected to the QDMA. The QDMA registers are in BAR 0 on all physical functions.
 * c. Each of the 4 physical functions is assigned a different subdevice ID and can access different peripherals.
 *    The assigned peripherals on BAR 2 are
 *    PF0 : Quad SPI connected to the FPGA configuration flash
 *    PF1 : SYSMON (internal sensors only)
 *    PF2 : GPIO input to read user access FPGA build times
 *    PF3 : 16550 UART, which has internal loopback
 */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_QUAD_SPI    0x000C /* PF0 */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_SYSMON      0x000D /* PF1 */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_USER_ACCESS 0x000E /* PF2 */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_UART        0x000F /* PF3 */

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Four AXI streams with fixed data to try and maximum DMA throughput:
 *    - C2H have a fixed data value always ready.
 *    - H2C just asserts TREADY and doesn't do anything with the data.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_STREAM_FIXED_DATA 0x0010

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Two AXI streams with fixed data to try and maximum DMA throughput:
 *    - C2H have a fixed data value always ready.
 *    - H2C just asserts TREADY and doesn't do anything with the data.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only).
 * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
 *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
#define FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_FIXED_DATA 0x0011

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Two AXI streams with fixed data to try and maximum DMA throughput:
 *    - C2H have a fixed data value always ready.
 *    - H2C just asserts TREADY and doesn't do anything with the data.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC - internal sensors and one external input. */
#define FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_FIXED_DATA 0x0012

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Two AXI streams with fixed data to try and maximum DMA throughput:
 *    - C2H have a fixed data value always ready.
 *    - H2C just asserts TREADY and doesn't do anything with the data.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_FIXED_DATA 0x0013

/* The FPGA which uses IBERT for testing the QSFP. The IBERT core is accessed
 * over JTAG, rather via PCIe.
 *
 * There is a DMA Bridge with the following memory mapped peripherals:
 * a. Management of each QSFP port via:
 *    - AXI IIC Bus Interface
 *    - GPIO for the discrete signals
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_IBERT 0x0014

/* The FPGA which uses the AXI Memory Mapped to PCI Express block to access:
 * a. Access the AXI DMA block to transfer between 8GB of DDR3 memory and stream sources/sinks.
 * b. Access the XADC (internal sensors only). */
#define FPGA_SIO_SUBDEVICE_ID_TEF1001_DDR3_THROUGHPUT 0x0015

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access four AXI streams which perform a CRC64 calculation.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64 0x0016

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access two AXI streams which perform a CRC64 calculation.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only).
 * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
 *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
#define FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_CRC64 0x0017

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access two AXI streams which perform a CRC64 calculation.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_CRC64 0x0018

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access two AXI streams which perform a CRC64 calculation.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC - internal sensors and one external input. */
#define FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_CRC64 0x0019

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access four AXI streams which perform a CRC64 calculation.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_AS02MC04_DMA_STREAM_CRC64 0x001A

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Contain only a AXI peripheral to read the user access timestamp.
 * b. Have different revisions to investigating to enumerate for x8 width. */
#define FPGA_SIO_SUBDEVICE_ID_AS02MC04_ENUM 0x001B

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Contain only a AXI peripheral to read the user access timestamp.
 * b. Have different revisions to investigating to enumerate for x16 width and bifurcation. */
#define FPGA_SIO_SUBDEVICE_ID_U200_ENUM 0x001C

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Contain only a AXI peripheral to read the user access timestamp.
 * b. Have a 100G Ethernet simplex transmitter, including control and statistics registers.
 * c. The CMS subsystem for management of the QSFP ports.
 * d. Access the SYSMON on all 3 SLRs (internal sensors only).
 * e. Access a Quad SPI connected to the FPGA configuration flash. */
#define FPGA_SIO_SUBDEVICE_ID_U200_100G_ETHER_SIMPLEX_TX 0x001D


/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access four AXI streams which perform a CRC64 calculation.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the SYSMON on all 3 SLRs (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_U200_DMA_STREAM_CRC64 0x001E

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Access the CMS subsystem for management of the QSFP ports.
 * b. Access the SYSMON on all 3 SLRs (internal sensors only).
 * c. Access GPIOs to control Refclk frequency selection.
 * d. Access a Quad SPI connected to the FPGA configuration flash. */
#define FPGA_SIO_SUBDEVICE_ID_U200_IBERT_100G_ETHER 0x001F

#endif /* SOURCE_FPGA_SIO_PCI_IDS_H_ */
