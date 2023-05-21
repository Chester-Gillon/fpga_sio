/*
 * @file pmbus_access.c
 * @date 21 May 2023
 * @author Chester Gillon
 * @brief Provides an interface to access devices via PMBus
 * @details
 *   The following references were used:
 *      https://pmbusprod.wpenginepowered.com/wp-content/uploads/2021/05/PMBus-Specification-Rev-1-2-Part-I-20100906.pdf
 *        Part I - General Requirements, Transport And Electrical Interface
 *      https://pmbusprod.wpenginepowered.com/wp-content/uploads/2021/05/PMBus-Specification-Rev-1-2-Part-II-20100906.pdf
 *        Part II - Command Language
 *
 *   Where the above are for PMBus Version 1.2, which the revision supported by the LTM4676A which this library was
 *   first written to support.
 */

#include "pmbus_access.h"

#include <stdio.h>


/**
 * @brief Read the PMBus capability for a PMBus device.
 * @details If PEC is supported then enable PEC for subsequent messages for the PMBus device
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address of the PMBus device
 * @param[out] capability The capability byte read from the PMBus device.
 * @return Indicates if the capability was read successfully or not.
 */
smbus_transfer_status_t read_pmbus_capability (bit_banged_i2c_controller_context_t *const controller,
                                               const uint8_t i2c_slave_address, uint8_t *const capability)
{
    smbus_transfer_status_t status;

    status = bit_banged_smbus_read (controller, i2c_slave_address, PMBUS_COMMAND_CAPABILITY, sizeof (*capability), capability);
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        if ((*capability & PMBUS_CAPABILITY_PEC_SUPPORTED_MASK) != 0)
        {
            bit_banged_smbus_enable_pec (controller, i2c_slave_address);
        }
    }

    return status;
}


/**
 * @brief Report the PBMus capability and revision for a PMBus device
 * @details The PMBus capability is used to enable PEC if supported, but apart from that the capability and revision
 *          are only displayed as diagnostic information.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address of the PMBus device
 * @return Indicates if the capability and revision were read successfully or not.
 */
smbus_transfer_status_t report_pmbus_capability_and_revision (bit_banged_i2c_controller_context_t *const controller,
                                                              const uint8_t i2c_slave_address)
{
    smbus_transfer_status_t status;
    uint8_t capability;
    uint8_t pmbus_revision;

    /* Read capability first, as that enables PEC if supported */
    status = read_pmbus_capability (controller, i2c_slave_address, &capability);

    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        /* If PEC is supported, this is the first test that can calculate the PEC successfully */
        status = bit_banged_smbus_read (controller, i2c_slave_address, PMBUS_COMMAND_PMBUS_REVISION,
                sizeof (pmbus_revision), &pmbus_revision);
    }

    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        const char *max_bus_speed;

        switch (capability & PMBUS_CAPABILITY_MAX_BUS_SPEED_MASK)
        {
        case PMBUS_CAPABILITY_MAX_BUS_SPEED_100_kHz:
            max_bus_speed = "100 kHz";
            break;

        case PMBUS_CAPABILITY_MAX_BUS_SPEED_400_kHz:
            max_bus_speed = "400 kHz";
            break;

        default:
            max_bus_speed = "unknown";
            break;
        }

        printf ("  PMBus capability=0x%02x :%s  Max bus speed=%s%s\n",
                capability,
                (capability & PMBUS_CAPABILITY_PEC_SUPPORTED_MASK) != 0 ? " PEC supported" : "",
                max_bus_speed,
                (capability & PMBUS_CAPABILITY_SMBALERT_SUPPORTED_MASK) != 0 ? "  SMBALERT# supported" : "");

        char *part_I_revision;
        char *part_II_revision;

        switch ((pmbus_revision & 0xf0) >> 4)
        {
        case 0: part_I_revision = "1.0"; break;
        case 1: part_I_revision = "1.1"; break;
        case 2: part_I_revision = "1.2"; break;
        default: part_I_revision = "unknown"; break;
        }

        switch (pmbus_revision & 0xf)
        {
        case 0: part_II_revision = "1.0"; break;
        case 1: part_II_revision = "1.1"; break;
        case 2: part_II_revision = "1.2"; break;
        default: part_II_revision = "unknown"; break;
        }

        printf ("  PMBus revision=0x%02x : Part I revision %s  Part II revision %s\n",
                pmbus_revision, part_I_revision, part_II_revision);
    }

    return status;
}


/**
 * @brief Called after a PMBus message transfer has failed to report diagnostic information
 * @param[in] controller The controller for the GPIO bit-banged interface, which contains information about the failed transfer
 * @param[in] status The failed transfer status
 */
void report_pmbus_transfer_failure (const bit_banged_i2c_controller_context_t *const controller,
                                    const smbus_transfer_status_t status)
{
    printf ("  PMBus command 0x%02x failed due to %s", controller->last_smbus_command_code,
            smbus_transfer_status_descriptions[status]);
    switch (status)
    {
    case SMBUS_TRANSFER_READ_INCORRECT_PEC:
        printf ("  actual PEC byte=0x%02x  expected PEC byte=0x%02x\n",
                controller->smbus_actual_pec_byte, controller->smbus_expected_pec_byte);
        break;

    case SMBUS_TRANSFER_INVALID_BLOCK_BYTE_COUNT:
        printf ("  block byte count=%u\n", controller->last_smbus_block_byte_count);
        break;

    default:
        /* No supplementary information */
        printf ("\n");
        break;
    }
}


/**
 * @brief Report the manufacturer ID and model for a PMBus device, which are formatted as variable length ASCII strings
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address of the PMBus device
 * @return Indicates if the ID and model were read successfully or not.
 */
smbus_transfer_status_t report_pmbus_id_and_model (bit_banged_i2c_controller_context_t *const controller,
                                                   const uint8_t i2c_slave_address)
{
    smbus_transfer_status_t status;
    char mfr_id[255];
    char mfr_model[255];
    size_t mfr_id_len = 0;
    size_t mfr_model_len = 0;

    status = bit_banged_smbus_block_read (controller, i2c_slave_address, PMBUS_COMMAND_MFR_ID,
            sizeof (mfr_id), (uint8_t *) mfr_id, &mfr_id_len);
    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        status = bit_banged_smbus_block_read (controller, i2c_slave_address, PMBUS_COMMAND_MFR_MODEL,
                sizeof (mfr_model), (uint8_t *) mfr_model, &mfr_model_len);
    }

    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        printf ("  MFR_ID=%.*s  MFR_MODEL=%.*s\n", (int) mfr_id_len, mfr_id, (int) mfr_model_len, mfr_model);
    }

    return status;
}
