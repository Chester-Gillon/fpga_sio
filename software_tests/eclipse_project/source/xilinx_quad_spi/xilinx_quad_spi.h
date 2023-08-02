/*
 * @file xilinx_quad_spi.h
 * @date 8 Jul 2023
 * @author Chester Gillon
 * @brief Defines an interface to Xilinx "AXI Quad Serial Peripheral Interface (SPI) core" to access the FPGA configuration flash
 */

#ifndef XILINX_QUAD_SPI_H_
#define XILINX_QUAD_SPI_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


/* The supported Quad SPI flash devices */
typedef enum
{
    QUAD_SPI_FLASH_SPANSION_S25FL_A,
    QUAD_SPI_FLASH_MICRON_N25Q256A
} quad_spi_flash_t;

extern const char *const quad_spi_flash_names[];


/* Defines one CFI alternate vendor-specific extended query parameter read from the flash */
#define MAX_CFI_ALTERNATIVE_VENDOR_SPECIFIC_PARMETERS 16
typedef struct
{
    /* The identity of the parameters */
    uint8_t parameter_id;
    /* The number of bytes in the parameter */
    uint8_t parameter_length;
    /* Array of length [parameter_length] containing the data bytes for the parameter.
     * The meaning of the bytes depends upon the parameter_id */
    const uint8_t *parameters;
} cfi_alternative_vendor_specific_parmeters_t;


/* SFDP parameter IDs */
#define SFDP_JEDEC_BASIC_PARAMETER_ID 0xFF00


/* Defines one Serial Flash Discovery Parameter table read from the flash */
typedef struct
{
    /* The number of data 32-bit words in the parameter table */
    uint32_t parameter_table_length;
    /* The identity of the parameter table */
    uint32_t parameter_id;
    /* The revision of the parameter table */
    uint32_t major_revision;
    uint32_t minor_revision;
    /* The data in the parameter table */
    const uint32_t *table;
} sfdp_parameter_table_t;


/* Parameters for a QUAD_SPI_FLASH_SPANSION_S25FL_A */
typedef struct
{
    /* The value of the configuration register in the flash */
    uint8_t configuration_register;
    /* The Common Flash Interface (CFI) parameters read from the flash */
    uint8_t cfi_parameters[512];
    /* Vendor specific parameters, which point into the cfi_parameters[] data */
    uint32_t num_vendor_specific;
    cfi_alternative_vendor_specific_parmeters_t vendor_specific[MAX_CFI_ALTERNATIVE_VENDOR_SPECIFIC_PARMETERS];
} spansion_s25fl_a_parameters_t;


/* Parameters for a QUAD_SPI_FLASH_MICRON_N25Q256A */
typedef struct
{
    /* The Serial Flash Discovery Parameters */
    uint8_t sfdp[2048];
    /* The basic parameters obtained from SFDP */
    sfdp_parameter_table_t basic;
    /* The value of the non-volatile configuration register in the flash */
    uint16_t nonvolatile_configuration_register;
    /* The value of the volatile configuration register in the flash */
    uint8_t volatile_configuration_register;
} micron_n25q256a_parameters_t;


/* Defines one erase block region for a Quad SPI flash, in terms of a number of contiguous sectors of the same size.
 * Where each sector can be independently erased. */
#define QUAD_SPI_MAX_ERASE_BLOCK_REGIONS 2
typedef struct
{
    /* The number of sectors in the region */
    uint32_t num_sectors;
    /* The size of each sector */
    uint32_t sector_size_bytes;
    /* The opcode used to erase a sector */
    uint8_t erase_opcode;
} quad_spi_erase_block_region_t;


/* The context for one Quad SPI controller, used to perform flash access */
typedef struct
{
    /* The mapped registers for the Xilinx Quad SPI */
    uint8_t *quad_spi_regs;
    /* The FIFO depth which has been configured in the Quad SPI core */
    uint32_t fifo_depth;
    /* The enumerated identification of the Quad SPI flash read using XSPI_OPCODE_READ_IDENTIFICATION_ID */
    uint8_t manufacturer_id;
    uint8_t memory_interface_type;
    uint8_t density;
    /* The total size of the flash */
    uint32_t flash_size_bytes;
    /* The program page size in bytes */
    uint32_t page_size_bytes;
    /* The number of bytes for addresses used in read, erase or program opcodes */
    uint32_t num_address_bytes;
    /* The type of flash connected to the Quad SPI controller, used to take flash specific action */
    quad_spi_flash_t flash_type;
    /* The opcode used to read the flash */
    uint8_t read_opcode;
    /* The number of dummy (latency) bytes after the address and before the start of the read data */
    uint32_t read_num_dummy_bytes;
    /* When true need to issue a mode bit reset after read_opcode, in case the flash device has falsely sample mode bits
     * as requesting to stay in continuous read mode. */
    bool perform_mode_bit_reset_after_read;
    /* Defines the regions for erasing sectors.
     * erase_block_regions[] is arranged in increasing address order. */
    uint32_t num_erase_block_regions;
    quad_spi_erase_block_region_t erase_block_regions[QUAD_SPI_MAX_ERASE_BLOCK_REGIONS];
    /* flash_type specific parameters */
    union
    {
        /* For QUAD_SPI_FLASH_SPANSION_S25FL_A */
        spansion_s25fl_a_parameters_t s25fl_a_params;
        /* For QUAD_SPI_FLASH_MICRON_N25Q256A */
        micron_n25q256a_parameters_t n25q256a_params;
    };
} quad_spi_controller_context_t;


bool quad_spi_initialise_controller (quad_spi_controller_context_t *const controller, uint8_t *const quad_spi_regs);
bool quad_spi_read_flash (quad_spi_controller_context_t *const controller, const uint32_t start_address,
                          const size_t num_data_bytes, uint8_t data[const num_data_bytes]);

#endif /* XILINX_QUAD_SPI_H_ */
