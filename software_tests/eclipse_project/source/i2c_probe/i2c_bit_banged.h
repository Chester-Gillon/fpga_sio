/*
 * @file i2c_bit_banged.h
 * @date 7 May 2023
 * @author Chester Gillon
 * @brief Provides an interface for a GPIO bit-banged I2C controller, which is the only master on the I2C bus.
 */

#ifndef I2C_BIT_BANGED_H_
#define I2C_BIT_BANGED_H_

#include <stdbool.h>
#include <stdint.h>


/* The context for a GPIO bit-banged I2C controller */
typedef struct
{
    /* The mapped registers for the GPIO */
    uint8_t *gpio_regs;
    /* Remembers the last state of the GPIO data output bits.
     * Needs to be shadowed in the software as the AXI GPIO PG144 data registers doesn't provide read-back for the output bits */
    uint32_t gpio_data_out;
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

#endif /* I2C_BIT_BANGED_H_ */
