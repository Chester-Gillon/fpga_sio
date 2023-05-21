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
        case 0x40: write_protect = "Disable all writes except to the WRITE_PROTECT, PAGE, MFR_EE_UNLOCK, MFR_CLEAR_PEAKS, STORE_USER_ALL, OPERATION and CLEAR_FAULTS command. individual fault bits can be cleared by writing a 1 to the respective bits in the STATUS registers."; break;
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

    if (status != SMBUS_TRANSFER_SUCCESS)
    {
        report_pmbus_transfer_failure (controller, status);
    }
}
