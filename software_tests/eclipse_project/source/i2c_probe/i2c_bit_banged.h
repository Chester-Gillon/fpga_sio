/*
 * @file i2c_bit_banged.h
 * @date 7 May 2023
 * @author Chester Gillon
 * @brief Provides an interface for a GPIO bit-banged I2C controller, which is the only master on the I2C bus.
 */

#ifndef I2C_BIT_BANGED_H_
#define I2C_BIT_BANGED_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>


/* The maximum number of 7 bit I2C slave addresses, for creating arrays indexed by I2C address */
#define I2C_MAX_NUM_7_BIT_ADDRESSES 128

#define SMBUS_CRC_TABLE_SIZE 256


/* Defines the possible status values for a SMBus transfer */
typedef enum
{
    /* The transfer was successful */
    SMBUS_TRANSFER_SUCCESS,
    /* NACK for the I2C write transfer address. I.e. no I2C slave for the address. */
    SMBUS_TRANSFER_WRITE_ADDRESS_NACK,
    /* NACK during the I2C write transfer of the command data. The SMBus slave may consider the command invalid. */
    SMBUS_TRANSFER_WRITE_DATA_NACK,
    /* NACK for the I2C read transfer address. May happen in the SMBus slave times out the transfer,
     * due to the driver getting delayed in generating the transfer. */
    SMBUS_TRANSFER_READ_ADDRESS_NACK,
    /* An incorrect PEC byte was received for a read */
    SMBUS_TRANSFER_READ_INCORRECT_PEC,
    /* The Byte Count received from the SMBus slave was outside of the expected range in a Block Read */
    SMBUS_TRANSFER_INVALID_BLOCK_BYTE_COUNT,

    SMBUS_TRANSFER_ARRAY_SIZE
} smbus_transfer_status_t;


const char *const smbus_transfer_status_descriptions[SMBUS_TRANSFER_ARRAY_SIZE];


/* The context for a GPIO bit-banged I2C controller */
typedef struct
{
    /* The mapped registers for the GPIO */
    uint8_t *gpio_regs;
    /* Remembers the last state of the GPIO data output bits.
     * Needs to be shadowed in the software as the AXI GPIO PG144 data registers doesn't provide read-back for the output bits */
    uint32_t gpio_data_out;
    /* Array indexed by I2C slave address, which when true enabled SMBus Packet Error Code (PEC) calculation for any
     * SMBus transfer to that I2C slave address. Allows PEC to enabled for SMBus slaves which support it. */
    bool smbus_pec_enables[I2C_MAX_NUM_7_BIT_ADDRESSES];
    /* When true the current transfer uses a SMBus message CRC, which is updated as bytes are written / read during the
     * transfer of the message */
    bool smbus_message_uses_crc;
    /* When performing a message transfer for a SMBus slave with PEC enabled, used to calculate the CRC for the message */
    uint8_t smbus_crc;
    /* Lookup table created at initialisation to be used to calculate the SMBus CRC a byte at a time */
    uint8_t crc_table[SMBUS_CRC_TABLE_SIZE];
    /* When SMBUS_TRANSFER_READ_INCORRECT_PEC is returned for a read, records the actual and expected PEC byte for diagnostics */
    uint8_t smbus_expected_pec_byte;
    uint8_t smbus_actual_pec_byte;
    /* The last SMBus command attempting, for reporting diagnostic information about an unsuccessful transfer */
    uint8_t last_smbus_command_code;
    /* The last SMBus block Byte Count received, for recording diagnostic information for SMBUS_TRANSFER_INVALID_BLOCK_BYTE_COUNT */
    uint8_t last_smbus_block_byte_count;
} bit_banged_i2c_controller_context_t;


void select_i2c_controller (const bool select_bit_banged, uint8_t *const gpio_regs,
                            bit_banged_i2c_controller_context_t *const controller);
bool bit_banged_i2c_read (bit_banged_i2c_controller_context_t *const controller,
                          const uint8_t i2c_slave_address,
                          const size_t num_bytes, uint8_t data[const num_bytes],
                          const bool generate_stop);
size_t bit_banged_i2c_write (bit_banged_i2c_controller_context_t *const controller,
                             const uint8_t i2c_slave_address,
                             const size_t num_bytes, const uint8_t data[const num_bytes],
                             const bool generate_stop);
bool bit_banged_i2c_read_byte_addressable_reg (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address,
                                               const uint8_t reg_address,
                                               const size_t num_bytes, uint8_t data[const num_bytes]);

void bit_banged_smbus_enable_pec (bit_banged_i2c_controller_context_t *const controller, const uint8_t i2c_slave_address);
smbus_transfer_status_t bit_banged_smbus_read (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address,
                                               const uint8_t command_code,
                                               const size_t num_data_bytes, uint8_t data[const num_data_bytes]);
smbus_transfer_status_t bit_banged_smbus_block_read (bit_banged_i2c_controller_context_t *const controller,
                                                     const uint8_t i2c_slave_address,
                                                     const uint8_t command_code,
                                                     const size_t max_data_bytes, uint8_t data[const max_data_bytes],
                                                     size_t *const num_data_bytes);

#endif /* I2C_BIT_BANGED_H_ */
