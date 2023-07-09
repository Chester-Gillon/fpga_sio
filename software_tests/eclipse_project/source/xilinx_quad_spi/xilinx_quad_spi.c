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
 *  a. S25FL128SAGBHI210 16 MB
 *     Data sheet: https://www.infineon.com/dgdl/Infineon-S25FL128SS25FL256S_128_Mb_(16_MB)256_Mb_(32_MB)_3.0V_SPI_Flash_Memory-DataSheet-v20_00-EN.pdf?fileId=8ac78c8c7d0d8da4017d0ecfb6a64a17
 *     Known as a "Spansion" device to the Quad SPI core.
 *
 *  b. N25Q256A11ESF40G 32 MB
 *     Data sheet: https://media-www.micron.com/-/media/client/global/documents/products/data-sheet/nor-flash/serial-nor/n25q/n25q_256mb_1_8v.pdf
 */

#include "xilinx_quad_spi.h"
#include "xilinx_quad_spi_host_interface.h"
#include "vfio_access.h"

#include <string.h>
#include <stdio.h>


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
bool quad_spi_read_identification (quad_spi_controller_context_t *const controller,
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
    write_reg32 (controller->quad_spi_regs, XSPI_SOFTWARE_RESET_OFFSET, XSPI_SOFTWARE_RESET_VALUE);
    write_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET, control_register_settings);

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
        write_reg32 (controller->quad_spi_regs, XSPI_SOFTWARE_RESET_OFFSET, XSPI_SOFTWARE_RESET_VALUE);
        write_reg32 (controller->quad_spi_regs, XSPI_CONTROL_OFFSET, control_register_settings);
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
        printf ("Initial device identification incorrect - ignoring due to Qaud SPI core not outputting initial clock cycles\n");
    }

    return true;
}
