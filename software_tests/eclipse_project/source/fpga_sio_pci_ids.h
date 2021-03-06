/*
 * @file fpga_sio_pci_ids.h
 * @date Sep 15, 2021
 * @author Chester Gillon
 * @brief PCI IDs for the different FPGAs tested
 */

#ifndef SOURCE_FPGA_SIO_PCI_IDS_H_
#define SOURCE_FPGA_SIO_PCI_IDS_H_

#include <linux/pci.h>

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

#endif /* SOURCE_FPGA_SIO_PCI_IDS_H_ */
