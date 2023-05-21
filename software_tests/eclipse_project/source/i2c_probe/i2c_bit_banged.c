/*
 * @file i2c_bit_banged.c
 * @date 7 May 2023
 * @author Chester Gillon
 * @brief Implements a GPIO bit-banged I2C controller, which is the only master on the I2C bus.
 * @details
 *   Designed for use with the I2C bus on the Trenz Electronic TEF1001-02-B2IX4-A. A limitation of that board is that the SCL
 *   from the FPGA is output, i.e. can't readback the actual SCL bus signal. This is what limits this implementation to:
 *   a. Being master only.
 *   b. The only master on the I2C bus.
 *   c. Unable to handle I2C slaves which stretch SCL.
 *
 *   Attempts to use a nominal I2C "Standard" SCL frequency of 100 KHz.
 *
 *   Includes support for System Management Bus (SMBus) since:
 *   a. Allows the encapsulation of the Packet Error Code (PEC) calculation, which is computed over the entire message
 *      from the first START condition. I.e. includes the byte sent by i2c_begin() which is not exposed by the API.
 *   b. For a SMBus Block Read the number of bytes to be returned is indicated by the first byte read.
 *      I.e. for a Block Read a variable number of bytes is returned which the bit_banged_i2c_read() API function
 *      doesn't handle.
 *
 *   The SMBus support is based upon SMBus 2.0 (http://smbus.org/specs/smbus20.pdf) since that is the SMBus version
 *   used by Power Management Bus (PMBus).
 */

#include <time.h>
#include <string.h>

#include "i2c_bit_banged.h"
#include "vfio_access.h"


/* The offset to the GPIO data register.
 * This is the only GPIO register used, as the GPIO 3-State Control Register is initialised in the FPGA configuration */
#define GPIO_DATA_OFFSET 0x0

/* GPIO input bit which reads the state of the SDA signal on the I2C bus */
#define GPIO_DATA_SDA_IN_MASK          0x1U

/* GPIO output bits:
 * a. SDA_OUT can control the state of SDA on the I2C bus. 0 pulls low, 1 tri-states so is pulled up.
 *
 * b. SCL_OUT sets the state of SCL on the I2C bus.
 *    Due to the CPLD on the TEF1001 are unable to read back the actual SCL signal on the I2C bus.
 *
 * c. SELECT_BIT_BANG controls the multiplexor for the I2C bus signal to the CPLD:
 *    - 0 selects the Xilinx AXI IIC controller
 *    - 1 selects the bit-banged GPIO SDA_OUT and SCL_OUT signals.
 */
#define GPIO_DATA_SDA_OUT_MASK         0x2U
#define GPIO_DATA_SCL_OUT_MASK         0x4U
#define GPIO_DATA_SELECT_BIT_BANG_MASK 0x8U


/* Least significant bit used to identify the I2C read or write */
#define READ_OPERATION  1 /* Read operation on the I2C bus */
#define WRITE_OPERATION 0 /* Write operation on the I2C bus */


/* Delay values taken from the I2C bus specification UM10204, for Standard Mode using a 100 KHz SCL clock frequency */
#define T_RISE   1000 /* t r rise time of both SDA and SCL signals */
#define T_FALL    300 /* t f fall time of both SDA and SCL signals */
#define T_BUF    4700 /* t BUF bus free time between a STOP and START condition */
#define T_SU_STA 4700 /* t SU;STA set-up time for a repeated START condition in Standard Mode */
#define T_HD_STA 4000 /* t HD;STA hold time (repeated) START condition in Standard Mode */
#define T_SU_STO 4000 /* t SU;STO set-up time for STOP condition */
#define T_LOW    4700 /* t LOW LOW period of the SCL clock */
#define T_HIGH   4000 /* t HIGH HIGH period of the SCL clock */


/* Describes the different SMBus transfer status values */
const char *const smbus_transfer_status_descriptions[SMBUS_TRANSFER_ARRAY_SIZE] =
{
    [SMBUS_TRANSFER_SUCCESS] = "success",
    [SMBUS_TRANSFER_WRITE_ADDRESS_NACK] = "write address NACK",
    [SMBUS_TRANSFER_WRITE_DATA_NACK] = "write data NACK",
    [SMBUS_TRANSFER_READ_ADDRESS_NACK] = "read address NACK",
    [SMBUS_TRANSFER_READ_INCORRECT_PEC] = "read incorrect PEC"
};


/**
 * @brief Use busy-polling to delay for a minimum amount of time to satisfy I2C bus timing.
 * @details Uses CLOCK_MONOTONIC_RAW on the assumption that are running on a modern Kernel which can read that
 *          entirely from user space.
 * @param[in] delay_ns The number of nanoseconds to delay for.
 */
static void bit_bang_delay (const int delay_ns)
{
    const int nsecs_per_sec = 1000000000;
    struct timespec end_time;
    struct timespec now;
    bool delay_complete;

    clock_gettime (CLOCK_MONOTONIC_RAW, &end_time);
    end_time.tv_nsec += delay_ns;
    if (end_time.tv_nsec >= nsecs_per_sec)
    {
        end_time.tv_sec++;
        end_time.tv_nsec -= nsecs_per_sec;
    }

    do
    {
        clock_gettime (CLOCK_MONOTONIC_RAW, &now);
        delay_complete = (now.tv_sec > end_time.tv_sec) ||
                ((now.tv_sec == end_time.tv_sec) && (now.tv_nsec >= end_time.tv_nsec));
    } while (!delay_complete);
}


static inline void scl_low (bit_banged_i2c_controller_context_t *const controller)
{
    controller->gpio_data_out &= ~GPIO_DATA_SCL_OUT_MASK;
    write_reg32 (controller->gpio_regs, GPIO_DATA_OFFSET, controller->gpio_data_out);
    bit_bang_delay (T_FALL);
}


static inline void scl_high (bit_banged_i2c_controller_context_t *const controller)
{
    controller->gpio_data_out |= GPIO_DATA_SCL_OUT_MASK;
    write_reg32 (controller->gpio_regs, GPIO_DATA_OFFSET, controller->gpio_data_out);
    bit_bang_delay (T_RISE);
}


static inline void sda_low (bit_banged_i2c_controller_context_t *const controller)
{
    controller->gpio_data_out &= ~GPIO_DATA_SDA_OUT_MASK;
    write_reg32 (controller->gpio_regs, GPIO_DATA_OFFSET, controller->gpio_data_out);
    bit_bang_delay (T_FALL);
}


static inline void sda_high (bit_banged_i2c_controller_context_t *const controller)
{
    controller->gpio_data_out |= GPIO_DATA_SDA_OUT_MASK;
    write_reg32 (controller->gpio_regs, GPIO_DATA_OFFSET, controller->gpio_data_out);
    bit_bang_delay (T_RISE);
}


/**
 * @brief Read the state of SDA on the I2C bus
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @return The state of SDA
 */
static uint8_t read_sda (bit_banged_i2c_controller_context_t *const controller)
{
    const uint32_t gpio_data = read_reg32 (controller->gpio_regs, GPIO_DATA_OFFSET);

    return (gpio_data & GPIO_DATA_SDA_IN_MASK) != 0U ? 1 : 0;
}


/**
 * @brief Generate a stop condition on the I2C bus
 * @details Assumes SCL is low when called
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 */
static void generate_i2c_stop (bit_banged_i2c_controller_context_t *const controller)
{
    sda_low (controller); /* Need to ensure SDA is low, to generate a rising edge to signify a STOP condition */
    bit_bang_delay (T_LOW);
    scl_high (controller);
    bit_bang_delay (T_SU_STO);
    sda_high (controller);
}


/**
 * @brief When a SMBus CRC is enabled for the current message, update the message CRC with one byte of the message
 * @details The CRC includes the I2C address + read/write bit
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] message_byte The byte to update the CRC with
 */
static void update_smbus_crc_with_byte (bit_banged_i2c_controller_context_t *const controller, const uint8_t message_byte)
{
    if (controller->smbus_message_uses_crc)
    {
        controller->smbus_crc = controller->crc_table[controller->smbus_crc ^ message_byte];
    }
}


/**
 * @brief Transmit one byte on the I2C bus.
 * @details Assumes SCL is low when called
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] tx_byte The byte to transmit, which is also used to update the SMBus message CRC if enabled for the current message
 */
static bool i2c_transmit_byte (bit_banged_i2c_controller_context_t *const controller, const uint8_t tx_byte)
{
    uint8_t output_shift_register = tx_byte;
    uint8_t sampled_sda;
    bool slave_acked;

    /* Transmit most significant bit first */
    for (uint32_t bit = 0u; bit < 8u; bit++)
    {
        if ((output_shift_register & 0x80U) != 0U)
        {
            sda_high (controller);
        }
        else
        {
            sda_low (controller);
        }

        bit_bang_delay (T_LOW);
        scl_high (controller);
        bit_bang_delay (T_HIGH);
        scl_low (controller);

        output_shift_register = (uint8_t) (output_shift_register << 1U);
    }

    /* Take SDA high to be able to read ACK */
    sda_high (controller);

    /* Generate 9th clock and sample SDA to determine if an ACK from the slave */
    scl_high (controller);
    bit_bang_delay (T_HIGH);
    sampled_sda = read_sda (controller);
    scl_low (controller);
    bit_bang_delay (T_LOW);
    slave_acked = sampled_sda == 0;

    update_smbus_crc_with_byte (controller, tx_byte);

    return slave_acked;
}


/**
 * @brief Read one byte from the I2C bus
 * @details Assumes SCL is low when called
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] last_byte Indicates if being called for the last byte to be read:
 *                      - When false sends an ACK to tell the slave another byte will be read
 *                      - When true sends an NACK to tell the slave all bytes have been read
 * @return The received byte, which is also used to the update SMBus message CRC if enabled for the current message
 */
static uint8_t i2c_receive_byte (bit_banged_i2c_controller_context_t *const controller, const bool last_byte)
{
    uint8_t rx_byte = 0u;

    /* Take SDA high to be able to read data */
    sda_high (controller);

    /* Receive most significant bit first */
    for (uint32_t bit = 0u; bit < 8u; bit++)
    {
        rx_byte = (uint8_t) (rx_byte << 1U);

        bit_bang_delay (T_LOW);
        rx_byte |= read_sda (controller);
        scl_high (controller);
        bit_bang_delay (T_HIGH);
        scl_low (controller);
    }

    /* Send a NACK on the last byte, or a ACK otherwise */
    if (last_byte)
    {
        sda_high (controller); /* NACK */
    }
    else
    {
        sda_low (controller); /* ACK */
    }
    bit_bang_delay (T_LOW);
    scl_high (controller);
    bit_bang_delay (T_HIGH);
    scl_low (controller);

    update_smbus_crc_with_byte (controller, rx_byte);

    return rx_byte;
}


/**
 * @brief Begin an I2C bus transfer by sending a (re-)start condition followed by an I2C slave address and read/write operation
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address for the transfer
 * @param[in] operation READ_OPERATION or WRITE_OPERATION
 * @return Returns true if an ACK from the slave, or false if NACK.
 */
static bool i2c_begin (bit_banged_i2c_controller_context_t *const controller,
                       const uint8_t i2c_slave_address, const uint8_t operation)
{
    const uint8_t tx_byte = (uint8_t) ((i2c_slave_address << 1u) | operation);
    bool success;

    if ((controller->gpio_data_out & GPIO_DATA_SCL_OUT_MASK) != 0)
    {
        /* When called with SCL high the bus is free so generate a START condition. Assumes SDA is high */
        bit_bang_delay (T_BUF);
        sda_low (controller); /* Take SDA low to generate the start condition */
        bit_bang_delay (T_HD_STA);
    }
    else
    {
        /* When called with SCL low the bus is in use so generate a RESTART condition */
        sda_high (controller); /* Need to ensure SDA is high, to generate a falling edge to signify a RESTART condition */
        scl_high (controller); /* Take SCL high in preparation for the RESTART condition */
        bit_bang_delay (T_SU_STA);
        sda_low (controller); /* Take SDA low to generate the RESTART condition */
        bit_bang_delay (T_HD_STA);
    }

    scl_low (controller); /* Take SCL low for the beginning of the 1st clock pulse */

    success = i2c_transmit_byte (controller, tx_byte);
    return success;
}


/**
 * @brief Select either the Xilinx AXI IIC or GPIO bit-banged interface
 * @param[in] select_bit_banged When true selects the bit-banged interface, when false select the AXI IIC interface
 * @param[in/out] gpio_regs The mapped registers for the GPIO
 * @param[out] controller The initialised controller for the GPIO bit-banged interface
 */
void select_i2c_controller (const bool select_bit_banged, uint8_t *const gpio_regs,
                            bit_banged_i2c_controller_context_t *const controller)
{
    controller->gpio_regs = gpio_regs;

    /* Default to SMBus PEC disabled */
    (void) memset (controller->smbus_pec_enables, false, sizeof (controller->smbus_pec_enables));

    /* Assume the I2C bus is idle so can initialise the GPIO data output to both SDA and SLA high without
     * needing to try and complete any previous failed transaction.
     *
     * @todo The AXI IIC should always be tracking the bus-busy state even when the GPIO bit-banged controller was is use,
     *       so could perhaps check if the AXI IIC thinks the bus is busy.
     *
     *       One complication is that the GPIO data register doesn't readback the state of the output bits.
     */
    controller->gpio_data_out = GPIO_DATA_SDA_OUT_MASK | GPIO_DATA_SCL_OUT_MASK;
    if (select_bit_banged)
    {
        controller->gpio_data_out |= GPIO_DATA_SELECT_BIT_BANG_MASK;
    }
    write_reg32 (controller->gpio_regs, GPIO_DATA_OFFSET, controller->gpio_data_out);

    /* Create the SMBus CRC look-up table, which uses the "CRC-8-CCITT" algorithm */
    for (uint32_t crc_table_index = 0; crc_table_index < SMBUS_CRC_TABLE_SIZE; crc_table_index++)
    {
        uint8_t data = (uint8_t) crc_table_index;

        for (uint32_t bit_index = 0; bit_index < 8; bit_index++)
        {
            if ((data & 0x80) != 0)
            {
                data = (uint8_t) (data << 1);
                data ^= 0x07;
            }
            else
            {
                data = (uint8_t) (data << 1);
            }
        }
        controller->crc_table[crc_table_index] = data;
    }
}


/**
 * @brief Perform a read from the I2C bus using the GPIO bit-banged interface
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address for the transfer
 * @param[in] num_bytes The number of bytes to read from the I2C slave
 * @param[out] data The bytes read from the I2C slave
 * @param[in] generate_stop Determines if to generate an I2C STOP condition at the end of the read.
 *                          If false then the next transfer will generate a RESTART condition
 * @return Indicates if the read was successful:
 *         - false means NACK as no slave, no data was read, and a STOP condition has been generated.
 *         - true means ACK from the slave, and the data has been read.
 */
bool bit_banged_i2c_read (bit_banged_i2c_controller_context_t *const controller,
                          const uint8_t i2c_slave_address,
                          const size_t num_bytes, uint8_t data[const num_bytes],
                          const bool generate_stop)
{
    bool success;

    controller->smbus_message_uses_crc = false;
    success = i2c_begin (controller, i2c_slave_address, READ_OPERATION);
    if (success)
    {
        for (size_t byte_index = 0; byte_index < num_bytes; byte_index++)
        {
            const bool last_byte = (byte_index + 1) == num_bytes;

            data[byte_index] = i2c_receive_byte (controller, last_byte);
        }

        if (generate_stop)
        {
            generate_i2c_stop (controller);
        }
    }
    else
    {
        /* No ACK from the slave, so have to generate a STOP condition */
        generate_i2c_stop (controller);
    }

    return success;
}


/**
 * @brief Perform a write to the I2C bus using the GPIO bit-banged interface
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address for the transfer
 * @param[in] num_bytes The number of bytes to write to the I2C slave
 * @param[in] data The bytes to write to the I2C slave
 * @param[in] generate_stop Determines if to generate an I2C STOP condition once all bytes written.
 *                          If false then the next transfer will generate a RESTART condition
 * @return The number of bytes successfully written to the slave:
 *         - 0 means no ACK from the slave for the address
 *         - num_bytes means an ACK from the slave for all bytes
 *         - Any other value means an ACK from the slave for only some of the bytes
 *
 *         A STOP condition has been generated when the less than num_bytes returned, as considered an error.
 */
size_t bit_banged_i2c_write (bit_banged_i2c_controller_context_t *const controller,
                             const uint8_t i2c_slave_address,
                             const size_t num_bytes, const uint8_t data[const num_bytes],
                             const bool generate_stop)
{
    bool success;
    size_t num_bytes_written = 0;

    controller->smbus_message_uses_crc = false;
    success = i2c_begin (controller, i2c_slave_address, WRITE_OPERATION);
    while (success && (num_bytes_written < num_bytes))
    {
        success = i2c_transmit_byte (controller, data[num_bytes_written]);
        if (success)
        {
            num_bytes_written++;
        }
    }

    if ((!success) || generate_stop)
    {
        generate_i2c_stop (controller);
    }

    return num_bytes_written;
}


/**
 * @brief Use the GPIO bit-banged controller to read from a I2C device with a byte wide register address
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address to read from
 * @param[in] reg_address Starting register address for the read
 * @param[in] num_bytes The number of bytes to read
 * @param[out] data The bytes read from the I2C device register
 * @return Returns true if the register read was successful, or false if a NACK from the I2C slave
 */
bool bit_banged_i2c_read_byte_addressable_reg (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address,
                                               const uint8_t reg_address,
                                               const size_t num_bytes, uint8_t data[const num_bytes])
{
    size_t num_bytes_written;
    bool success;

    num_bytes_written = bit_banged_i2c_write (controller, i2c_slave_address, sizeof (reg_address), &reg_address, false);
    success = num_bytes_written == sizeof (reg_address);
    if (success)
    {
        success = bit_banged_i2c_read (controller, i2c_slave_address, num_bytes, data, true);
    }

    return success;
}


/**
 * @brief Enable PEC for a SMBus slave, so PEC will be used in further SMBus transfers for the slave
 * @details By storing the PEC enable state in the controller context per SMBus slave, avoids the need for the SMBus transfer
 *          API functions to take a parameter controlling PEC.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address to enable PEC for
 */
void bit_banged_smbus_enable_pec (bit_banged_i2c_controller_context_t *const controller, const uint8_t i2c_slave_address)
{
    controller->smbus_pec_enables[i2c_slave_address] = true;
}


/**
 * @brief Called prior to starting a SMBus message, to reset the CRC if PEC is enabled for the SMBus slave.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address The 7-bit SMBus slave address the message transfer is for.
 */
static void initialise_smbus_message_crc (bit_banged_i2c_controller_context_t *const controller,
                                          const uint8_t i2c_slave_address, const uint8_t command_code)
{
    controller->smbus_message_uses_crc = controller->smbus_pec_enables[i2c_slave_address];
    if (controller->smbus_message_uses_crc)
    {
        controller->smbus_crc = 0;
    }
    controller->last_smbus_command_code = command_code;
}


/**
 * @brief Read data bytes for a SMBus message, and check the PEC byte if PEC is enabled for the message.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] num_data_bytes The number of data bytes to be read
 * @param[out] data The data bytes read
 * @param[in/out] status Used to record the overall status of the message transfer.
 */
static void read_smbus_data_bytes (bit_banged_i2c_controller_context_t *const controller,
                                   const size_t num_data_bytes, uint8_t data[const num_data_bytes],
                                   smbus_transfer_status_t *const status)
{
    size_t byte_index;
    bool last_byte;

    if (*status == SMBUS_TRANSFER_SUCCESS)
    {
        if (controller->smbus_message_uses_crc)
        {
            /* Receive the data bytes */
            last_byte = false;
            for (byte_index = 0; byte_index < num_data_bytes; byte_index++)
            {
                data[byte_index] = i2c_receive_byte (controller, last_byte);
            }

            /* Receive the PEC byte and verify it */
            last_byte = true;
            controller->smbus_expected_pec_byte = controller->smbus_crc;
            controller->smbus_actual_pec_byte = i2c_receive_byte (controller, last_byte);
            if (controller->smbus_actual_pec_byte != controller->smbus_expected_pec_byte)
            {
                *status = SMBUS_TRANSFER_READ_INCORRECT_PEC;
            }
        }
        else
        {
            /* No PEC used - just receive the data bytes */
            for (byte_index = 0; byte_index < num_data_bytes; byte_index++)
            {
                last_byte = (byte_index + 1) == num_data_bytes;
                data[byte_index] = i2c_receive_byte (controller, last_byte);
            }
        }
    }
}


/**
 * @brief Perform a SMBus READ for a fixed number of bytes.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address to read from
 * @param[in] command_code The SMBus command code to perform the read from.
 * @param[in] num_data_bytes The number of bytes to read, which excludes any PEC byte
 * @param[out] data The bytes read from the SMBus slave.
 * @return Indicates if the READ was successful or not.
 */
smbus_transfer_status_t bit_banged_smbus_read (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address,
                                               const uint8_t command_code,
                                               const size_t num_data_bytes, uint8_t data[const num_data_bytes])
{
    smbus_transfer_status_t status = SMBUS_TRANSFER_SUCCESS;

    /* Begin the message with a write operation for the command */
    initialise_smbus_message_crc (controller, i2c_slave_address, command_code);
    if (!i2c_begin (controller, i2c_slave_address, WRITE_OPERATION))
    {
        status = SMBUS_TRANSFER_WRITE_ADDRESS_NACK;
    }

    /* Write the command code */
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        if (!i2c_transmit_byte (controller, command_code))
        {
            status = SMBUS_TRANSFER_WRITE_DATA_NACK;
        }
    }

    /* Issue a re-START for the read */
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        if (!i2c_begin (controller, i2c_slave_address, READ_OPERATION))
        {
            status = SMBUS_TRANSFER_READ_ADDRESS_NACK;
        }
    }

    /* Read the data bytes, which may change the status if the PEC verification fails */
    read_smbus_data_bytes (controller, num_data_bytes, data, &status);

    /* Always generate a STOP condition to free the I2C bus, regardless of if the SMBus message transfer was successful */
    generate_i2c_stop (controller);

    return status;
}
