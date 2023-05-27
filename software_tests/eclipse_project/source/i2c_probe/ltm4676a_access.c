/*
 * @file ltm4676a_access.c
 * @date 21 May 2023
 * @author Chester Gillon
 * @brief Provides an interface to access a LTM4676A device via PMBus
 * @details
 *   The following datasheet was used to implement this source file:
 *     https://www.analog.com/media/en/technical-documentation/data-sheets/4676afa.pdf
 */

#include "ltm4676a_access.h"

#include <stdio.h>

/* The LTM4676A is a dual-channel DCDC converter, with channel specific sensors per-page */
#define LTM4676A_NUM_PAGES 2

/* Defines the LTM4676A sensors which are read and displayed.
 * The sensors with a PMBUS_COMMAND_* prefix are defined by the PMBus specification.
 * The sensors with a LTM4674A_COMMAND_MFR_* prefix are manufacturer specific. */
#define LTM4676A_NUM_SENSORS (sizeof (ltm4676a_sensor_definitions) / sizeof (ltm4676a_sensor_definitions[0]))
static const pmbus_sensor_definition_t ltm4676a_sensor_definitions[] =
{
    /* From the TELEMETRY list of PMBus commands in the LTM4676A datasheet */
    {
        .command_code = PMBUS_COMMAND_READ_VIN,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = false,
        .name = "Measured input supply (SVin) voltage",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_READ_VOUT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Measured output voltage",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_READ_IIN,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = false,
        .name = "Calculated input supply current",
        .units = "A"
    },
    {
        .command_code = LTM4676A_COMMAND_MFR_READ_IIN,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Calculated input current per channel",
        .units = "A"
    },
    {
        .command_code = PMBUS_COMMAND_READ_IOUT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Measured output current",
        .units = "A"
    },
    {
        .command_code = PMBUS_COMMAND_READ_TEMPERATURE_1,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Power stage temperature sensor",
        .units = "C"
    },
    {
        .command_code = PMBUS_COMMAND_READ_TEMPERATURE_2,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = false,
        .name = "Control IC die temperature",
        .units = "C"
    },
    {
        .command_code = PMBUS_COMMAND_READ_DUTY_CYCLE,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Duty cycle of the top gate control signal",
        .units = "%"
    },
    {
        .command_code = PMBUS_COMMAND_READ_POUT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Calculated output power",
        .units = "W"
    },
    {
        .command_code = LTM4676A_COMMAND_MFR_VOUT_PEAK,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Maximum measured value of READ_VOUT since last MFR_CLEAR_PEAKS",
        .units = "V"
    },
    {
        .command_code = LTM4676A_COMMAND_MFR_VIN_PEAK,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = false,
        .name = "Maximum measured value of READ_VIN since last MFR_CLEAR_PEAKS",
        .units = "V"
    },
    {
        .command_code = LTM4676A_COMMAND_MFR_TEMPERATURE_1_PEAK,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Maximum measured value of power stage temperature since last MFR_CLEAR_PEAKS",
        .units = "C"
    },
    {
        .command_code = LTM4676A_COMMAND_MFR_TEMPERATURE_2_PEAK,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = false,
        .name = "Maximum measured value of control IC die temperature since last MFR_CLEAR_PEAKS",
        .units = "C"
    },
    {
        .command_code = LTM4676A_COMMAND_MFR_IOUT_PEAK,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Report the maximum measured value of READ_IOUT since last MFR_CLEAR_PEAKS",
        .units = "A"
    },

    /* From the Output Voltage and Limits list of PMBus commands in the LTM4676A datasheet */
    {
        .command_code = PMBUS_COMMAND_VOUT_MAX,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Upper limit on the commanded output voltage including VOUT_MARGIN_HIGH",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_OV_FAULT_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Output overvoltage fault limit",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_OV_WARN_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Output overvoltage warning limit",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_MARGIN_HIGH,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Margin high output voltage set point",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_COMMAND,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Nominal output voltage set point",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_MARGIN_LOW,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Margin low output voltage set point",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_UV_WARN_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Output undervoltage warning limit",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_VOUT_UV_FAULT_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Output undervoltage fault limit",
        .units = "V"
    },
    {
        .command_code = PMBUS_COMMAND_MFR_VOUT_MAX,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_16U,
        .paged = true,
        .name = "Maximum allowed output voltage including VOUT_OV_FAULT_LIMIT",
        .units = "V"
    },

    /* From the Output Current list of PMBus commands in the LTM4676A datasheet */
    {
        .command_code = PMBUS_COMMAND_IOUT_OC_FAULT_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Output overcurrent fault limit",
        .units = "A"
    },
    {
        .command_code = PMBUS_COMMAND_IOUT_OC_WARN_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Output overcurrent warning limit",
        .units = "A"
    },

    /* From the Power Stage Temperature Limits list of PMBus commands in the LTM4676A datasheet */
    {
        .command_code = PMBUS_COMMAND_OT_FAULT_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Power stage overtemperature fault limit",
        .units = "C"
    },
    {
        .command_code = PMBUS_COMMAND_OT_WARN_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Power stage overtemperature warning limit",
        .units = "C"
    },
    {
        .command_code = PMBUS_COMMAND_UT_FAULT_LIMIT,
        .sensor_format = PMBUS_SENSOR_FORMAT_LINEAR_5S_11S,
        .paged = true,
        .name = "Power stage undertemperature fault limit",
        .units = "C"
    }
};

/* Used to store the sensor readings read from one LTM4676A */
static pmbus_sensor_reading_t ltm4676a_sensor_readings[LTM4676A_NUM_SENSORS];


/**
 * @brief Display write protect information for a LTM4676A
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address The I2C address of the LTM4676A to dump the information for
 */
static smbus_transfer_status_t report_ltm4676a_write_protect (bit_banged_i2c_controller_context_t *const controller,
                                                              const uint8_t i2c_slave_address)
{
    smbus_transfer_status_t status;
    uint8_t write_protect_data_byte;
    uint8_t mfr_common;
    const char *write_protect;

    status = bit_banged_smbus_read (controller, i2c_slave_address, PMBUS_COMMAND_WRITE_PROTECT,
            sizeof (write_protect_data_byte), &write_protect_data_byte);
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        status = bit_banged_smbus_read (controller, i2c_slave_address, LTM4676A_COMMAND_MFR_COMMON,
                sizeof (mfr_common), &mfr_common);
    }

    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        /* Report the write protect status using the descriptions from the LTM4676A datasheet which has some different
         * descriptions compared to the PMBus specification. */
        switch (write_protect_data_byte)
        {
        case 0x80: write_protect = "Disable all writes except to the WRITE_PROTECT, PAGE, MFR_EE_UNLOCK and STORE_USER_ALL command"; break;
        case 0x40: write_protect = "Disable all writes except to the WRITE_PROTECT, PAGE, MFR_EE_UNLOCK, MFR_CLEAR_PEAKS, STORE_USER_ALL, OPERATION and CLEAR_FAULTS command. Individual fault bits can be cleared by writing a 1 to the respective bits in the STATUS registers."; break;
        case 0x20: write_protect = "Disable all writes except to the WRITE_PROTECT, OPERATION, MFR_EE_UNLOCK, MFR_CLEAR_PEAKS, CLEAR_FAULTS, PAGE, ON_OFF_CONFIG, VOUT_COMMAND and STORE_USER_ ALL. Individual fault bits can be cleared by writing a 1 to the respective bits in the STATUS registers."; break;
        case 0x00: write_protect = "Enable writes to all commands"; break;
        default: write_protect = "unknown";
        }
        printf ("  WRITE_PROTECT=0x%02x : %s\n", write_protect_data_byte, write_protect);

        /* Display the write protect pin status, from a LTM4676A specific PMBus command */
        printf ("  MFR_COMMON=0x%02x : WP pin %s\n",
                mfr_common, (mfr_common & LTM4676A_MFR_COMMON_WP_PIN_HIGH_MASK) != 0 ? "ACTIVE" : "INACTIVE");
    }

    return status;
}


/**
 * @brief Dump information for one DCDC LTM4676A
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address The I2C address of the LTM4676A to dump the information for
 */
void dump_ltm4676a_information (bit_banged_i2c_controller_context_t *const controller, const uint8_t i2c_slave_address)
{
    smbus_transfer_status_t status;

    printf ("\nLTM4676A at I2C address 0x%02x\n", i2c_slave_address);

    /* First check can read the PMBus capability and revision */
    status = report_pmbus_capability_and_revision (controller, i2c_slave_address);

    /* Report the ID and model, as an initial check of a variable length BLOCK READ */
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        status = report_pmbus_id_and_model (controller, i2c_slave_address);
    }

    /* Report write protect status */
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        report_ltm4676a_write_protect (controller, i2c_slave_address);
    }

    /* Obtain sensor readings */
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        status = read_pmbus_sensors (controller, i2c_slave_address, LTM4676A_NUM_PAGES, LTM4676A_NUM_SENSORS,
                ltm4676a_sensor_definitions, ltm4676a_sensor_readings);
    }

    /* Display the sensor readings */
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        display_pmbus_sensors (LTM4676A_NUM_PAGES, LTM4676A_NUM_SENSORS, ltm4676a_sensor_definitions, ltm4676a_sensor_readings);
    }

    if (status != SMBUS_TRANSFER_SUCCESS)
    {
        report_pmbus_transfer_failure (controller, status);
    }
}
