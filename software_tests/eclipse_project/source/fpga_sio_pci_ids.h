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
 * a. Loopback two AXI streams.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only).
 * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
 *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
#define FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_LOOPBACK 0x0007

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Loopback two AXI streams.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC - internal sensors and one external input. */
#define FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_LOOPBACK 0x0008

/* The FPGA which uses the DMA/Bridge Subsystem to:
 * a. Loopback two AXI streams.
 * b. Access a Quad SPI connected to the FPGA configuration flash.
 * c. Access the XADC (internal sensors only) */
#define FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_LOOPBACK 0x0009

#endif /* SOURCE_FPGA_SIO_PCI_IDS_H_ */
