/*
 * @file identify_pcie_fpga_design.h
 * @date 10 Sep 2023
 * @author Chester Gillon
 * @brief Defines an interface to identity FPGA designs with a PCIe interface
 */

#ifndef IDENTIFY_PCIE_FPGA_DESIGN_H_
#define IDENTIFY_PCIE_FPGA_DESIGN_H_

#include "vfio_access.h"


/* For FPGA_DESIGN_LITEFURY_PROJECT0 and FPGA_DESIGN_NITEFURY_PROJECT0.
 * Defined in the include file since some of the GPIO ports are for use by external programs.
 * This library only uses GPIOs to identify the design type and revision. */
#define FURY_PROJECT0_AXI_PERIPHERALS_BAR 0
#define FURY_PROJECT0_DMA_BRIDGE_BAR      2

/* Offsets in FURY_PROJECT0_AXI_PERIPHERALS_BAR */
#define FURY_PROJECT0_GPIO_0_BASE_OFFSET    0x0000
#define FURY_PROJECT0_GPIO_1_BASE_OFFSET    0x1000
#define FURY_PROJECT0_GPIO_2_BASE_OFFSET    0x2000
#define FURY_PROJECT0_XADC_WIZ_BASE_OFFSET  0x3000
#define FURY_PROJECT0_QUAD_SPI_BASE_OFFSET 0x10000

/* Frame size of each of the above peripherals */
#define FURY_PROJECT0_PERIPHERAL_FRAME_SIZE 0x1000


/* Used to enumerate the FPGA designs which this library can identify */
typedef enum
{
    /* fpga_tests/dma_blkram which uses DMA/Bridge Subsystem to access block RAM */
    FPGA_DESIGN_DMA_BLKRAM,
    /* fpga_tests/i2c_probe/ which contains:
     * a. I2C controller, both the Xilinx "AXI IIC Bus Interface" IP and a GPIO based bit-banged interface.
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     */
    FPGA_DESIGN_I2C_PROBE,
    /* fpga_tests/TOSING_160T_dma_ddr3 which contains:
     * a. DMA/Bridge Subsystem to access 1GB of DDR3
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     */
    FPGA_DESIGN_TOSING_160T_DMA_DDR3,
    /* The Project-0 sample projects for the RHS Research LiteFury and Nitefury which contains:
     * a. DMA/Bridge Subsystem to access DDR3
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     * d. GPIOs. Only used by this library to identify the revision of the design.
     */
    FPGA_DESIGN_LITEFURY_PROJECT0,
    FPGA_DESIGN_NITEFURY_PROJECT0,
    /* fpga_tests/TEF1001_dma_ddr3 which contains:
     * a. DMA/Bridge Subsystem to access 1GB of DDR3
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     * d. I2C controller, both the Xilinx "AXI IIC Bus Interface" IP and a GPIO based bit-banged interface. */
    FPGA_DESIGN_TEF1001_DMA_DDR3,

    /* fpga_tests/NiteFury_dma_ddr3 which contains:
     * a. Access 1GB of DDR3 memory.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC - internal sensors and one external input. */
    FPGA_DESIGN_NITEFURY_DMA_DDR3,

    FPGA_DESIGN_ARRAY_SIZE
} fpga_design_id_t;


extern const char *const fpga_design_names[FPGA_DESIGN_ARRAY_SIZE];


/* Defines one identified design */
typedef struct
{
    /* The enumeration for the design */
    fpga_design_id_t design_id;
    /* Points at the underlying VFIO device */
    vfio_device_t *vfio_device;
    /* When true the DMA/Bridge Subsystem is present.
     * The actual number of channels can be queried from the Xilinx IP. */
    bool dma_bridge_present;
    /* Which BAR contains the DMA/Bridge Subsystem control registers */
    uint32_t dma_bridge_bar;
    /* The amount of memory addressed by the DMA/Bridge Subsystem */
    size_t dma_bridge_memory_size_bytes;
    /* When non-NULL the base of the mapped registers for the Xilinx Quad SPI present in the design */
    uint8_t *quad_spi_regs;
    /* When non-NULL the base of the mapped registers for the XADC IP present in the design */
    uint8_t *xadc_regs;
    /* When non-NULL the base of the mapped registers for the Xilinx AXI IIC IP present in the design */
    uint8_t *iic_regs;
    /* When non-NULL the base of the mapped GIO registers which are used to:
     * a. bit-bang an I2C interface on the TEF1001
     * b. Mux the I2C output pins between either the bit-banged GPIOs or Xilinx AXI IIC */
    uint8_t *bit_banged_i2c_gpio_regs;
    /* For FPGA_DESIGN_LITEFURY_PROJECT0 or FPGA_DESIGN_NITEFURY_PROJECT0 gives the version of the board design */
    uint32_t board_version;
} fpga_design_t;


/* Contains the FPGA designs which have been identified on the PCIs bus */
typedef struct
{
    /* The underlying VFIO devices which have been opened, based upon the supported PCIe identities supported by this library */
    vfio_devices_t vfio_devices;
    /* The number of FPGA designed identified in vfio_devices */
    uint32_t num_identified_designs;
    /* The array of identified FPGA designs */
    fpga_design_t designs[MAX_VFIO_DEVICES];
} fpga_designs_t;


void identify_pcie_fpga_designs (fpga_designs_t *const designs);
void close_pcie_fpga_designs (fpga_designs_t *const designs);


#endif /* IDENTIFY_PCIE_FPGA_DESIGN_H_ */
