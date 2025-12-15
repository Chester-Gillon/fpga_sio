/*
 * @file identify_pcie_fpga_design.c
 * @date 10 Sep 2023
 * @author Chester Gillon
 * @brief Implements a mechanism for identifying different FPGA designs which use a PCIe interface
 * @details
 *   This was written to provide a mechanism of locating IP which is used in multiple designs.
 *   The approach is to use the PCIe vendor and/or subvendor to identify the the design, and possibly probe some
 *   other information.
 *
 *   Some thoughts on how to have a more generic way of identifying IP:
 *   a. While PCIe has "Vital Product Data" (VPD), Xilinx series devices don't seem to support it.
 *   b. Could potentially have some "ROM" located at the lowest address of the first BAR.
 *      E.g. Like the ARM ROM Table https://developer.arm.com/documentation/102585/0000/What-is-a-ROM-Table-
 */

#include "identify_pcie_fpga_design.h"
#include "fpga_sio_pci_ids.h"

#include <string.h>
#include <stdio.h>


/* Lookup table to give the name for each FPGA design, with the name of the board in brackets if not part of the design name. */
const char *const fpga_design_names[FPGA_DESIGN_ARRAY_SIZE] =
{
    [FPGA_DESIGN_DMA_BLKRAM] = "dma_blkram (TEF1001)",
    [FPGA_DESIGN_I2C_PROBE] = "i2c_probe (TEF1001) or TOSING_160T_quad_spi",
    [FPGA_DESIGN_TOSING_160T_DMA_DDR3] = "TOSING_160T_dma_ddr3",
    [FPGA_DESIGN_LITEFURY_PROJECT0] = "Litefury Project-0",
    [FPGA_DESIGN_NITEFURY_PROJECT0] = "Nitefury Project-0",
    [FPGA_DESIGN_TEF1001_DMA_DDR3] = "TEF1001_dma_ddr3",
    [FPGA_DESIGN_NITEFURY_DMA_DDR3] = "NiteFury_dma_ddr3",
    [FPGA_DESIGN_TEF1001_DMA_STREAM_LOOPBACK] = "TEF1001_dma_stream_loopback",
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_LOOPBACK] = "NiteFury_dma_stream_loopback",
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_LOOPBACK] = "TOSING_160T_dma_stream_loopback",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK] = "XCKU5P_DUAL_QSFP_dma_stream_loopback",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_RAM] = "XCKU5P_DUAL_QSFP_dma_ram",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_QUAD_SPI] = "XCKU5P_DUAL_QSFP_qdma_ram (quad SPI)",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_SYSMON] = "XCKU5P_DUAL_QSFP_qdma_ram (SYSMON)",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_USER_ACCESS] = "XCKU5P_DUAL_QSFP_qdma_ram (user access)",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_UART] = "XCKU5P_DUAL_QSFP_qdma_ram (UART)",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_FIXED_DATA] = "XCKU5P_DUAL_QSFP_dma_stream_fixed_data",
    [FPGA_DESIGN_TEF1001_DMA_STREAM_FIXED_DATA] = "TEF1001_dma_stream_fixed_data",
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_FIXED_DATA] = "NiteFury_dma_stream_fixed_data",
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_FIXED_DATA] = "TOSING_160T_dma_stream_fixed_data",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_IBERT] = "XCKU5P_DUAL_QSFP_ibert",
    [FPGA_DESIGN_TEF1001_DDR3_THROUGHPUT] = "TEF1001_ddr3_throughput",
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64] = "XCKU5P_DUAL_QSFP_dma_stream_crc64",
    [FPGA_DESIGN_TEF1001_DMA_STREAM_CRC64] = "TEF1001_dma_stream_crc64",
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_CRC64] = "TOSING_160T_dma_stream_crc64",
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_CRC64] = "NiteFury_dma_stream_crc64",
    [FPGA_DESIGN_AS02MC04_DMA_STREAM_CRC64] = "AS02MC04_dma_stream_crc64",
    [FPGA_DESIGN_AS02MC04_ENUM] = "AS02MC04_enum",
    [FPGA_DESIGN_U200_ENUM] = "U200_enum",
    [FPGA_DESIGN_U200_100G_ETHER_SIMPLEX_TX] = "U200_100G_ether_simplex_tx",
    [FPGA_DESIGN_U200_DMA_STREAM_CRC64] = "U200_dma_stream_crc64",
    [FPGA_DESIGN_U200_IBERT_100G_ETHER] = "U200_ibert_100G_ether",
    [FPGA_DESIGN_OPEN_NIC] = "open-nic",
    [FPGA_DESIGN_VD100_ENUM] = "VD100_enum"
};


/* The PCI filters used for each design */
static const vfio_pci_device_identity_filter_t fpga_design_pci_filters[FPGA_DESIGN_ARRAY_SIZE] =
{
    [FPGA_DESIGN_DMA_BLKRAM] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_DMA_BLKRAM,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_I2C_PROBE] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_I2C_PROBE,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_TOSING_160T_DMA_DDR3] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_DDR3,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    /* Same identity used for both Litefury and Nitefury, with a GPIO read to identify which design */
    [FPGA_DESIGN_LITEFURY_PROJECT0 ... FPGA_DESIGN_NITEFURY_PROJECT0] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = 0x7011,
        .subsystem_vendor_id = 0,
        .subsystem_device_id = 0,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TEF1001_DMA_DDR3] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_DDR3,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_NITEFURY_DMA_DDR3] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_DDR3,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TEF1001_DMA_STREAM_LOOPBACK] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_LOOPBACK,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_LOOPBACK] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_LOOPBACK,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_LOOPBACK] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_LOOPBACK,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_RAM] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_RAM,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_QUAD_SPI] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_QUAD_SPI,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_SYSMON] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_SYSMON,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_USER_ACCESS] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_USER_ACCESS,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_UART] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_QDMA_RAM_UART,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_FIXED_DATA] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_STREAM_FIXED_DATA,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TEF1001_DMA_STREAM_FIXED_DATA] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_FIXED_DATA,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_FIXED_DATA] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_FIXED_DATA,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_FIXED_DATA] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_FIXED_DATA,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_IBERT] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_IBERT,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_TEF1001_DDR3_THROUGHPUT] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TEF1001_DDR3_THROUGHPUT,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TEF1001_DMA_STREAM_CRC64] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TEF1001_DMA_STREAM_CRC64,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_CRC64] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TOSING_160T_DMA_STREAM_CRC64,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_CRC64] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_NITEFURY_DMA_STREAM_CRC64,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_AS02MC04_DMA_STREAM_CRC64] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_AS02MC04_DMA_STREAM_CRC64,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_AS02MC04_ENUM] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_AS02MC04_ENUM,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_U200_ENUM] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_U200_ENUM,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_U200_100G_ETHER_SIMPLEX_TX] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_U200_100G_ETHER_SIMPLEX_TX,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_U200_DMA_STREAM_CRC64] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_U200_DMA_STREAM_CRC64,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_U200_IBERT_100G_ETHER] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_U200_IBERT_100G_ETHER,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    },
    [FPGA_DESIGN_OPEN_NIC] =
    {
        /* @todo Uses the ID from looking at the QDMA IP settings after building https://github.com/Xilinx/open-nic-shell
         *       for a Alveo U200.
         *       The https://github.com/Xilinx/open-nic-shell/blob/main/src/qdma_subsystem/vivado_ip/qdma_no_sriov_au200.tcl
         *       source file doesn't specify the IDs, so they are probably the QDMA defaults.
         *       I.e. could clash with other QDMA designs. */
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_VENDOR_ID,
        .subsystem_device_id = 0x0007,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    },
    [FPGA_DESIGN_VD100_ENUM] =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
        .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_VD100_ENUM,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A64
    }
};


/* For the designs which implement a CRC64 stream, the size of tdata width in bytes.
 * The value depends upon the PCIe speed and width of the DMA/Bridge Subsystem, which in turn sets the available stream width.
 *
 * The CRC64 operation:
 * a. Means the size of each H2C packet is fixed as 8 bytes.
 * b. Is performed in parallel across the width of the C2H stream, without taking account of tkeep on the end of packet.
 *    Therefore, to get the expected CRC64 result all HC2 packets have to be a multiple of this value.
 */
const uint32_t crc64_stream_tdata_width_bytes[FPGA_DESIGN_ARRAY_SIZE] =
{
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64] = 32,
    [FPGA_DESIGN_TEF1001_DMA_STREAM_CRC64         ] = 16,
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_CRC64     ] = 16,
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_CRC64        ] = 16,
    [FPGA_DESIGN_AS02MC04_DMA_STREAM_CRC64        ] = 32,
    [FPGA_DESIGN_U200_DMA_STREAM_CRC64            ] = 64

};


/**
 * @brief Identify if a design is a FPGA_DESIGN_LITEFURY_PROJECT0 or FPGA_DESIGN_NITEFURY_PROJECT0
 * @details Both designs use the same PCI identities, and are differentiated by reading a GPIO register in the design
 * @param[in/out] vfio_device The VFIO device for the candidate design to be probed
 * @param[out] candidate_design_id Set to the identified design
 * @param[on/out] candidate_design Used to store the information about the identified design
 * @return Returns true if have identified the design based upon the GPIO register
 */
static bool identify_fury_project0 (vfio_device_t *const vfio_device, fpga_design_id_t *const candidate_design_id,
                                    fpga_design_t *const candidate_design)
{
    bool design_identified = false;
    uint8_t *const gpio_0_regs = map_vfio_registers_block (vfio_device, FURY_PROJECT0_AXI_PERIPHERALS_BAR,
            FURY_PROJECT0_GPIO_0_BASE_OFFSET, FURY_PROJECT0_PERIPHERAL_FRAME_SIZE);

    if (gpio_0_regs != NULL)
    {
        /* pid string is a constant value fed to the GPIO input value */
        const uint32_t pid_integer = read_reg32 (gpio_0_regs, 0x0);
        const char *const pid_bytes = (const char *) &pid_integer;
        char pid_string[4];

        /* Need to reverse the bytes to get the pid string */
        pid_string[0] = pid_bytes[3];
        pid_string[1] = pid_bytes[2];
        pid_string[2] = pid_bytes[1];
        pid_string[3] = pid_bytes[0];

        /* Look for the encoded pid string to identify the LiteFury or NiteFury board.
         * The two boards have:
         * a. Different DDR3 sizes.
         * b. Different FPGA devices. However, the type of device is not available to this library. */
        if (strncmp (pid_string, "LITE", 4) == 0)
        {
            *candidate_design_id = FPGA_DESIGN_LITEFURY_PROJECT0;
            candidate_design->dma_bridge_memory_size_bytes = 512UL * 1024 * 1024;
            design_identified = true;
        }
        else if (strncmp (pid_string, "NITE", 4) == 0)
        {
            *candidate_design_id = FPGA_DESIGN_NITEFURY_PROJECT0;
            candidate_design->dma_bridge_memory_size_bytes = 1024UL * 1024 * 1024;
            design_identified = true;
        }

        if (design_identified)
        {
            /* board_version is a constant value fed to the GPIO2 input value */
            candidate_design->board_version = read_reg32 (gpio_0_regs, 0x8);

            /* Size of the DMA bridge memory has been set above, as depends upon the design */
            candidate_design->dma_bridge_present = true;
            candidate_design->dma_bridge_bar = FURY_PROJECT0_DMA_BRIDGE_BAR;

            candidate_design->quad_spi_regs =
                    map_vfio_registers_block (vfio_device, FURY_PROJECT0_AXI_PERIPHERALS_BAR,
                            FURY_PROJECT0_QUAD_SPI_BASE_OFFSET, FURY_PROJECT0_PERIPHERAL_FRAME_SIZE);
            candidate_design->xadc_regs =
                    map_vfio_registers_block (vfio_device, FURY_PROJECT0_AXI_PERIPHERALS_BAR,
                            FURY_PROJECT0_XADC_WIZ_BASE_OFFSET, FURY_PROJECT0_PERIPHERAL_FRAME_SIZE);
        }
    }

    return design_identified;
}


/**
 * @brief Identify the PCIe FPGA designs known to the library, opening them using VFIO
 * @param[out] designs Contains the identified designs
 */
void identify_pcie_fpga_designs (fpga_designs_t *const designs)
{
    fpga_design_id_t candidate_design_id;
    bool design_identified;

    /* Open all VFIO devices potentially matching the designs */
    memset (designs, 0, sizeof (*designs));
    open_vfio_devices_matching_filter (&designs->vfio_devices, FPGA_DESIGN_ARRAY_SIZE, fpga_design_pci_filters);

    designs->num_identified_designs = 0;
    for (uint32_t device_index = 0; device_index < designs->vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &designs->vfio_devices.devices[device_index];
        fpga_design_t *const candidate_design = &designs->designs[designs->num_identified_designs];

        design_identified = false;
        candidate_design_id = 0;
        while (!design_identified && (candidate_design_id < FPGA_DESIGN_ARRAY_SIZE))
        {
            if (vfio_device_pci_filter_match (vfio_device, &fpga_design_pci_filters[candidate_design_id]))
            {
                switch (candidate_design_id)
                {
                case FPGA_DESIGN_DMA_BLKRAM:
                    {
                        /* The total amount of BLKRAM addressable by DMA. Sizes set to maximise BLKRAM usage in FPGA */
                        const size_t blkram_0_size_bytes = 1024 * 1024;
                        const size_t blkram_1_size_bytes =  128 * 1024;

                        candidate_design->dma_bridge_present = true;
                        candidate_design->dma_bridge_bar = 0; /* Since the PCIe to AXI Lite Master Interface isn't used */
                        candidate_design->dma_bridge_memory_size_bytes = blkram_0_size_bytes + blkram_1_size_bytes;
                        design_identified = true;
                    }
                    break;

                case FPGA_DESIGN_I2C_PROBE:
                    {
                        const uint32_t bar_index = 0;
                        const size_t iic_base_offset      = 0x0000;
                        const size_t iic_frame_size       = 0x1000;
                        const size_t gpio_base_offset     = 0x1000;
                        const size_t gpio_frame_size      = 0x1000;
                        const size_t quad_spi_base_offset = 0x2000;
                        const size_t quad_spi_frame_size  = 0x1000;
                        const size_t xadc_base_offset     = 0x3000;
                        const size_t xadc_frame_size      = 0x1000;

                        candidate_design->iic_regs =
                                map_vfio_registers_block (vfio_device, bar_index, iic_base_offset, iic_frame_size);
                        candidate_design->bit_banged_i2c_gpio_regs =
                                map_vfio_registers_block (vfio_device, bar_index, gpio_base_offset, gpio_frame_size);
                        candidate_design->quad_spi_regs =
                                map_vfio_registers_block (vfio_device, bar_index, quad_spi_base_offset, quad_spi_frame_size);
                        candidate_design->xadc_regs =
                                map_vfio_registers_block (vfio_device, bar_index, xadc_base_offset, xadc_frame_size);
                        design_identified = true;
                    }
                    break;

                case FPGA_DESIGN_TOSING_160T_DMA_DDR3:
                    {
                        const uint32_t peripherals_bar_index = 0;
                        const uint32_t dma_bridge_bar_index = 2;
                        const size_t quad_spi_base_offset    = 0x0000;
                        const size_t quad_spi_frame_size     = 0x1000;
                        const size_t xadc_base_offset        = 0x1000;
                        const size_t xadc_frame_size         = 0x1000;
                        const size_t user_access_base_offset = 0x2000;
                        const size_t user_access_frame_size  = 0x1000;

                        candidate_design->dma_bridge_present = true;
                        candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                        candidate_design->dma_bridge_memory_size_bytes = 1024UL * 1024 * 1024;
                        candidate_design->quad_spi_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        quad_spi_base_offset, quad_spi_frame_size);
                        candidate_design->xadc_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                        candidate_design->user_access =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        user_access_base_offset, user_access_frame_size);
                        design_identified = true;
                    }
                    break;

                case FPGA_DESIGN_LITEFURY_PROJECT0:
                case FPGA_DESIGN_NITEFURY_PROJECT0:
                    design_identified = identify_fury_project0 (vfio_device, &candidate_design_id, candidate_design);
                    break;

                case FPGA_DESIGN_TEF1001_DMA_DDR3:
                    {
                        const uint32_t peripherals_bar_index = 0;
                        const uint32_t dma_bridge_bar_index = 2;
                        const size_t iic_base_offset         = 0x0000;
                        const size_t iic_frame_size          = 0x1000;
                        const size_t gpio_base_offset        = 0x1000;
                        const size_t gpio_frame_size         = 0x1000;
                        const size_t quad_spi_base_offset    = 0x2000;
                        const size_t quad_spi_frame_size     = 0x1000;
                        const size_t xadc_base_offset        = 0x3000;
                        const size_t xadc_frame_size         = 0x1000;
                        const size_t user_access_base_offset = 0x4000;
                        const size_t user_access_frame_size  = 0x1000;

                        candidate_design->dma_bridge_present = true;
                        candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                        candidate_design->dma_bridge_memory_size_bytes = 8192UL * 1024 * 1024;

                        candidate_design->iic_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, iic_base_offset, iic_frame_size);
                        candidate_design->bit_banged_i2c_gpio_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, gpio_base_offset, gpio_frame_size);
                        candidate_design->quad_spi_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                        candidate_design->xadc_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                        candidate_design->user_access =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        user_access_base_offset, user_access_frame_size);
                        design_identified = true;
                    }
                    break;

                case FPGA_DESIGN_NITEFURY_DMA_DDR3:
                    {
                        const uint32_t peripherals_bar_index = 0;
                        const uint32_t dma_bridge_bar_index = 2;
                        const size_t quad_spi_base_offset    = 0x0000;
                        const size_t quad_spi_frame_size     = 0x1000;
                        const size_t xadc_base_offset        = 0x1000;
                        const size_t xadc_frame_size         = 0x1000;
                        const size_t user_access_base_offset = 0x2000;
                        const size_t user_access_frame_size  = 0x1000;

                        candidate_design->dma_bridge_present = true;
                        candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                        candidate_design->dma_bridge_memory_size_bytes = 1024UL * 1024 * 1024;

                        candidate_design->quad_spi_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                        candidate_design->xadc_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                        candidate_design->user_access =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        user_access_base_offset, user_access_frame_size);
                        design_identified = true;
                    }
                    break;

                case FPGA_DESIGN_TEF1001_DMA_STREAM_LOOPBACK:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t iic_base_offset         = 0x0000;
                    const size_t iic_frame_size          = 0x1000;
                    const size_t gpio_base_offset        = 0x1000;
                    const size_t gpio_frame_size         = 0x1000;
                    const size_t quad_spi_base_offset    = 0x2000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x3000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x4000;
                    const size_t user_access_frame_size  = 0x1000;
                    const size_t axi_switch_base_offset  = 0x6000;
                    const size_t axi_switch_frame_size   = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */

                    candidate_design->iic_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, iic_base_offset, iic_frame_size);
                    candidate_design->bit_banged_i2c_gpio_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, gpio_base_offset, gpio_frame_size);
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    if (vfio_device->pci_revision_id >= 1)
                    {
                        candidate_design->axi_switch_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        axi_switch_base_offset, axi_switch_frame_size);
                        candidate_design->axi_switch_num_master_ports = 2;
                        candidate_design->axi_switch_num_slave_ports = 2;
                    }
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_TEF1001_DMA_STREAM_FIXED_DATA:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t iic_base_offset         = 0x0000;
                    const size_t iic_frame_size          = 0x1000;
                    const size_t gpio_base_offset        = 0x1000;
                    const size_t gpio_frame_size         = 0x1000;
                    const size_t quad_spi_base_offset    = 0x2000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x3000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x4000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */

                    candidate_design->iic_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, iic_base_offset, iic_frame_size);
                    candidate_design->bit_banged_i2c_gpio_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, gpio_base_offset, gpio_frame_size);
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_NITEFURY_DMA_STREAM_LOOPBACK:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x1000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;
                    const size_t axi_switch_base_offset  = 0x3000;
                    const size_t axi_switch_frame_size   = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    if (vfio_device->pci_revision_id >= 1)
                    {
                        candidate_design->axi_switch_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        axi_switch_base_offset, axi_switch_frame_size);
                        candidate_design->axi_switch_num_master_ports = 2;
                        candidate_design->axi_switch_num_slave_ports = 2;
                    }
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_NITEFURY_DMA_STREAM_FIXED_DATA:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x1000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_TOSING_160T_DMA_STREAM_LOOPBACK:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x1000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;
                    const size_t axi_switch_base_offset  = 0x3000;
                    const size_t axi_switch_frame_size   = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    if (vfio_device->pci_revision_id >= 1)
                    {
                        candidate_design->axi_switch_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        axi_switch_base_offset, axi_switch_frame_size);
                        candidate_design->axi_switch_num_master_ports = 2;
                        candidate_design->axi_switch_num_slave_ports = 2;
                    }
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_TOSING_160T_DMA_STREAM_FIXED_DATA:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x1000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t sysmon_base_offset      = 0x1000;
                    const size_t sysmon_frame_size       = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;
                    const size_t axi_switch_base_offset  = 0x3000;
                    const size_t axi_switch_frame_size   = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    if (vfio_device->pci_revision_id >= 1)
                    {
                        candidate_design->axi_switch_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        axi_switch_base_offset, axi_switch_frame_size);
                        candidate_design->axi_switch_num_master_ports = 4;
                        candidate_design->axi_switch_num_slave_ports = 4;
                    }
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_FIXED_DATA:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t sysmon_base_offset      = 0x1000;
                    const size_t sysmon_frame_size       = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_RAM:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t sysmon_base_offset      = 0x1000;
                    const size_t sysmon_frame_size       = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 2UL * 1024 * 1024;
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_QUAD_SPI:
                {
                    const uint32_t peripherals_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_SYSMON:
                {
                    const uint32_t peripherals_bar_index = 2;
                    const size_t sysmon_base_offset      = 0x0000;
                    const size_t sysmon_frame_size       = 0x1000;

                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_USER_ACCESS:
                {
                    const uint32_t peripherals_bar_index = 2;
                    const size_t user_access_base_offset = 0x0000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_UART:
                {
                    /* The only peripheral on this design is a UART, which isn't supported as part of the identification.
                     * This design identification is a placeholder until QDMA support is added */
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_IBERT:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const size_t quad_spi_base_offset    = 0x4000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t sysmon_base_offset      = 0x5000;
                    const size_t sysmon_frame_size       = 0x1000;
                    const size_t user_access_base_offset = 0x6000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_TEF1001_DDR3_THROUGHPUT:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const size_t xadc_base_offset        = 0x0000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x1000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t sysmon_base_offset      = 0x1000;
                    const size_t sysmon_frame_size       = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_TEF1001_DMA_STREAM_CRC64:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t iic_base_offset         = 0x0000;
                    const size_t iic_frame_size          = 0x1000;
                    const size_t gpio_base_offset        = 0x1000;
                    const size_t gpio_frame_size         = 0x1000;
                    const size_t quad_spi_base_offset    = 0x2000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x3000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x4000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */

                    candidate_design->iic_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, iic_base_offset, iic_frame_size);
                    candidate_design->bit_banged_i2c_gpio_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, gpio_base_offset, gpio_frame_size);
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_TOSING_160T_DMA_STREAM_CRC64:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x1000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_NITEFURY_DMA_STREAM_CRC64:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t xadc_base_offset        = 0x1000;
                    const size_t xadc_frame_size         = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->xadc_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, xadc_base_offset, xadc_frame_size);
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_AS02MC04_DMA_STREAM_CRC64:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t sysmon_base_offset      = 0x1000;
                    const size_t sysmon_frame_size       = 0x1000;
                    const size_t user_access_base_offset = 0x2000;
                    const size_t user_access_frame_size  = 0x1000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 0;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_AS02MC04_ENUM:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 1; /* Due to the peripherals BAR being 32-bit */
                    const size_t user_access_base_offset = 0x0000;
                    const size_t user_access_frame_size  = 0x1000;

                    /* DMA bridge configured for "Memory Mapped" but no actual memory connected.
                     * The following allows x2x_get_num_channels() to return valid results, but if attempts to actually
                     * perform DMA will timeout. */
                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 4096;

                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_U200_ENUM:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 1; /* Due to the peripherals BAR being 32-bit */
                    const size_t user_access_base_offset = 0x0000;
                    const size_t user_access_frame_size  = 0x1000;

                    /* DMA bridge configured for "Memory Mapped" but no actual memory connected.
                     * The following allows x2x_get_num_channels() to return valid results, but if attempts to actually
                     * perform DMA will timeout. */
                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 4096;

                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_U200_100G_ETHER_SIMPLEX_TX:
                {
                    const uint32_t peripherals_bar_index   = 0;
                    const uint32_t dma_bridge_bar_index    = 2;
                    const size_t user_access_base_offset   = 0x02000;
                    const size_t user_access_frame_size    = 0x02000;
                    const size_t sysmon_base_offset        = 0x04000;
                    const size_t sysmon_frame_size         = 0x02000;
                    const size_t quad_spi_base_offset      = 0x06000;
                    const size_t quad_spi_frame_size       = 0x02000;
                    const size_t cms_base_offset           = 0x40000;
                    const size_t cmac_registers_base_offsets[] =
                    {
                        0x00000,
                        0x10000
                    };
                    const size_t cmac_registers_frame_size = 0x02000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    if (vfio_device->pci_revision_id >= 1)
                    {
                        candidate_design->quad_spi_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        quad_spi_base_offset, quad_spi_frame_size);
                        candidate_design->sysmon_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                        candidate_design->num_sysmon_slaves = 2;
                        candidate_design->cms_subsystem_present = true;
                        candidate_design->cms_subsystem_bar_index = peripherals_bar_index;
                        candidate_design->cms_subsystem_base_offset = cms_base_offset;
                    }

                    candidate_design->num_cmac_ports = (vfio_device->pci_revision_id >= 2) ? 2 : 1;
                    for (uint32_t port_index = 0; port_index < candidate_design->num_cmac_ports; port_index++)
                    {
                        candidate_design->cmac_ports[port_index].cmac_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        cmac_registers_base_offsets[port_index], cmac_registers_frame_size);
                    }

                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_U200_DMA_STREAM_CRC64:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const uint32_t dma_bridge_bar_index = 2;
                    const size_t quad_spi_base_offset    = 0x0000;
                    const size_t quad_spi_frame_size     = 0x1000;
                    const size_t user_access_base_offset = 0x1000;
                    const size_t user_access_frame_size  = 0x1000;
                    const size_t sysmon_base_offset      = 0x2000;
                    const size_t sysmon_frame_size       = 0x2000;

                    candidate_design->dma_bridge_present = true;
                    candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                    candidate_design->dma_bridge_memory_size_bytes = 0; /* DMA bridge configured for "AXI Stream" */
                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 2;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_U200_IBERT_100G_ETHER:
                {
                    const uint32_t peripherals_bar_index = 0;
                    const size_t quad_spi_base_offset    = 0x44000;
                    const size_t quad_spi_frame_size     = 0x01000;
                    const size_t user_access_base_offset = 0x42000;
                    const size_t user_access_frame_size  = 0x01000;
                    const size_t sysmon_base_offset      = 0x40000;
                    const size_t sysmon_frame_size       = 0x02000;

                    /* While the design uses the DMA/Bridge Subsystem, is configured for AXI Bridge mode so the DMA bridge
                     * isn't present. */
                    candidate_design->dma_bridge_present = false;

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 2;
                    candidate_design->user_access =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    user_access_base_offset, user_access_frame_size);
                    candidate_design->cms_subsystem_present = true;
                    candidate_design->cms_subsystem_bar_index = peripherals_bar_index;
                    candidate_design->cms_subsystem_base_offset = 0x0;
                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_OPEN_NIC:
                {
                    /* The addresses are taken from comments in
                     * https://github.com/Xilinx/open-nic-shell/blob/main/src/system_config/system_config_address_map.sv */
                    const uint32_t peripherals_bar_index = 2;
                    const size_t sysmon_base_offset               = 0x010000;
                    const size_t sysmon_frame_size                = 0x002000;
                    const size_t cms_base_offset                  = 0x300000;
                    const size_t quad_spi_base_offset             = 0x340000;
                    const size_t quad_spi_frame_size              = 0x001000;
                    const size_t cmac_registers_base_offsets[] =
                    {
                        0x008000,
                        0x00C000
                    };
                    const size_t cmac_registers_frame_size        = 0x002000;

                    candidate_design->quad_spi_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                    quad_spi_base_offset, quad_spi_frame_size);
                    candidate_design->sysmon_regs =
                            map_vfio_registers_block (vfio_device, peripherals_bar_index, sysmon_base_offset, sysmon_frame_size);
                    candidate_design->num_sysmon_slaves = 2;
                    candidate_design->cms_subsystem_present = true;
                    candidate_design->cms_subsystem_bar_index = peripherals_bar_index;
                    candidate_design->cms_subsystem_base_offset = cms_base_offset;

                    candidate_design->num_cmac_ports = 2;
                    for (uint32_t port_index = 0; port_index < candidate_design->num_cmac_ports; port_index++)
                    {
                        candidate_design->cmac_ports[port_index].cmac_regs =
                                map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                        cmac_registers_base_offsets[port_index], cmac_registers_frame_size);
                    }

                    design_identified = true;
                }
                break;

                case FPGA_DESIGN_VD100_ENUM:
                    {
                        const uint32_t dma_bridge_bar_index = 1; /* Due to the peripherals BAR being 32-bit */

                        /* DMA bridge configured for "Memory Mapped" but no actual memory connected.
                         * The following allows x2x_get_num_channels() to return valid results, but if attempts to actually
                         * perform DMA will timeout. */
                        candidate_design->dma_bridge_present = true;
                        candidate_design->dma_bridge_bar = dma_bridge_bar_index;
                        candidate_design->dma_bridge_memory_size_bytes = 4096;
                        design_identified = true;
                    }
                    break;

                case FPGA_DESIGN_ARRAY_SIZE:
                    /* Shouldn't get here */
                    design_identified = false;
                    break;
                }
            }

            if (design_identified)
            {
                candidate_design->design_id = candidate_design_id;
                candidate_design->vfio_device = vfio_device;
                designs->num_identified_designs++;
            }
            else
            {
                candidate_design_id++;
            }
        }
    }
}


/**
 * @brief Close the VFIO devices which were opened by identify_pcie_fpga_designs()
 * @param[in/out] designs Contains the VFIO devices to close
 */
void close_pcie_fpga_designs (fpga_designs_t *const designs)
{
    close_vfio_devices (&designs->vfio_devices);
}


/**
 * @brief Display the possible FPGA designs in the PC which can be opened by identify_pcie_fpga_designs()
 * @details This only needs to scan the PCI bus, and doesn't attempt to open supported PCI devices using VFIO
 */
void display_possible_fpga_designs (void)
{
    display_possible_vfio_devices (FPGA_DESIGN_ARRAY_SIZE, fpga_design_pci_filters, fpga_design_names);
}


/**
 * @brief Format a string containing the timestamp embedded in the the user access (AXSS register) in the bitstream
 * @param[in] user_access The value of the user access to format
 * @param[out] formatted_timestamp The formatted timestamp string
 */
void format_user_access_timestamp (const uint32_t user_access,
                                   char formatted_timestamp[const USER_ACCESS_TIMESTAMP_LEN])
{
    /* Extract the individual bit fields of the timestamp */
    const uint32_t day    = (user_access & 0xf8000000) >> 27;
    const uint32_t month  = (user_access & 0x07800000) >> 23;
    const uint32_t year   = (user_access & 0x007e0000) >> 17;
    const uint32_t hour   = (user_access & 0x0001f000) >> 12;
    const uint32_t minute = (user_access & 0x00000fc0) >>  6;
    const uint32_t second = (user_access & 0x0000003f);

    const uint32_t epoch_year = 2000;

    snprintf (formatted_timestamp, USER_ACCESS_TIMESTAMP_LEN, "%02u/%02u/%04u %02u:%02u:%02u",
            day, month, year + epoch_year, hour, minute, second);
}
