/*
 * @file ltm4676a_access.h
 * @date 21 May 2023
 * @author Chester Gillon
 * @brief Contains an interface to access a LTM4676A device via PMBus
 */

#ifndef LTM4676A_ACCESS_H_
#define LTM4676A_ACCESS_H_

#include "pmbus_access.h"


/* PMBus command codes which are specific to a LTM4676A, which are "Manufacturer Specific" in the PMBus specification */
#define LTM4676A_COMMAND_MFR_VOUT_PEAK          0xDD
#define LTM4676A_COMMAND_MFR_VIN_PEAK           0xDE
#define LTM4676A_COMMAND_MFR_TEMPERATURE_1_PEAK 0xDF
#define LTM4676A_COMMAND_MFR_READ_IIN           0xED
#define LTM4676A_COMMAND_MFR_COMMON             0xEF /* Manufacturer status bits that are common across multiple LTC ICs/modules. */
#define LTM4676A_COMMAND_MFR_TEMPERATURE_2_PEAK 0xF4
#define LTM4676A_COMMAND_MFR_IOUT_PEAK          0xD7

/* Bit masks for the MFR_COMMON command */
#define LTM4676A_MFR_COMMON_MODULE_NOT_DRIVING_ALERT_LOW_MASK      0x80
#define LTM4676A_MFR_COMMON_MODULE_NOT_BUSY_MASK                   0x40
#define LTM4676A_MFR_COMMON_INTERNAL_CALCULATIONS_NOT_PENDING_MASK 0x20
#define LTM4676A_MFR_COMMON_OUTPUT_NOT_IN_TRANSITION_MASK          0x10
#define LTM4676A_MFR_COMMON_EEPROM_INITIALIZED_MASK                0x08
#define LTM4676A_MFR_COMMON_SHARE_CLK_LOW_MASK                     0x02
#define LTM4676A_MFR_COMMON_WP_PIN_HIGH_MASK                       0x01


void dump_ltm4676a_information (bit_banged_i2c_controller_context_t *const controller, const uint8_t i2c_slave_address);


#endif /* LTM4676A_ACCESS_H_ */
