/*
 * @file identify_pcie_fpga_design.h
 * @date 10 Sep 2023
 * @author Chester Gillon
 * @brief Defines an interface to identity FPGA designs with a PCIe interface
 */

#ifndef IDENTIFY_PCIE_FPGA_DESIGN_H_
#define IDENTIFY_PCIE_FPGA_DESIGN_H_

#include "vfio_access.h"


/* Defines the string length, including trailing null, to hold a formatted timestamp of the form MM/DD/YYYY hh:mm:ss */
#define USER_ACCESS_TIMESTAMP_LEN 20


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
     * a. DMA/Bridge Subsystem to access 8GB of DDR3
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     * d. I2C controller, both the Xilinx "AXI IIC Bus Interface" IP and a GPIO based bit-banged interface. */
    FPGA_DESIGN_TEF1001_DMA_DDR3,

    /* fpga_tests/NiteFury_dma_ddr3 which contains:
     * a. DMA/Bridge Subsystem to access 1GB of DDR3 memory.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC - internal sensors and one external input. */
    FPGA_DESIGN_NITEFURY_DMA_DDR3,

    /* fpga_tests/TEF1001_dma_stream_loopback which contains:
     * a. DMA/Bridge Subsystem loopback of two AXI streams via a AXI4-Stream Switch with register based routing.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC (internal sensors only).
     * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
     *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
    FPGA_DESIGN_TEF1001_DMA_STREAM_LOOPBACK,

    /* fpga_tests/NiteFury_dma_stream_loopback which contains:
     * a. DMA/Bridge Subsystem loopback of two AXI streams via a AXI4-Stream Switch with register based routing.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC - internal sensors and one external input. */
    FPGA_DESIGN_NITEFURY_DMA_STREAM_LOOPBACK,

    /* fpga_tests/TOSING_160T_dma_stream_loopback which contains:
     * a. DMA/Bridge Subsystem loopback of two AXI streams via a AXI4-Stream Switch with register based routing.
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     */
    FPGA_DESIGN_TOSING_160T_DMA_STREAM_LOOPBACK,

    /* fpga_tests/XCKU5P_DUAL_QSFP_dma_stream_loopback which contains:
     * a. DMA/Bridge Subsystem loopback of four AXI streams via a AXI4-Stream Switch with register based routing.
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK,

    /* fpga_tests/XCKU5P_DUAL_QSFP_dma_ram which contains:
     * a. DMA/Bridge Subsystem to access 2MB of URAM.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the SYSMON (internal sensors only) */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_RAM,

    /* fpga_tests/XCKU5P_DUAL_QSFP_qdma_ram which contains:
     * a. Access 2MB of internal memory connected to the QDMA. The QDMA registers are in BAR 0 on all physical functions.
     * c. Each of the 4 physical functions is assigned a different subdevice ID and can access different peripherals.
     *    The assigned peripherals on BAR 2 are
     *    PF0 : Quad SPI connected to the FPGA configuration flash
     *    PF1 : SYSMON (internal sensors only)
     *    PF2 : GPIO input to read user access FPGA build times
     *    PF3 : 16550 UART, which has internal loopback
     */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_QUAD_SPI,    /* PF0 */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_SYSMON,      /* PF1 */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_USER_ACCESS, /* PF2 */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_QDMA_RAM_UART,        /* PF3 */

    /* fpga_tests/XCKU5P_DUAL_QSFP_dma_stream_fixed_data which contains:
     * a. Four AXI streams with fixed data to try and maximum DMA throughput:
     *    - C2H have a fixed data value always ready.
     *    - H2C just asserts TREADY and doesn't do anything with the data.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the SYSMON (internal sensors only) */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_FIXED_DATA,

    /* fpga_tests/TEF1001_dma_stream_fixed_data which contains:
     * a. Two AXI streams with fixed data to try and maximum DMA throughput:
     *    - C2H have a fixed data value always ready.
     *    - H2C just asserts TREADY and doesn't do anything with the data.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC (internal sensors only).
     * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
     *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
    FPGA_DESIGN_TEF1001_DMA_STREAM_FIXED_DATA,

    /* fpga_tests/NiteFury_dma_stream_fixed_data which contains:
     * a. Two AXI streams with fixed data to try and maximum DMA throughput:
     *    - C2H have a fixed data value always ready.
     *    - H2C just asserts TREADY and doesn't do anything with the data.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC - internal sensors and one external input. */
    FPGA_DESIGN_NITEFURY_DMA_STREAM_FIXED_DATA,

    /* fpga_tests/TOSING_160T_dma_stream_fixed_data which contains:
     * a. Two AXI streams with fixed data to try and maximum DMA throughput:
     *    - C2H have a fixed data value always ready.
     *    - H2C just asserts TREADY and doesn't do anything with the data.
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     */
    FPGA_DESIGN_TOSING_160T_DMA_STREAM_FIXED_DATA,

    /* fpga_tests/XCKU5P_DUAL_QSFP_ibert_4.166 which uses IBERT for testing the QSFP.
     * The IBERT core is accessed over JTAG, rather via PCIe.
     *
     * There is a DMA Bridge with the following memory mapped peripherals:
     * a. Management of each QSFP port via:
     *    - AXI IIC Bus Interface
     *    - GPIO for the discrete signals
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the SYSMON (internal sensors only) */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_IBERT,

    /* fpga_tests/TEF1001_ddr3_throughput which uses the AXI Memory Mapped to PCI Express block to access:
     * a. Access the AXI DMA block to transfer between 8GB of DDR3 memory and stream sources/sinks.
     * b. Access the XADC (internal sensors only). */
    FPGA_DESIGN_TEF1001_DDR3_THROUGHPUT,

    /* fpga_tests/XCKU5P_DUAL_QSFP_dma_stream_crc64 which contains:
     * a. DMA/Bridge Subsystem access to four AXI streams which perform a CRC64 calculation.
     * b. Quad SPI connected to the configuration flash.
     * c. SYSMON */
    FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64,

    /* fpga_tests/TEF1001_dma_stream_crc64 which contains:
     * a. DMA/Bridge Subsystem access to two AXI streams which perform a CRC64 calculation.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC (internal sensors only).
     * d. Access the I2C bus, using either a AXI IIC Bus Interface PG090 and AXI GPIO PG144
     *    in the same way as the i2c_probe (FPGA_SIO_SUBDEVICE_ID_I2C_PROBE) design. */
    FPGA_DESIGN_TEF1001_DMA_STREAM_CRC64,

    /* fpga_tests/TOSING_160T_dma_stream_crc64 which contains:
     * a. DMA/Bridge Subsystem access to two AXI streams which perform a CRC64 calculation.
     * b. Quad SPI connected to the configuration flash.
     * c. XADC
     */
    FPGA_DESIGN_TOSING_160T_DMA_STREAM_CRC64,

    /* fpga_tests/NiteFury_dma_stream_crc64 which contains:
     * a. DMA/Bridge Subsystem access to two AXI streams which perform a CRC64 calculation.
     * b. Access a Quad SPI connected to the FPGA configuration flash.
     * c. Access the XADC - internal sensors and one external input. */
    FPGA_DESIGN_NITEFURY_DMA_STREAM_CRC64,

    /* fpga_tests/AS02MC04_dma_stream_crc64 which contains:
     * a. DMA/Bridge Subsystem access to four AXI streams which perform a CRC64 calculation.
     * b. Quad SPI connected to the configuration flash.
     * c. SYSMON */
    FPGA_DESIGN_AS02MC04_DMA_STREAM_CRC64,

    /* fpga_tests/AS02MC04_enum/<designs_with_different_PCIe_configuration> which contains:
     * a. DMA/Bridge Subsystem to contain only a AXI peripheral to read the user access timestamp.
     * b. Have different revisions to investigating to enumerate for x8 width. */
    FPGA_DESIGN_AS02MC04_ENUM,

    /* fpga_tests/U200_enum/<designs_with_different_PCIe_configuration> which contains:
     * a. DMA/Bridge Subsystem to contain only a AXI peripheral to read the user access timestamp.
     * b. Have different revisions to investigating to enumerate for x8 width and bifurcation. */
    FPGA_DESIGN_U200_ENUM,

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
    /* The amount of memory addressed by the DMA/Bridge Subsystem, which also indicates the assumed DMA interface option:
     * a. A non-zero value means "AXI Memory Mapped".
     * b. A zero values means "AXI Stream". */
    size_t dma_bridge_memory_size_bytes;
    /* When non-NULL the base of the mapped registers for the Xilinx Quad SPI present in the design */
    uint8_t *quad_spi_regs;
    /* When non-NULL the base of the mapped registers for the XADC IP present in the design */
    uint8_t *xadc_regs;
    /* When non-NULL the base of the mapped registers for the SYSMON IP present in the design */
    uint8_t *sysmon_regs;
    /* When non-NULL the base of the mapped registers for the Xilinx AXI IIC IP present in the design */
    uint8_t *iic_regs;
    /* When non-NULL the base of the mapped register which contains the user access (AXSS register) which
     * contains timestamp embedded during the FPGA bitstream generation. */
    uint8_t *user_access;
    /* When non-NULL the base of the mapped GIO registers which are used to:
     * a. bit-bang an I2C interface on the TEF1001
     * b. Mux the I2C output pins between either the bit-banged GPIOs or Xilinx AXI IIC */
    uint8_t *bit_banged_i2c_gpio_regs;
    /* When non-NULL the base of the mapped registers used to control the routing of a AXI4-Stream Switch */
    uint8_t *axi_switch_regs;
    /* The number of ports on the AXI4-Stream Switch, as the registers don't define the number of ports configured in the IP. */
    uint32_t axi_switch_num_master_ports;
    uint32_t axi_switch_num_slave_ports;
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
void display_possible_fpga_designs (void);
void format_user_access_timestamp (const uint32_t user_access,
                                   char formatted_timestamp[const USER_ACCESS_TIMESTAMP_LEN]);


#endif /* IDENTIFY_PCIE_FPGA_DESIGN_H_ */
