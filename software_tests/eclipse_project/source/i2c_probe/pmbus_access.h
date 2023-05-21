/*
 * @file pmbus_access.h
 * @date 21 May 2023
 * @author Chester Gillon
 * @brief Provides an interface to access devices via PMBus
 */

#ifndef PMBUS_ACCESS_H_
#define PMBUS_ACCESS_H_

#include "i2c_bit_banged.h"


/* The command codes defined in the PMBus specification */
#define PMBUS_COMMAND_CAPABILITY     0x19
#define PMBUS_COMMAND_PMBUS_REVISION 0x98
#define PMBUS_COMMAND_MFR_ID         0x99
#define PMBUS_COMMAND_MFR_MODEL      0x9A


/* Bit masks for the CAPABILITY COMMAND Data Byte */
#define PMBUS_CAPABILITY_PEC_SUPPORTED_MASK      0x80
#define PMBUS_CAPABILITY_MAX_BUS_SPEED_MASK      0x60
#define PMBUS_CAPABILITY_MAX_BUS_SPEED_100_kHz   0x00
#define PMBUS_CAPABILITY_MAX_BUS_SPEED_400_kHz   0x20
#define PMBUS_CAPABILITY_SMBALERT_SUPPORTED_MASK 0x10


smbus_transfer_status_t read_pmbus_capability (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address, uint8_t *const capability);
smbus_transfer_status_t report_pmbus_capability_and_revision (bit_banged_i2c_controller_context_t *const controller,
                                                              const uint8_t i2c_slave_address);
void report_pmbus_transfer_failure (const bit_banged_i2c_controller_context_t *const controller,
                                    const smbus_transfer_status_t status);
smbus_transfer_status_t report_pmbus_id_and_model (bit_banged_i2c_controller_context_t *const controller,
                                                   const uint8_t i2c_slave_address);

#endif /* PMBUS_ACCESS_H_ */
