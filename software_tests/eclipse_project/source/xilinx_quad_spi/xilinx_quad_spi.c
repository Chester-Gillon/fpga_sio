/*
 * @file xilinx_quad_spi.c
 * @date 8 Jul 2023
 * @author Chester Gillon
 * @brief Implements an interface to Xilinx "AXI Quad Serial Peripheral Interface (SPI) core" to access the FPGA configuration flash
 * @details
 *  Assumes the core is configured:
 *  a. In Quad SPI mode
 *  b. Performance Mode is disabled, so using the AXI4-Lite interface
 *  c. With the Slave Device set to a single manufacturer.
 *
 *  Has been used with Quad SPI flash devices:
 *  a. S25FL256SAGBHI200 32 MB
 *  b. N25Q256A11ESF40G 32 MB
 *  c. MX25L12835F 16 MB
 */

#include "xilinx_quad_spi.h"
#include "xilinx_quad_spi_host_interface.h"
#include "vfio_access.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>


/* JEDEC assigned manufacturer identities */
#define MANUFACTURER_ID_SPANSION 0x01
#define MANUFACTURER_ID_MICRON   0x20
#define MANUFACTURER_ID_MACRONIX 0xC2


/* Contains names for quad_spi_flash_t */
const char *const quad_spi_flash_names[] =
{
    [QUAD_SPI_FLASH_SPANSION_S25FL_A]  = "Spansion S25FL_A",
    [QUAD_SPI_FLASH_MICRON_N25Q256A]   = "Micron N25Q256A",
    [QUAD_SPI_FLASH_MACRONIX_MX25L128] = "Macronix MX25L128",
    [QUAD_SPI_FLASH_MICRON_MT25QU01G]  = "Micron MT25QU01G"
};


/* Lookup table which defines the opcode which use either 3 or 4 byte addresses for the same operation */
typedef struct
{
    uint8_t three_byte_addr_opcode;
    uint8_t four_byte_addr_opcode;
} quad_spi_addressing_opcodes_t;

static const quad_spi_addressing_opcodes_t quad_spi_addressing_opcodes[] =
{
    {XSPI_OPCODE_SUBSECTOR_ERASE_3_BYTE_ADDRESS, XSPI_OPCODE_SUBSECTOR_ERASE_4_BYTE_ADDRESS},
    {XSPI_OPCODE_SECTOR_ERASE_3_BYTE_ADDRESS   , XSPI_OPCODE_SECTOR_ERASE_4_BYTE_ADDRESS   },
    {XSPI_OPCODE_DUAL_IO_READ_3_BYTE_ADDRESS   , XSPI_OPCODE_DUAL_IO_READ_4_BYTE_ADDRESS   },
    {XSPI_OPCODE_QUAD_IO_READ_3_BYTE_ADDRESS   , XSPI_OPCODE_QUAD_IO_READ_4_BYTE_ADDRESS   }
};


/* Defines one element in a Quad SPI transaction which allows dummy write and/or read bytes to not be ignored
 * rather than buffer space having to be allocated for the dummy bytes. */
typedef struct
{
    /* The number of bytes in the element, which is full-duplex at the interface to the Quad SPI core */
    size_t iov_len;
    /* If non-NULL then the transmit bytes for the element.
     * If NULL dummy bytes are transmitted. */
    const void *write_iov;
    /* If non-NULL where to store the receive bytes for the element.
     * If NULL the receive bytes are discarded. */
    void *read_iov;
} quad_spi_iovec_t;


/**
 * @brief Select a Quad SPI opcode to be used for the address size selected for the controller.
 * @details Can be used to select the address size specific opcode when the flash parameters specify only an opcode
 *          for a fixed address size.
 * @param[in] controller The controller being initialised
 * @param[in/out] opcode The opcode being selected. Will be changed if doesn't match the address size specified in the controller.
 * @return Returns true if the opcode was selected, or false if not recognised.
 */
static bool quad_spi_select_opcode_for_address_size (const quad_spi_controller_context_t *const controller, uint8_t *const opcode)
{
    const uint32_t num_lut_entries = sizeof (quad_spi_addressing_opcodes) / sizeof (quad_spi_addressing_opcodes[0]);

    for (uint32_t lut_index = 0; lut_index < num_lut_entries; lut_index++)
    {
        const quad_spi_addressing_opcodes_t *const lut_entry = &quad_spi_addressing_opcodes[lut_index];

        if ((lut_entry->three_byte_addr_opcode == *opcode) || (lut_entry->four_byte_addr_opcode == *opcode))
        {
            *opcode = (controller->num_address_bytes == 3) ? lut_entry->three_byte_addr_opcode : lut_entry->four_byte_addr_opcode;
            return true;
        }
    }

    printf ("Unable to select opcode %u for num_address_bytes %u\n", *opcode, controller->num_address_bytes);
    return false;
}


/**
 * @brief Perform a single transaction on the Quad SPI interface, delimited by the slave being selected for the entire transaction.
 * @details Doesn't perform any timeout, waits for the transaction to complete or the core to report an error.
 * @param[in/out] controller The Quad SPI controller context to use
 * @param[in] iovcnt The number of elements in the transaction
 * @param[in/out] iov Array of elements for the transaction, where each element can select to:
 *                    a. Transmit real bytes, or dummy bytes (when used to just clock the SPI bus)
 *                    b. Save the receive byte, or discard them (when the values not needed)
 *
 *                    The first byte must be a valid opcode.
 * @returns Returns true if the transaction completed without an error being reported by the Quad SPI core.
 *          Once have returned false the state of any read data in iov[] is undefined and quad_spi_initialise_controller()
 *          will need to be called if attempt to recover and try another transaction.
 */
static bool quad_spi_perform_transaction (quad_spi_controller_context_t *const controller,
                                          const uint32_t iovcnt, const quad_spi_iovec_t iov[const iovcnt])
{
    bool success = true;
    bool transaction_complete = false;
    bool transaction_inhibited = true;
    uint32_t write_completed_iovcnt = 0;
    size_t write_element_index = 0;
    uint32_t read_completed_iovcnt = 0;
    size_t read_element_index = 0;
    uint32_t status_register;
    uint32_t control_register;
    uint32_t num_rx_bytes_pending = 0u;

    /* Loop while no errors reported and the transaction is not complete */
    while (success && !transaction_complete)
    {
        /* To maximise throughput try and keep the transmit FIFO full with the remaining data for the transaction.
         * Stops when the number of receiver bytes pending matches the FIFO depth, rather than checking if the
         * status of the transmit FIFO is full, to avoid over-running the receive FIFO if the transmit FIFO starts
         * to empty as this loop is running. */
        while ((num_rx_bytes_pending < controller->fifo_depth) && (write_completed_iovcnt < iovcnt))
        {
            const quad_spi_iovec_t *const iovec = &iov[write_completed_iovcnt];

            if (iovec->write_iov != NULL)
            {
                /* Write the byte value from the caller supplied element */
                const uint8_t *const write_iov_bytes = iovec->write_iov;
                write_reg32 (controller->quad_spi_regs, XSPI_DATA_TRANSMIT_OFFSET, write_iov_bytes[write_element_index]);
            }
            else
            {
                /* Write a dummy byte, as no caller supplied data */
                write_reg32 (controller->quad_spi_regs, XSPI_DATA_TRANSMIT_OFFSET, 0xff);
            }

            /* For every byte written to the transmit FIFO expect to read a byte from the receive FIFO */
            num_rx_bytes_pending++;

            /* Advance to the next write byte */
            write_element_index++;
            if (write_element_index == iovec->iov_len)
            {
                write_element_index = 0;
                write_completed_iovcnt++;
            }
        }

        /* After the initial fill of the transmit FIFO enable the Quad SPI core to start the transaction */
        if (transaction_inhibited)
        {
            /* Select the single SPI slave */
            write_reg32 (controller->quad_spi_regs, XSPI_SLAVE_SELECT_OFFSET, ~1u);

            /* Remove the transaction inhibit */
            control_register = read_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET);
            control_register &= ~XSPI_CONTROL_MASTER_TRANSACTION_INHIBIT_MASK;
            write_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET, control_register);
            transaction_inhibited = false;
        }

        /* Read available bytes from the receive FIFO */
        status_register = read_reg32 (controller->quad_spi_regs, XSPI_STATUS_OFFSET);
        while ((num_rx_bytes_pending > 0) &&
                ((status_register & XSPI_STATUS_RX_EMPTY_MASK) == 0) && (read_completed_iovcnt < iovcnt))
        {
            const quad_spi_iovec_t *const iovec = &iov[read_completed_iovcnt];
            const uint32_t rx_data = read_reg32 (controller->quad_spi_regs, XSPI_DATA_RECEIVE_OFFSET);

            if (iovec->read_iov != NULL)
            {
                /* Store the byte in caller supplied buffer */
                uint8_t *const read_iov_bytes = iovec->read_iov;
                read_iov_bytes[read_element_index] = (uint8_t) rx_data;
            }

            /* Advance to the next read byte */
            num_rx_bytes_pending--;
            read_element_index++;
            if (read_element_index == iovec->iov_len)
            {
                read_element_index = 0;
                read_completed_iovcnt++;
            }

            status_register = read_reg32 (controller->quad_spi_regs, XSPI_STATUS_OFFSET);
        }

        /* Check for any errors reported by the Quad SPI core */
        success = (status_register & XSPI_STATUS_ERRORS_MASK) == 0;

        /* Detect when the transaction is complete, both in terms of reaching the end of the IOV and the transmit and receive
         * FIFOs being empty. */
        transaction_complete = (write_completed_iovcnt == iovcnt) &&
                (read_completed_iovcnt == iovcnt) &&
                ((status_register & XSPI_STATUS_TX_EMPTY_MASK) != 0) &&
                ((status_register & XSPI_STATUS_RX_EMPTY_MASK) != 0);
    }

    /* Inhibit the transaction to tell the Quad SPI core the transaction is complete */
    control_register = read_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET);
    control_register |= XSPI_CONTROL_MASTER_TRANSACTION_INHIBIT_MASK;
    write_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET, control_register);

    /* De-select the single SPI slave */
    write_reg32 (controller->quad_spi_regs, XSPI_SLAVE_SELECT_OFFSET, ~0u);

    /* If the core reported an error, display the status_register for diagnostic information */
    if (!success)
    {
        const uint8_t *const opcode = iov[0].write_iov;

        printf ("Quad SPI transaction failed for opcode 0x%x: core status_register=0x%x\n", *opcode, status_register);
    }

    return success;
}


/**
 * @brief Read the identification of the Quad SPI flash.
 * @details Only reads the Manufacturer ID and Device ID bytes. Additional manufacturer bytes may be available.
 * @param[out] manufacturer_id The Manufacturer ID
 * @param[out] memory_interface_type The memory interface byte which is the MSB of the Device ID.
 *                                   Manufacturer specific encoding.
 * @param[out] density The density byte which is the LSB of the Device ID.
 *                     Manufacturer specific encoding. On the devices supported in the log2 number of address bits.
 * @returns Returns true if the transaction completed without an error being reported by the Quad SPI core.
 */
static bool quad_spi_read_identification (quad_spi_controller_context_t *const controller,
                                          uint8_t *const manufacturer_id,
                                          uint8_t *const memory_interface_type, uint8_t *const density)
{
    const uint8_t opcode = XSPI_OPCODE_READ_IDENTIFICATION_ID;
    const quad_spi_iovec_t iov[] =
    {
        {
            .iov_len = sizeof (opcode),
            .write_iov = &opcode,
            .read_iov = NULL
        },
        {
            .iov_len = sizeof (*manufacturer_id),
            .write_iov = NULL,
            .read_iov = manufacturer_id
        },
        {
            .iov_len = sizeof (*memory_interface_type),
            .write_iov = NULL,
            .read_iov = memory_interface_type
        },
        {
            .iov_len = sizeof (*density),
            .write_iov = NULL,
            .read_iov = density
        }
    };
    const uint32_t iovcnt = sizeof (iov) / sizeof (iov[0]);

    return quad_spi_perform_transaction (controller, iovcnt, iov);
}


/**
 * @brief Read a 8-bit register from a Quad SPI flash device
 * @param[in/out] controller The controller used for the read
 * @param[in] reg_read_opcode Opcode used to read the register
 * @param[out] reg_value The register value read
 * @returns Returns true if the transaction completed without an error being reported by the Quad SPI core.
 */
static bool quad_spi_read_reg8 (quad_spi_controller_context_t *const controller,
                                const uint8_t reg_read_opcode, uint8_t *const reg_value)
{
    const quad_spi_iovec_t iov[] =
    {
        {
            .iov_len = sizeof (reg_read_opcode),
            .write_iov = &reg_read_opcode,
            .read_iov = NULL
        },
        {
            .iov_len = sizeof (*reg_value),
            .write_iov = NULL,
            .read_iov = reg_value
        }
    };
    const uint32_t iovcnt = sizeof (iov) / sizeof (iov[0]);

    return quad_spi_perform_transaction (controller, iovcnt, iov);
}


/**
 * @brief Read a 16-bit little endian register from a Quad SPI flash device
 * @param[in/out] controller The controller used for the read
 * @param[in] reg_read_opcode Opcode used to read the register
 * @param[out] reg_value The register value read
 * @returns Returns true if the transaction completed without an error being reported by the Quad SPI core.
 */
static bool quad_spi_read_le_reg16 (quad_spi_controller_context_t *const controller,
                                    const uint8_t reg_read_opcode, uint16_t *const reg_value)
{
    bool success;
    uint8_t reg_value_bytes[sizeof (*reg_value)];
    const quad_spi_iovec_t iov[] =
    {
        {
            .iov_len = sizeof (reg_read_opcode),
            .write_iov = &reg_read_opcode,
            .read_iov = NULL,
        },
        {
            .iov_len = sizeof (*reg_value),
            .write_iov = NULL,
            .read_iov = reg_value_bytes
        }
    };
    const uint32_t iovcnt = sizeof (iov) / sizeof (iov[0]);

    success = quad_spi_perform_transaction (controller, iovcnt, iov);
    if (success)
    {
        *reg_value = (uint16_t) (((uint16_t) reg_value_bytes[1] << 8) | reg_value_bytes[0]);
    }

    return success;
}


/**
 * @brief Issue a Quad SPI command which only consists of an opcode
 * @param[in/out] controller The controller used for the command
 * @param[in] opcode The opcode for the command
 * @returns Returns true if the transaction completed without an error being reported by the Quad SPI core.
 */
static bool quad_spi_issue_command (quad_spi_controller_context_t *const controller, const uint8_t opcode)
{
    const quad_spi_iovec_t iov =
    {
        .iov_len = sizeof (opcode),
        .write_iov = &opcode,
        .read_iov = NULL
    };

    return quad_spi_perform_transaction (controller, 1, &iov);
}


/*
 * @brief Unpack a little-endian 16 bit value from an array of bytes
 * @param[in] bytes The bytes to unpack from
 * @return The unpacked value
 */
static uint32_t unpack_little_endian_u16 (const uint8_t bytes[const 2])
{
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8);
}


/**
 * @brief Read the Serial Flash Discoverable Parameters (SFDP) from a Quad SPI flash
 * @param[in/out] controller The controller being initialised.
 * @param[in] sfdp_len The number of bytes of SFDP to read.
 * @param[out] sfdp The SFDP which have been read.
 * @return Returns true if the CFI parameters have been read.
 */
static bool quad_spi_read_serial_flash_discoverable_parameters (quad_spi_controller_context_t *const controller,
                                                                const size_t sfdp_len, uint8_t sfdp[const sfdp_len])
{
    const uint8_t opcode = XSPI_OPCODE_READ_SERIAL_FLASH_DISCOVERABLE_PARAMETERS;
    const uint8_t starting_address[3] = {0};
    const quad_spi_iovec_t iov[] =
    {
        {
            .iov_len = sizeof (opcode),
            .write_iov = &opcode,
            .read_iov = NULL
        },
        {
            /* Address is always 3 bytes */
            .iov_len = sizeof (starting_address),
            .write_iov = starting_address,
            .read_iov = NULL
        },
        {
            /* Dummy clock cycles */
            .iov_len = 1,
            .write_iov = NULL,
            .read_iov = NULL
        },
        {
            .iov_len = sfdp_len,
            .write_iov = NULL,
            .read_iov = sfdp
        }
    };
    const uint32_t iovcnt = sizeof (iov) / sizeof (iov[0]);

    return quad_spi_perform_transaction (controller, iovcnt, iov);
}


/**
 * @brief Find a parameter table in the Serial Flash Discoverable Parameters read from a Quad SPI flash
 * @param[out] params Where to store the parameter table.
 *                    params->table points into sfdp which means the sfdp array must remain in scope
 *                    as long as params does.
 * @param[in] sfdp_len The number of bytes of Serial Flash Discoverable Parameters
 * @param[in] sfdp The Serial Flash Discoverable Parameters to find the parameter table in
 * @param[in] requested_parameter_id The identity of the parameter table to find
 * @param[in] sfdp_populated_len The number of bytes in sfdp populated with parameter tables, based upon
 *                               the parameter table found at the highest offset
 * @return Returns true if requested_parameter_id was found, or false otherwise.
 */
static bool quad_spi_find_sfdp_parameter_table (sfdp_parameter_table_t *const params,
                                                const size_t sfdp_len, const uint8_t sfdp[const sfdp_len],
                                                const uint32_t requested_parameter_id,
                                                uint32_t *const sfdp_populated_len)
{
    bool table_found = false;

    *sfdp_populated_len = 0;

    /* Check for expected string to validate the SFDP. */
    if (strncmp ((const char *) &sfdp[0], "SFDP", 4) != 0)
    {
        printf ("Failed to find SFDP string\n");
        return false;
    }

    /* JESD216F.02 says vendors may include multiple revisions of the Basic Parameter Table, provided the tables,
     * headers are in order starting with the oldest version. Therefore, if there are multiple matches against the
     * requested_parameter_id return the final match. */
    const uint32_t num_parameter_headers = sfdp[6] + 1u; /* Zero based count */
    for (uint32_t header_index = 0; header_index < num_parameter_headers; header_index++)
    {
        const uint32_t parameter_header_size = 8u;
        const uint32_t parameter_header_start_offset = 8 + (header_index * parameter_header_size);

        if ((parameter_header_start_offset + parameter_header_size) > sfdp_len)
        {
            printf ("Attempt to read header off end of SFDP data\n");
            return false;
        }

        const uint32_t parameter_id =
                (((uint32_t) sfdp[parameter_header_start_offset + 7]) << 8) | sfdp[parameter_header_start_offset];
        const uint32_t parameter_length_words = sfdp[parameter_header_start_offset + 3];
        const uint32_t parameter_length_bytes = parameter_length_words * 4;
        const uint32_t major_revision = sfdp[parameter_header_start_offset + 2];
        const uint32_t minor_revision = sfdp[parameter_header_start_offset + 1];
        const uint32_t parameter_table_offset =
                (((uint32_t) sfdp[parameter_header_start_offset + 6]) << 16) |
                (((uint32_t) sfdp[parameter_header_start_offset + 5]) <<  8) |
                             sfdp[parameter_header_start_offset + 4];
        const uint32_t parameter_table_end = parameter_table_offset + parameter_length_bytes;
        if (parameter_table_end <= sfdp_len)
        {
            if (parameter_id == requested_parameter_id)
            {
                params->parameter_id = parameter_id;
                params->parameter_table_length = parameter_length_words;
                params->major_revision = major_revision;
                params->minor_revision = minor_revision;
                params->table = (const uint32_t *) &sfdp[parameter_table_offset];
                table_found = true;
            }

            if (parameter_table_end > *sfdp_populated_len)
            {
                *sfdp_populated_len = parameter_table_end;
            }
        }
        else
        {
            printf ("Attempt to read table off end of SFDP data\n");
            return false;
        }
    }

    return table_found;
}


/**
 * @brief Extract one field from a SFDP table
 * @param[in] params The SFDP table to extract the field from
 * @param[in] word_index The word index containing the field. One-based to match JESD216F.02
 * @param[in] field_width_bits The width of the field in bits
 * @param[in] field_lsb The least significant bit of the field
 * @return The extracted field value
 */
static uint32_t quad_spi_extract_sfdp_field (const sfdp_parameter_table_t *const params, const uint32_t word_index,
                                             const uint32_t field_width_bits, const uint32_t field_lsb)
{
    if ((word_index == 0) || (word_index > params->parameter_table_length))
    {
        printf ("SFDP word_index %u out of range\n", word_index);
        return 0;
    }

    const uint32_t sfdp_word = params->table[word_index - 1];
    const uint32_t field_mask = (1u << field_width_bits) - 1u;

    return (sfdp_word >> field_lsb) & field_mask;
}


/**
 * @brief Select the number of address bytes to be used to address all of the flash.
 * @param[in/out] controller The controller being initialised.
 */
static void quad_spi_select_num_address_bytes (quad_spi_controller_context_t *const controller)
{
    const uint32_t max_flash_size_for_3_byte_addressing = 0x1000000;
    controller->num_address_bytes = (controller->flash_size_bytes <= max_flash_size_for_3_byte_addressing) ? 3 : 4;
}


/**
 * @brief Determine the flash size, and number of address bytes, from the SFDP basic parameters
 * @param[in/out] controller The controller being initialised.
 * @param[in] basic The SFDP basic parameters to obtain the flash size from
 */
static void quad_spi_sfdp_determine_flash_size (quad_spi_controller_context_t *const controller,
                                                const sfdp_parameter_table_t *const basic)
{
    /* Determine the flash size */
    const uint32_t flash_memory_density = quad_spi_extract_sfdp_field (basic, 2, 31, 0);
    const uint32_t density_msb = quad_spi_extract_sfdp_field (basic, 2, 1, 31);
    const uint64_t flash_size_bits = density_msb ?
            1ULL << flash_memory_density : /* Density specified as log2 bits */
            flash_memory_density + 1; /* Density specified as one less than the number of bits */
    controller->flash_size_bytes = (uint32_t) (flash_size_bits / 8);

    /* Set the number of address bytes required to address all of the flash */
    quad_spi_select_num_address_bytes (controller);
}


/**
 * @brief Determine the erase sectors from the SFDP basic parameters
 * @details This always uses Sector Type 1, on the assumption that is the finest grained erase sector size
 * @param[in/out] controller The controller being initialised.
 * @param[in] basic The SFDP basic parameters to obtain the erase sectors from
 * @return Returns true if the opcode was selected, or false if not recognised.
 */
static bool quad_spi_sfdp_determine_erase_sectors (quad_spi_controller_context_t *const controller,
                                                   const sfdp_parameter_table_t *const basic)
{
    const uint32_t erase_size_log2 = quad_spi_extract_sfdp_field (basic, 8, 8, 0);
    controller->erase_block_regions[0].sector_size_bytes = 1u << erase_size_log2;
    controller->erase_block_regions[0].erase_opcode = (uint8_t) quad_spi_extract_sfdp_field (basic, 8, 8, 8);
    controller->erase_block_regions[0].num_sectors =
            controller->flash_size_bytes / controller->erase_block_regions[0].sector_size_bytes;
    controller->num_erase_block_regions = 1;

    return quad_spi_select_opcode_for_address_size (controller, &controller->erase_block_regions[0].erase_opcode);
}


/**
 * @brief Read the Common Flash Interface (CFI) parameters from a Quad SPI flash
 * @param[in/out] controller The controller being initialised.
 * @param[in] cfi_parameters_len The number of bytes of CFI parameters to read
 * @param[out] cfi_parameters The CFI parameters which have been read
 * @return Returns true if the CFI parameters have been read.
 */
static bool quad_spi_read_cfi_parameters (quad_spi_controller_context_t *const controller,
                                          const size_t cfi_parameters_len, uint8_t cfi_parameters[const cfi_parameters_len])
{
    const uint8_t opcode = XSPI_OPCODE_READ_IDENTIFICATION_ID;
    const quad_spi_iovec_t iov[] =
    {
        {
            .iov_len = sizeof (opcode),
            .write_iov = &opcode,
            .read_iov = NULL
        },
        {
            .iov_len = cfi_parameters_len,
            .write_iov = NULL,
            .read_iov = cfi_parameters
        }
    };
    const uint32_t iovcnt = sizeof (iov) / sizeof (iov[0]);

    return quad_spi_perform_transaction (controller, iovcnt, iov);
}


/**
 * @brief Identify the information to use a Spansion S25FL-S Quad SPI flash.
 * @details Used the datasheet:
 *          https://www.infineon.com/dgdl/Infineon-S25FL128SS25FL256S_128_Mb_(16_MB)256_Mb_(32_MB)_3.0V_SPI_Flash_Memory-DataSheet-v20_00-EN.pdf?fileId=8ac78c8c7d0d8da4017d0ecfb6a64a17
 *
 *          Uses Spansion as the manufacturer name rather than Infineon to match the name in the documentation for the
 *          Quad SPI core.
 * @param[in/out] controller The controller being initialised.
 * @return Returns true if have identified a supported Quad SPI flash or false otherwise.
 */
static bool quad_spi_identify_spansion_s25fl_a (quad_spi_controller_context_t *const controller)
{
    spansion_s25fl_a_parameters_t *const my_params = &controller->s25fl_a_params;

    if (!quad_spi_read_cfi_parameters (controller, sizeof (my_params->cfi_parameters), my_params->cfi_parameters))
    {
        return false;
    }
    if (!quad_spi_read_reg8 (controller, XSPI_OPCODE_SPANSION_READ_CONFIGURATION_REGISTER, &my_params->configuration_register))
    {
        return false;
    }

    /* Check for expected strings to validate the CFI parameters */
    if (strncmp ((const char *) &my_params->cfi_parameters[0x10], "QRY", 3) != 0)
    {
        printf ("Failed to find QRY string in CFI parameters\n");
        return false;
    }
    if (strncmp ((const char *) &my_params->cfi_parameters[0x17], "SF", 2) != 0)
    {
        printf ("Failed to find Alternate OEM Command Set in CFI parameters\n");
        return false;
    }

    /* Determine the populated length of the CFI parameters.
     * This function used fixed offsets into the CFI parameters based upon the datasheet.
     * cfi_populated_len is currently only to support quad_spi_dump_raw_parameters()
     *
     * If the ID-CFI length byte is zero the whole 512-byte CFI space must be read because the actual length of the ID-CFI
     * information is longer than can be indicated by this legacy single byte field. */
    const uint32_t id_cfi_length_offset = 3;
    const uint32_t id_cfi_length = my_params->cfi_parameters[id_cfi_length_offset];
    my_params->cfi_populated_len =
            (id_cfi_length == 0) ? sizeof (my_params->cfi_parameters) : (id_cfi_length_offset + id_cfi_length);

    /* Determine the flash size from the CFI Geometry information */
    const uint32_t device_size_log2 = my_params->cfi_parameters[0x27];
    controller->flash_size_bytes = 1u << device_size_log2;

    quad_spi_select_num_address_bytes (controller);

    /* Determine the page size for programming */
    const uint32_t page_size_log2 = unpack_little_endian_u16 (&my_params->cfi_parameters[0x2A]);
    controller->page_size_bytes = 1u << page_size_log2;

    /* Determine the erase block regions */
    const uint32_t erase_block_regions_start_offset = 0x2D;
    const uint32_t num_bytes_per_erase_block_region = 4;
    const uint32_t parameter_sector_size = 4096;
    controller->num_erase_block_regions = my_params->cfi_parameters[0x2C];
    if ((controller->num_erase_block_regions == 0) || (controller->num_erase_block_regions > QUAD_SPI_MAX_ERASE_BLOCK_REGIONS))
    {
        printf ("Out of range num_erase_block_regions value of %u\n", controller->num_erase_block_regions);
        return false;
    }

    for (uint32_t region_index = 0; region_index < controller->num_erase_block_regions; region_index++)
    {
        const uint32_t region_start_offset = erase_block_regions_start_offset + (region_index * num_bytes_per_erase_block_region);

        /* The number of sectors is encoded as one less than the actual number */
        const uint32_t num_sectors = unpack_little_endian_u16 (&my_params->cfi_parameters[region_start_offset + 0]) + 1;

        /* The sector size is specified in multiples of 256 bytes */
        const uint32_t sector_size_bytes = unpack_little_endian_u16 (&my_params->cfi_parameters[region_start_offset + 2]) * 256;

        controller->erase_block_regions[region_index].num_sectors = num_sectors;
        controller->erase_block_regions[region_index].sector_size_bytes = sector_size_bytes;
        controller->erase_block_regions[region_index].erase_opcode = (sector_size_bytes == parameter_sector_size) ?
                XSPI_OPCODE_SUBSECTOR_ERASE_4_BYTE_ADDRESS : XSPI_OPCODE_SECTOR_ERASE_4_BYTE_ADDRESS;
    }

    /* As of Xilinx AXI Quad SPI v3.2 the subsector erase commands are not supported for Spansion devices.
     * Therefore, if the first flash erase block region has 4 KB parameter sectors (aka subsectors) change the
     * region definition to a smaller number of the assumed next larger sector size.
     *
     * Since are not using the parameter sector size, no need to use the TBPARM bit in the Configuration Register
     * to determine if the parameter sectors are located in the bottom or top of the address map. */
    if ((controller->erase_block_regions[0].erase_opcode == XSPI_OPCODE_SUBSECTOR_ERASE_4_BYTE_ADDRESS) &&
        (controller->num_erase_block_regions > 1))
    {
        controller->erase_block_regions[0].num_sectors /=
                controller->erase_block_regions[1].sector_size_bytes / controller->erase_block_regions[0].sector_size_bytes;
        controller->erase_block_regions[0].sector_size_bytes = controller->erase_block_regions[1].sector_size_bytes;
        controller->erase_block_regions[0].erase_opcode = controller->erase_block_regions[1].erase_opcode;
    }

    /* Ensure the erase opcode matches the selected number of address bytes */
    for (uint32_t region_index = 0; region_index < controller->num_erase_block_regions; region_index++)
    {
        if (!quad_spi_select_opcode_for_address_size (controller, &controller->erase_block_regions[region_index].erase_opcode))
        {
            return false;
        }
    }

    /* Locate the "CFI alternate vendor-specific extended query parameter" tables, which was done while manually comparing
     * the latency parameter tables against the datasheet. The contents of these parameters are not yet used by the code. */
    my_params->num_vendor_specific = 0;
    if (strncmp ((const char *) &my_params->cfi_parameters[0x51], "ALT20", 5) == 0)
    {
        uint32_t parameter_start_offset = 0x56;
        const uint32_t parameters_header_size = 2;

        while ((my_params->num_vendor_specific < MAX_CFI_ALTERNATIVE_VENDOR_SPECIFIC_PARMETERS) &&
               (parameter_start_offset + parameters_header_size) < sizeof (my_params->cfi_parameters))
        {
            cfi_alternative_vendor_specific_parmeters_t *const vendor_specific =
                    &my_params->vendor_specific[my_params->num_vendor_specific];

            vendor_specific->parameter_id = my_params->cfi_parameters[parameter_start_offset];
            vendor_specific->parameter_length = my_params->cfi_parameters[parameter_start_offset + 1];
            vendor_specific->parameters = &my_params->cfi_parameters[parameter_start_offset + parameters_header_size];

            parameter_start_offset += parameters_header_size + vendor_specific->parameter_length;
            if (parameter_start_offset <= sizeof (my_params->cfi_parameters))
            {
                my_params->num_vendor_specific++;
            }
        }
    }

    /* For simplicity select a fixed Quad I/O Read opcode and matching latency assuming the non-volatile Configuration Register
     * bits have enabled Quad Mode with a latency code of 00h.
     *
     * Reports a failure if the Configuration Register doesn't have the expected settings. */
    const bool quad_mode_enabled = (my_params->configuration_register & 0x02) != 0;
    const uint32_t latency_code = (my_params->configuration_register & 0xC0) >> 6;
    if (!quad_mode_enabled || (latency_code != 0))
    {
        return false;
    }
    controller->read_opcode = XSPI_OPCODE_QUAD_IO_READ_4_BYTE_ADDRESS;

    /* With Quad IO read mode enabled need to perform a mode bit reset after each read in case the flash device
     * has entered continuous mode due to the Quad SPI core not providing a mechanism to drive the mode bits
     * (nibble after the address) to a deterministic state.
     * TBC is if the Quad SPI core tristates IO[3-0] after has output the address. */
    controller->perform_mode_bit_reset_after_read = true;

    /* The datasheet specifies 1 mode byte and 2 dummy bytes. Since quad_spi_perform_transaction() transmits the value 0xff
     * for dummy bytes the dummy byte value won't be considered as a mode bit pattern of Axh which indicates the following
     * command will also be a Quad I/O Read command. */
    controller->read_num_dummy_bytes = 3;

    return true;
}


/**
 * @brief Identify the information to use a Micron N25Q256A Quad SPI flash.
 * @details Used the datasheet:
 *          https://media-www.micron.com/-/media/client/global/documents/products/data-sheet/nor-flash/serial-nor/n25q/n25q_256mb_1_8v.pdf
 *
 *          The assumption is that the device is in "Extended SPI protocol" mode with the command entered on DQ0 only.
 *          If the device has been placed in "Dual SPI protocol" or "Quad SPI protocol" modes then wouldn't be able to identify
 *          the device as the Quad SPI core always issues commands on DQ0 only.
 * @param[in/out] controller The controller being initialised.
 * @return Returns true if have identified a supported Quad SPI flash or false otherwise.
 */
static bool quad_spi_identify_micron_n25q256a (quad_spi_controller_context_t *const controller)
{
    micron_n25q256a_parameters_t *const my_params = &controller->n25q256a_params;

    if (!quad_spi_read_serial_flash_discoverable_parameters (controller, sizeof (my_params->sfdp), my_params->sfdp))
    {
        return false;
    }
    if (!quad_spi_read_reg8 (controller, XSPI_OPCODE_READ_VOLATILE_CONFIGURATION_REGISTER,
            &my_params->volatile_configuration_register))
    {
        return false;
    }
    if (!quad_spi_read_le_reg16 (controller, XSPI_OPCODE_MICRON_READ_NONVOLATILE_CONFIGURATION_REGISTER,
            &my_params->nonvolatile_configuration_register))
    {
        return false;
    }

    /* The N25Q256A only implemented version 1.0 of the SFDP Basic Parameter table with 9 words,
     * unlike the most recent version 1.8 in JESD216F.02 which has 23 words. */
    const uint32_t min_basic_parameter_table_length = 9;
    if (!quad_spi_find_sfdp_parameter_table (&my_params->basic, sizeof (my_params->sfdp), my_params->sfdp,
            SFDP_JEDEC_BASIC_PARAMETER_ID, &my_params->sfdp_populated_len))
    {
        return false;
    }
    else if (my_params->basic.parameter_table_length < min_basic_parameter_table_length)
    {
        printf ("SFDP basic parameter length only %u\n", my_params->basic.parameter_table_length);
        return false;
    }

    /* Determine flash information from the SFDP */
    quad_spi_sfdp_determine_flash_size (controller, &my_params->basic);
    if (!quad_spi_sfdp_determine_erase_sectors (controller, &my_params->basic))
    {
        return false;
    }

    /* Use Quad IO read with the number of dummy bytes looked up from the SFDP. */
    const uint32_t qaud_io_read_mode_clock_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 3, 3, 5);
    const uint32_t quad_io_read_dummy_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 3, 5, 0);
    const uint32_t num_quad_io_cycles_per_byte = 2;
    controller->read_num_dummy_bytes = (qaud_io_read_mode_clock_cycles + quad_io_read_dummy_cycles) / num_quad_io_cycles_per_byte;
    controller->read_opcode = XSPI_OPCODE_QUAD_IO_READ_4_BYTE_ADDRESS;

    /* It is assumed that XIP mode is disabled in the volatile configuration and non-volatile configure registers,
     * and so the mode bits are not sampled by the flash. */
    controller->perform_mode_bit_reset_after_read = false;

    /* The SFDP Basic Parameter table revision in the N25Q256A doesn't contain word 11 with the Page Size,
     * so use the value from the datasheet. */
    controller->page_size_bytes = 256;

    return true;
}


/**
 * @brief Identify the information to use a MT25QU01G Quad SPI flash
 * @details Used the datasheet:
 *          https://www.micron.com/content/dam/micron/global/secure/products/data-sheet/nor-flash/serial-nor/mt25q/die-rev-b/mt25q-qlkt-u-01g-bbb-0.pdf
 *
 *          And the technical note:
 *          https://www.micron.com/content/dam/micron/global/secure/products/technical-note/nor-flash/tn2506-sfdp-for-mt25q.pdf
 * @param[in/out] controller The controller being initialised.
 * @return Returns true if have identified a supported Quad SPI flash or false otherwise.
 */
static bool quad_spi_identify_micron_mt25qu01g (quad_spi_controller_context_t *const controller)
{
    micron_mt25qu01g_parameters_t *const my_params = &controller->mt25qu01g_params;

    if (!quad_spi_read_serial_flash_discoverable_parameters (controller, sizeof (my_params->sfdp), my_params->sfdp))
    {
        return false;
    }
    if (!quad_spi_read_reg8 (controller, XSPI_OPCODE_READ_VOLATILE_CONFIGURATION_REGISTER,
            &my_params->volatile_configuration_register))
    {
        return false;
    }
    if (!quad_spi_read_le_reg16 (controller, XSPI_OPCODE_MICRON_READ_NONVOLATILE_CONFIGURATION_REGISTER,
            &my_params->nonvolatile_configuration_register))
    {
        return false;
    }

    /* The MT25QU01G only implemented version 1.6 of the SFDP Basic Parameter table with 16 words,
     * unlike the most recent version 1.8 in JESD216F.02 which has 23 words. */
    const uint32_t min_basic_parameter_table_length = 16;
    if (!quad_spi_find_sfdp_parameter_table (&my_params->basic, sizeof (my_params->sfdp), my_params->sfdp,
            SFDP_JEDEC_BASIC_PARAMETER_ID, &my_params->sfdp_populated_len))
    {
        return false;
    }
    else if (my_params->basic.parameter_table_length < min_basic_parameter_table_length)
    {
        printf ("SFDP basic parameter length only %u\n", my_params->basic.parameter_table_length);
        return false;
    }

    /* Determine flash information from the SFDP */
    quad_spi_sfdp_determine_flash_size (controller, &my_params->basic);
    if (!quad_spi_sfdp_determine_erase_sectors (controller, &my_params->basic))
    {
        return false;
    }

    /* Use Quad IO read with the number of dummy bytes looked up from the SFDP. */
    const uint32_t qaud_io_read_mode_clock_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 3, 3, 5);
    const uint32_t quad_io_read_dummy_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 3, 5, 0);
    const uint32_t num_quad_io_cycles_per_byte = 2;
    controller->read_num_dummy_bytes = (qaud_io_read_mode_clock_cycles + quad_io_read_dummy_cycles) / num_quad_io_cycles_per_byte;
    controller->read_opcode = XSPI_OPCODE_QUAD_IO_READ_4_BYTE_ADDRESS;

    /* It is assumed that XIP mode is disabled in the volatile configuration and non-volatile configure registers,
     * and so the mode bits are not sampled by the flash. */
    controller->perform_mode_bit_reset_after_read = false;

    /* Determine the page size for programming */
    const uint32_t page_size_log2 = quad_spi_extract_sfdp_field (&my_params->basic, 11, 4, 4);
    controller->page_size_bytes = 1u << page_size_log2;

    return true;
}


/**
 * @brief Identify the information to use a MX25L128 Quad SPI flash
 * @details Used the datasheet:
 *          https://www.mxic.com.tw/Lists/Datasheet/Attachments/8653/MX25L12835F,%203V,%20128Mb,%20v1.6.pdf
 * @param[in/out] controller The controller being initialised.
 * @return Returns true if have identified a supported Quad SPI flash or false otherwise.
 */
static bool quad_spi_identify_macronix_mx25l128 (quad_spi_controller_context_t *const controller)
{
    macronix_mx25l128_parameters_t *const my_params = &controller->mx25l128_params;

    if (!quad_spi_read_serial_flash_discoverable_parameters (controller, sizeof (my_params->sfdp), my_params->sfdp))
    {
        return false;
    }

    /* The MX25L12835F only implemented version 1.0 of the SFDP Basic Parameter table with 9 words,
     * unlike the most recent version 1.8 in JESD216F.02 which has 23 words. */
    const uint32_t min_basic_parameter_table_length = 9;
    if (!quad_spi_find_sfdp_parameter_table (&my_params->basic, sizeof (my_params->sfdp), my_params->sfdp,
            SFDP_JEDEC_BASIC_PARAMETER_ID, &my_params->sfdp_populated_len))
    {
        return false;
    }
    else if (my_params->basic.parameter_table_length < min_basic_parameter_table_length)
    {
        printf ("SFDP basic parameter length only %u\n", my_params->basic.parameter_table_length);
        return false;
    }

    /* Determine flash information from the SFDP */
    quad_spi_sfdp_determine_flash_size (controller, &my_params->basic);
    if (!quad_spi_sfdp_determine_erase_sectors (controller, &my_params->basic))
    {
        return false;
    }

    /* Use Quad IO (1-4-4) read with the number of dummy bytes looked up from the SFDP. */
    const uint32_t qaud_io_read_mode_clock_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 3, 3, 5);
    const uint32_t quad_io_read_dummy_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 3, 5, 0);
    const uint32_t num_quad_io_cycles_per_byte = 2;
    controller->read_num_dummy_bytes = (qaud_io_read_mode_clock_cycles + quad_io_read_dummy_cycles) / num_quad_io_cycles_per_byte;
    controller->read_opcode = (uint8_t) quad_spi_extract_sfdp_field (&my_params->basic, 3, 8, 8);

    /* Use Dual Output (1-1-2) read with the number of dummy bytes looked up from the SFDP. */
    const uint32_t dual_io_read_mode_clock_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 4, 3, 5);
    const uint32_t dual_io_read_dummy_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 4, 5, 0);
    const uint32_t num_dual_io_cycles_per_byte = 4;
    controller->read_num_dummy_bytes = (dual_io_read_mode_clock_cycles + dual_io_read_dummy_cycles) / num_dual_io_cycles_per_byte;
    controller->read_opcode = (uint8_t) quad_spi_extract_sfdp_field (&my_params->basic, 4, 8, 8);

    /* Use Dual IO (1-2-2) read with the number of dummy bytes looked up from the SFDP. */
    const uint32_t dual_output_read_mode_clock_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 4, 3, 21);
    const uint32_t dual_output_read_dummy_cycles = quad_spi_extract_sfdp_field (&my_params->basic, 4, 5, 16);
    const uint32_t num_dual_output_cycles_per_byte = 4;
    controller->read_num_dummy_bytes =
            (dual_output_read_mode_clock_cycles + dual_output_read_dummy_cycles) / num_dual_output_cycles_per_byte;
    controller->read_opcode = (uint8_t) quad_spi_extract_sfdp_field (&my_params->basic, 4, 8, 24);

    /* While the MX25L12835F datasheet shows the XSPI_OPCODE_SPANSION_MODE_BIT_RESET is supported,
     * the Quad SPI core doesn't support the opcode for Macronix so can't use this option. */
    controller->perform_mode_bit_reset_after_read = false;

    /* The SFDP Basic Parameter table revision in the MX25L128 doesn't contain word 11 with the Page Size,
     * so use the value from the datasheet. */
    controller->page_size_bytes = 256;

    return true;
}


/**
 * @brief Check for consistency between the flash size in bytes and the erase block regions
 * @param[in] controller The controller being initialised.
 * @return Returns true if the flash size is consistent.
 */
static bool quad_spi_check_flash_size_consistency (const quad_spi_controller_context_t *const controller)
{
    uint32_t total_erase_block_bytes = 0;

    for (uint32_t region_index = 0; region_index < controller->num_erase_block_regions; region_index++)
    {
        total_erase_block_bytes +=
                controller->erase_block_regions[region_index].num_sectors *
                controller->erase_block_regions[region_index].sector_size_bytes;
    }

    if (total_erase_block_bytes != controller->flash_size_bytes)
    {
        printf ("Flash size inconsistency : total_erase_block_bytes=%u  flash_size_bytes=%u\n",
                total_erase_block_bytes, controller->flash_size_bytes);
        return false;
    }

    return true;
}


/**
 * @brief Identify if the Quad SPI flash is a supported device, based upon the XSPI_OPCODE_READ_IDENTIFICATION_ID result
 * @details Since the flash devices used to test differed in query commands (common flash interface .vs. serial flash
 *          discoverable parameters) has been written to only support specific devices based upon looking at the
 *          datasheets.
 *          Sets controller parameters to allow operation with the supported devices.
 * @param[in/out] controller The controller being initialised.
 * @return Returns true if have identified a supported Quad SPI flash or false otherwise.
 */
static bool quad_spi_identify_supported_flash (quad_spi_controller_context_t *const controller)
{
    bool supported = false;

    switch (controller->manufacturer_id)
    {
    case MANUFACTURER_ID_SPANSION:
        if (((controller->memory_interface_type == 0x20) && (controller->density == 0x18)) ||
            ((controller->memory_interface_type == 0x02) && (controller->density == 0x19))   )
        {
            controller->flash_type = QUAD_SPI_FLASH_SPANSION_S25FL_A;
            supported = quad_spi_identify_spansion_s25fl_a (controller);
        }
        break;

    case MANUFACTURER_ID_MICRON:
        if ((controller->memory_interface_type == 0xbb) && (controller->density == 0x19))
        {
            controller->flash_type = QUAD_SPI_FLASH_MICRON_N25Q256A;
            supported = quad_spi_identify_micron_n25q256a (controller);
        }
        else if ((controller->memory_interface_type = 0xbb) && (controller->density == 0x21))
        {
            controller->flash_type = QUAD_SPI_FLASH_MICRON_MT25QU01G;
            supported = quad_spi_identify_micron_mt25qu01g (controller);
        }
        break;

    case MANUFACTURER_ID_MACRONIX:
        if ((controller->memory_interface_type == 0x20) && (controller->density == 0x18))
        {
            controller->flash_type = QUAD_SPI_FLASH_MACRONIX_MX25L128;
            supported = quad_spi_identify_macronix_mx25l128 (controller);
        }
        break;

    default:
        break;
    }

    if (!supported)
    {
        printf ("Unsupported flash manufacturer_id 0x%02x (memory_interface_type 0x%02x density 0x%02x)\n",
                controller->manufacturer_id, controller->memory_interface_type, controller->density);
    }

    return supported;
}


/**
 * @brief Perform a software reset of the Qaud SPI core and wait for the reset to complete before setting the control settings.#
 * @param[in] controller The controller being initialised
 * @param[in] control_register_settings The control register settings to apply after the reset is complete.
 */
static void quad_spi_software_reset (quad_spi_controller_context_t *const controller, const uint32_t control_register_settings)
{
    uint32_t status_register;

    /* Assert a software reset */
    write_reg32 (controller->quad_spi_regs, XSPI_SOFTWARE_RESET_OFFSET, XSPI_SOFTWARE_RESET_VALUE);

    /* Wait until the software reset has completed. PG153 doesn't define how to check that the reset has completed.
     * This test was determined empirically. Without it when the AXI QUAD SPI was configured with ext_spi_clk a lower
     * frequency than the axi_aclk then either:
     * a. The FIFO depth read back as zero, since the TX FIFO was initially full,
     * b. qaud_spi_perform_transaction hung.
     */
    do
    {
        status_register = read_reg32 (controller->quad_spi_regs, XSPI_STATUS_OFFSET);
    } while ((status_register & XSPI_STATUS_TX_FULL_MASK) != 0);

    /* Set the required control settings */
    write_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET, control_register_settings);
}


/**
 * @brief Initialise the Quad SPI controller
 * @details Assumes only one thread is using the controller, and resets the Quad SPI core.
 * @param[out] controller The initialised controller
 * @param[in] quad_spi_regs The mapped registers for the Xilinx Quad SPI
 * @return Returns true if the controller was successfully initialised
 */
bool quad_spi_initialise_controller (quad_spi_controller_context_t *const controller, uint8_t *const quad_spi_regs)
{
    uint32_t status_register;

    /* Set master mode enabled, but with transaction inhibit.
     * Uses mode 0 just to avoid an extra-cycle to clock in the opcode.
     * (as per https://www.jblopen.com/qspi-nor-flash-part-3-the-quad-spi-protocol/) */
    const uint32_t control_register_settings =
            XSPI_CONTROL_MASTER_TRANSACTION_INHIBIT_MASK | XSPI_CONTROL_MASTER_MASK | XSPI_CONTROL_SPE_MASK;

    memset (controller, 0, sizeof (*controller));
    controller->quad_spi_regs = quad_spi_regs;

    /* Software reset the Quad SPI core, and then set master mode */
    quad_spi_software_reset (controller, control_register_settings);

    /* Determine the FIFO depth configured in the Quad SPI core by writing to the transmit data register
     * while transactions are inhibited, until the transmit FIFO becomes full. */
    const uint32_t fifo_depth_limit = 512;
    controller->fifo_depth = 0;
    status_register = read_reg32 (controller->quad_spi_regs, XSPI_STATUS_OFFSET);
    while (((status_register & XSPI_STATUS_TX_FULL_MASK) == 0) && (controller->fifo_depth <= fifo_depth_limit))
    {
        write_reg32 (controller->quad_spi_regs, XSPI_DATA_TRANSMIT_OFFSET, XSPI_OPCODE_READ_STATUS_REGISTER);
        controller->fifo_depth++;
        status_register = read_reg32 (controller->quad_spi_regs, XSPI_STATUS_OFFSET);
    }

    switch (controller->fifo_depth)
    {
    case 16:
    case 256:
        /* These are valid FIFO depths which can be configured in the core.
         * Reset the Quad SPI core again now that have determined the depth (a FIFO reset isn't sufficient). */
        quad_spi_software_reset (controller, control_register_settings);
        break;

    default:
        printf ("Invalid Quad SPI core fifo_depth of %u\n", controller->fifo_depth);
        return false;
        break;
    }

    /* Read the Quad SPI flash identity. This is done twice due to the issue in
     * https://support.xilinx.com/s/question/0D54U00005Seaj3SAB/axi-quad-spi-clock-polarity-1-incorrect-timing?language=en_US
     * whereby the first three SPI clock cycles after configuration are not output on the SPI bus, so the first
     * opcode after configuration will not be recognised by the Quad SPI flash. */
    uint8_t initial_manufacturer_id;
    uint8_t initial_memory_interface_type;
    uint8_t initial_density;
    if (!quad_spi_read_identification (controller, &initial_manufacturer_id, &initial_memory_interface_type, &initial_density))
    {
        return false;
    }
    if (!quad_spi_read_identification (controller,
            &controller->manufacturer_id, &controller->memory_interface_type, &controller->density))
    {
        return false;
    }

    if ((controller->manufacturer_id != initial_manufacturer_id) ||
        (controller->memory_interface_type != initial_memory_interface_type) ||
        (controller->density != initial_density))
    {
        printf ("Initial device identification incorrect - ignoring due to Quad SPI core not outputting initial clock cycles\n");
    }

    return quad_spi_identify_supported_flash (controller) && quad_spi_check_flash_size_consistency (controller);
}


/**
 * @brief Set the address bytes for a Quad SPI flash command, in big-endian format
 * @param[in] controller The controller, used to determine the address size used by the flash
 * @param[in] address The address to set
 * @param[out] address_bytes Buffer used to populate the address bytes
 * @param[out] iovec Populated element for the SPI transfer for the address bytes
 */
static void quad_spi_set_address_bytes (const quad_spi_controller_context_t *const controller, const uint32_t address,
                                        uint8_t address_bytes[const sizeof (uint8_t)], quad_spi_iovec_t *const iovec)

{
    iovec->iov_len = controller->num_address_bytes;
    iovec->read_iov = NULL;
    iovec->write_iov = address_bytes;

    if (controller->num_address_bytes == 4)
    {
        /* Generate a 4 byte address */
        address_bytes[0] = (uint8_t) ((address & 0xff000000) >> 24);
        address_bytes[1] = (uint8_t) ((address & 0x00ff0000) >> 16);
        address_bytes[2] = (uint8_t) ((address & 0x0000ff00) >>  8);
        address_bytes[3] = (uint8_t)  (address & 0x000000ff);
    }
    else
    {
        /* Generate a 3 byte address */
        address_bytes[0] = (uint8_t) ((address & 0x00ff0000) >> 16);
        address_bytes[1] = (uint8_t) ((address & 0x0000ff00) >>  8);
        address_bytes[2] = (uint8_t)  (address & 0x000000ff);
    }
}


/**
 * @brief Read data bytes from a Quad SPI flash
 * @param[on/out] controller The controller to use for the read
 * @param[in] start_address Start address to read from the flash
 * @param[in] num_data_bytes The number of bytes to read from the flash
 * @param[out] data The bytes read from the flash
 * @returns Returns true if the read was successful.
 *          Returns false if parameter validation failed or an error was reported by the Quad SPI core.
 */
bool quad_spi_read_flash (quad_spi_controller_context_t *const controller, const uint32_t start_address,
                          const size_t num_data_bytes, uint8_t data[const num_data_bytes])
{
    bool success;
    uint8_t address_bytes[sizeof (uint32_t)];
    uint32_t iovcnt = 0;
    quad_spi_iovec_t iov[4];

    /* Validate requested length */
    if (num_data_bytes == 0)
    {
        printf ("Invalid read length of zero\n");
        return false;
    }
    if ((start_address + num_data_bytes) > controller->flash_size_bytes)
    {
        printf ("Attempt to read off the end of the flash device\n");
        return false;
    }

    /* Read opcode */
    iov[iovcnt].iov_len = sizeof (controller->read_opcode);
    iov[iovcnt].write_iov = &controller->read_opcode;
    iov[iovcnt].read_iov = NULL;
    iovcnt++;

    /* Address bytes */
    quad_spi_set_address_bytes (controller, start_address, address_bytes, &iov[iovcnt]);
    iovcnt++;

    /* Dummy bytes if required */
    if (controller->read_num_dummy_bytes > 0)
    {
        iov[iovcnt].iov_len = controller->read_num_dummy_bytes;
        iov[iovcnt].write_iov = NULL;
        iov[iovcnt].read_iov = NULL;
        iovcnt++;
    }

    /* Data bytes read from flash */
    iov[iovcnt].iov_len = num_data_bytes;
    iov[iovcnt].write_iov = NULL;
    iov[iovcnt].read_iov = data;
    iovcnt++;

    success = quad_spi_perform_transaction (controller, iovcnt, iov);

    if (success && controller->perform_mode_bit_reset_after_read)
    {
        success = quad_spi_issue_command (controller, XSPI_OPCODE_SPANSION_MODE_BIT_RESET);
    }

    return success;
}


/**
 * @brief Display a raw hex dump of the Qaud-SPI flash parameters for diagnosing identification of flash parameters
 * @param[in] controller The Quad SPI controller context to display the parameters for
 */
void quad_spi_dump_raw_parameters (quad_spi_controller_context_t *const controller)
{
    const uint8_t *parameters = NULL;
    uint32_t parameters_len_bytes = 0;
    const char *parameters_name = NULL;
    uint32_t cfi_num_vendor_specific = 0;
    const cfi_alternative_vendor_specific_parmeters_t *cfi_vendor_specific = NULL;

    switch (controller->flash_type)
    {
    case QUAD_SPI_FLASH_SPANSION_S25FL_A:
        parameters = controller->s25fl_a_params.cfi_parameters;
        parameters_len_bytes = controller->s25fl_a_params.cfi_populated_len;
        parameters_name = "CFI";
        cfi_num_vendor_specific = controller->s25fl_a_params.num_vendor_specific;
        cfi_vendor_specific = controller->s25fl_a_params.vendor_specific;
        break;

    case QUAD_SPI_FLASH_MICRON_N25Q256A:
        parameters = controller->n25q256a_params.sfdp;
        parameters_len_bytes = controller->n25q256a_params.sfdp_populated_len;
        parameters_name = "SFDP";
        break;

    case QUAD_SPI_FLASH_MACRONIX_MX25L128:
        parameters = controller->mx25l128_params.sfdp;
        parameters_len_bytes = controller->mx25l128_params.sfdp_populated_len;
        parameters_name = "SFDP";
        break;

    case QUAD_SPI_FLASH_MICRON_MT25QU01G:
        parameters = controller->mt25qu01g_params.sfdp;
        parameters_len_bytes = controller->mt25qu01g_params.sfdp_populated_len;
        parameters_name = "SFDP";
        break;
    }

    if (parameters != NULL)
    {
        printf ("%s raw parameter bytes:\n", parameters_name);
        for (uint32_t byte_index = 0; byte_index < parameters_len_bytes; byte_index++)
        {
            printf ("  [%03X] = %02X %c\n", byte_index, parameters[byte_index],
                    isprint (parameters[byte_index]) ? parameters[byte_index] : '.');
        }
        printf ("\n");
    }

    if (cfi_vendor_specific != NULL)
    {
        /* Used to offset the reported byte index to match the datasheet */
        const uint32_t parameters_header_size = 2;

        for (uint32_t table_index = 0; table_index < cfi_num_vendor_specific; table_index++)
        {
            const cfi_alternative_vendor_specific_parmeters_t *const vendor_specific = &cfi_vendor_specific[table_index];

            printf ("CFI vendor specific table ID 0x%02X\n", vendor_specific->parameter_id);
            for (uint32_t byte_index = 0; byte_index < vendor_specific->parameter_length; byte_index++)
            {
                const uint8_t parameter_byte = vendor_specific->parameters[byte_index];

                printf ("  [%02X] = %02X %c\n", parameters_header_size + byte_index, parameter_byte,
                        isprint (parameter_byte) ? parameter_byte : '.');
            }
            printf ("\n");
        }
    }
}
