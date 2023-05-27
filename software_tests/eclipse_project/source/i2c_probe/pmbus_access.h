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
#define PMBUS_COMMAND_PAGE_PLUS_READ      0x06
#define PMBUS_COMMAND_WRITE_PROTECT       0x10
#define PMBUS_COMMAND_CAPABILITY          0x19
#define PMBUS_COMMAND_VOUT_MODE           0x20
#define PMBUS_COMMAND_VOUT_COMMAND        0x21
#define PMBUS_COMMAND_VOUT_MAX            0x24
#define PMBUS_COMMAND_VOUT_MARGIN_HIGH    0x25
#define PMBUS_COMMAND_VOUT_MARGIN_LOW     0x26
#define PMBUS_COMMAND_VOUT_OV_FAULT_LIMIT 0x40
#define PMBUS_COMMAND_VOUT_OV_WARN_LIMIT  0x42
#define PMBUS_COMMAND_VOUT_UV_WARN_LIMIT  0x43
#define PMBUS_COMMAND_VOUT_UV_FAULT_LIMIT 0x44
#define PMBUS_COMMAND_IOUT_OC_FAULT_LIMIT 0x46
#define PMBUS_COMMAND_IOUT_OC_WARN_LIMIT  0x4A
#define PMBUS_COMMAND_OT_FAULT_LIMIT      0x4F
#define PMBUS_COMMAND_OT_WARN_LIMIT       0x51
#define PMBUS_COMMAND_UT_FAULT_LIMIT      0x53
#define PMBUS_COMMAND_READ_VIN            0x88
#define PMBUS_COMMAND_READ_IIN            0x89
#define PMBUS_COMMAND_READ_VOUT           0x8B
#define PMBUS_COMMAND_READ_IOUT           0x8C
#define PMBUS_COMMAND_READ_TEMPERATURE_1  0x8D
#define PMBUS_COMMAND_READ_TEMPERATURE_2  0x8E
#define PMBUS_COMMAND_READ_DUTY_CYCLE     0x94
#define PMBUS_COMMAND_READ_POUT           0x96
#define PMBUS_COMMAND_PMBUS_REVISION      0x98
#define PMBUS_COMMAND_MFR_ID              0x99
#define PMBUS_COMMAND_MFR_MODEL           0x9A
#define PMBUS_COMMAND_MFR_VOUT_MAX        0xA5


/* Bit masks for the CAPABILITY COMMAND Data Byte */
#define PMBUS_CAPABILITY_PEC_SUPPORTED_MASK      0x80
#define PMBUS_CAPABILITY_MAX_BUS_SPEED_MASK      0x60
#define PMBUS_CAPABILITY_MAX_BUS_SPEED_100_kHz   0x00
#define PMBUS_CAPABILITY_MAX_BUS_SPEED_400_kHz   0x20
#define PMBUS_CAPABILITY_SMBALERT_SUPPORTED_MASK 0x10


/* Used to set a compile time maximum number of PMBus page for data structures */
#define PMBUS_MAX_PAGES 2

/* The supported formats for reading sensors values */
typedef enum
{
    /* 5 bit two’s complement exponent and 11 bit two’s complement mantissa */
    PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
    /* 16 bit unsigned mantissa. Exponent obtained by PMBUS_COMMAND_VOUT_MODE */
    PMBUS_SENSOR_FORMAT_LINEAR_16U
} pmbus_sensor_format_t;

/* Defines one PMBus sensor which can be read */
typedef struct
{
    /* The PMBus code to read for the sensor */
    uint8_t command_code;
    /* The format of sensor. Specified in this structure to use with PMBus device which don't support the QUERY command */
    pmbus_sensor_format_t sensor_format;
    /* When true the sensor is paged, and has a reading for each page. When false only a single reading is available */
    bool paged;
    /* The name of the sensor */
    const char *name;
    /* The units of the sensor */
    const char *units;
} pmbus_sensor_definition_t;

/* Used to store the values read from one sensor.
 * The paged field in the sensor definition defines the number of valid entries in the arrays:
 * - When true the valid indices are [0..num_pages-1.
 * - When false only index [0] is valid. */
typedef struct
{
    /* The raw values read */
    uint16_t raw_sensor_values[PMBUS_MAX_PAGES];
    /* The scaled sensor value, which is a value in the units defined in the sensor definition */
    double scaled_sensor_values[PMBUS_MAX_PAGES];
} pmbus_sensor_reading_t;


smbus_transfer_status_t pmbus_paged_read (bit_banged_i2c_controller_context_t *const controller,
                                          const uint8_t i2c_slave_address,
                                          const uint8_t page_number,
                                          const uint8_t read_command_code,
                                          const size_t data_size_bytes,
                                          void *const data);
smbus_transfer_status_t read_pmbus_capability (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address, uint8_t *const capability);
smbus_transfer_status_t report_pmbus_capability_and_revision (bit_banged_i2c_controller_context_t *const controller,
                                                              const uint8_t i2c_slave_address);
void report_pmbus_transfer_failure (const bit_banged_i2c_controller_context_t *const controller,
                                    const smbus_transfer_status_t status);
smbus_transfer_status_t report_pmbus_id_and_model (bit_banged_i2c_controller_context_t *const controller,
                                                   const uint8_t i2c_slave_address);
smbus_transfer_status_t read_pmbus_sensors (bit_banged_i2c_controller_context_t *const controller,
                                            const uint8_t i2c_slave_address,
                                            const size_t num_pages,
                                            const size_t num_sensors,
                                            const pmbus_sensor_definition_t sensor_definitions[const num_sensors],
                                            pmbus_sensor_reading_t sensor_readings[const num_sensors]);
void display_pmbus_sensors (const size_t num_pages,
                            const size_t num_sensors,
                            const pmbus_sensor_definition_t sensor_definitions[const num_sensors],
                            const pmbus_sensor_reading_t sensor_readings[const num_sensors]);

#endif /* PMBUS_ACCESS_H_ */
