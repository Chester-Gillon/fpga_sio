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

#include <math.h>
#include <string.h>
#include <stdio.h>


/**
 * @brief Read the data for a PMBus command from a specific page
 * @details Uses the PAGE_PLUS_READ PMBus command to perform the read using a single SMBus message transfer
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address of the PMBus device
 * @param[in] page_number The page number to read from
 * @param[in] read_command_code The PMBus command code for the read
 * @param[in] data_size_bytes The number of bytes to read
 * @param[out] data The data which has been read.
 * @return Indicates if the read was successful or not.
 */
smbus_transfer_status_t pmbus_paged_read (bit_banged_i2c_controller_context_t *const controller,
                                          const uint8_t i2c_slave_address,
                                          const uint8_t page_number,
                                          const uint8_t read_command_code,
                                          const size_t data_size_bytes,
                                          void *const data)
{
    smbus_transfer_status_t status;
    size_t read_actual_block_count;
    const uint8_t write_block[] = {page_number, read_command_code};

    status = bit_banged_smbus_block_write_block_read_process_call (controller, i2c_slave_address, PMBUS_COMMAND_PAGE_PLUS_READ,
            sizeof (write_block), write_block, data_size_bytes, data, &read_actual_block_count);
    if ((status == SMBUS_TRANSFER_SUCCESS) && (read_actual_block_count != data_size_bytes))
    {
        /* While the SMBUS BLOCK WRITE - BLOCK READ PROCESS CALL returns a variable block count, report an error
         * if the actual read block count doesn't match the size of the data as this function is only expected to be
         * used with a read_command_code which returns a fixed amount of data. */
        status = SMBUS_TRANSFER_INVALID_BLOCK_BYTE_COUNT;
    }

    return status;
}


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


/**
 * @brief Extract a PMBus two-complement field, with the result in 32-bit signed integer
 * @param[in] word The word read from a PMBus device to extract the field from
 * @param[in] field_width_bits The width of the field in bits
 * @param[in] field_lsb The least significant bit of the field
 * @return The extracted field value
 */
static inline int32_t pmbus_extract_twos_complement (const uint32_t word,
                                                     const uint32_t field_width_bits, const uint32_t field_lsb)
{
    const uint32_t field_mask = (1u << field_width_bits) - 1u;
    const uint32_t field_msb_mask = 1u << (field_width_bits - 1u);
    const uint32_t sign_extend_mask = ~field_mask;
    uint32_t unsigned_value = (word >> field_lsb) & field_mask;

    if ((unsigned_value & field_msb_mask) != 0)
    {
        /* Sign extend the field */
        unsigned_value |= sign_extend_mask;
    }

    return (int32_t) unsigned_value;
}


/**
 * @brief Read the sensor readings from a PMBus device
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address 7-bit slave address of the PMBus device
 * @param[in] num_pages The number of pages in the PMBus device, for sensors which are per-page
 * @param[in] num_sensors The number of sensors to read in the PMBus device
 * @param[in] sensor_definitions The definitions of the sensors to read.
 * @param[out] sensor_readings The sensor readings obtained from the PMBus device
 * @return Returns the overall status of reading the sensors:
 *         - SMBUS_TRANSFER_SUCCESS means all sensor values were read.
 *         - Any other value indicates the reading of the sensors values was aborted due to a SMBus error.
 *           The number of successfully read sensors before the error is not reported.
 */
smbus_transfer_status_t read_pmbus_sensors (bit_banged_i2c_controller_context_t *const controller,
                                            const uint8_t i2c_slave_address,
                                            const size_t num_pages,
                                            const size_t num_sensors,
                                            const pmbus_sensor_definition_t sensor_definitions[const num_sensors],
                                            pmbus_sensor_reading_t sensor_readings[const num_sensors])
{
    smbus_transfer_status_t status = SMBUS_TRANSFER_SUCCESS;
    uint8_t vout_mode_byte;
    double vout_mode_scalings[PMBUS_MAX_PAGES];
    uint8_t page_number;
    size_t sensor_index;

    /* First read the VOUT_MODE setting for each page, to be able to scale PMBUS_SENSOR_FORMAT_LINEAR_16U sensors */
    for (page_number = 0; (status == SMBUS_TRANSFER_SUCCESS) && (page_number < num_pages); page_number++)
    {
        status = pmbus_paged_read (controller, i2c_slave_address, page_number, PMBUS_COMMAND_VOUT_MODE,
                sizeof (vout_mode_byte), &vout_mode_byte);
        if (status == SMBUS_TRANSFER_SUCCESS)
        {
            const uint32_t mode = vout_mode_byte >> 5;
            const uint32_t linear_mode = 0;

            if (mode == linear_mode)
            {
                const int32_t vout_mode_exponent = pmbus_extract_twos_complement (vout_mode_byte, 5, 0);
                vout_mode_scalings[page_number] = pow (2.0, (double) vout_mode_exponent);
            }
            else
            {
                /* This function has only been written to support linear VOUT_MODE */
                printf ("Unsupported vout_mode byte 0x%02x\n", vout_mode_byte);
                status = SMBUS_TRANSFER_INVALID_BLOCK_BYTE_COUNT;
            }
        }
    }

    /* Read the raw values from all the sensors */
    for (sensor_index = 0; (status == SMBUS_TRANSFER_SUCCESS) && (sensor_index < num_sensors); sensor_index++)
    {
        const pmbus_sensor_definition_t *const definition = &sensor_definitions[sensor_index];
        pmbus_sensor_reading_t *const reading = &sensor_readings[sensor_index];

        if (definition->paged)
        {
            for (page_number = 0; (status == SMBUS_TRANSFER_SUCCESS) && (page_number < num_pages); page_number++)
            {
                status = pmbus_paged_read (controller, i2c_slave_address, page_number, definition->command_code,
                        sizeof (reading->raw_sensor_values[page_number]), &reading->raw_sensor_values[page_number]);
            }
        }
        else
        {
            status = bit_banged_smbus_read (controller, i2c_slave_address, definition->command_code,
                    sizeof (reading->raw_sensor_values[0]), (uint8_t *) &reading->raw_sensor_values[0]);
        }
    }

    if (status == SMBUS_TRANSFER_SUCCESS)
    {
        /* Scale the raw sensor values */
        for (sensor_index = 0; sensor_index < num_sensors; sensor_index++)
        {
            const pmbus_sensor_definition_t *const definition = &sensor_definitions[sensor_index];
            pmbus_sensor_reading_t *const reading = &sensor_readings[sensor_index];
            const size_t num_populated_readings = definition->paged ? num_pages : 1;

            for (page_number = 0; page_number < num_populated_readings; page_number++)
            {
                const uint16_t raw_sensor_value = reading->raw_sensor_values[page_number];

                switch (definition->sensor_format)
                {
                case PMBUS_SENSOR_FORMAT_LINEAR_5S_11S:
                    {
                        const int32_t exponent = pmbus_extract_twos_complement (raw_sensor_value, 5, 11);
                        const int32_t mantissa = pmbus_extract_twos_complement (raw_sensor_value, 11, 0);

                        reading->scaled_sensor_values[page_number] = (double) mantissa * pow (2.0, (double) exponent);
                    }
                    break;

                case PMBUS_SENSOR_FORMAT_LINEAR_16U:
                    reading->scaled_sensor_values[page_number] = (double) raw_sensor_value * vout_mode_scalings[page_number];
                    break;
                }
            }
        }
    }

    return status;
}


/**
 * @brief Display the PMBus sensor values read by read_pmbus_sensors() to standard out
 * @param[in] num_pages The number of pages in the PMBus device, for sensors which are per-page
 * @param[in] num_sensors The number of sensors to read in the PMBus device
 * @param[in] sensor_definitions The definitions of the sensors to read.
 * @param[in] sensor_readings The sensor readings to display
 */
void display_pmbus_sensors (const size_t num_pages,
                            const size_t num_sensors,
                            const pmbus_sensor_definition_t sensor_definitions[const num_sensors],
                            const pmbus_sensor_reading_t sensor_readings[const num_sensors])
{
    uint8_t page_number;
    size_t sensor_index;
    size_t max_name_len;
    const int page_width = 7;

    /* Find the maximum name length for aligning the output */
    max_name_len = 0;
    for (sensor_index = 0; sensor_index < num_sensors; sensor_index++)
    {
        const size_t name_len = strlen (sensor_definitions[sensor_index].name);

        if (name_len > max_name_len)
        {
            max_name_len = name_len;
        }
    }

    /* First display all sensor values which are not paged */
    for (sensor_index = 0; sensor_index < num_sensors; sensor_index++)
    {
        const pmbus_sensor_definition_t *const definition = &sensor_definitions[sensor_index];
        const pmbus_sensor_reading_t *const reading = &sensor_readings[sensor_index];

        if (!definition->paged)
        {
            printf ("  %-*s: %7.3f %s\n",
                    (int) max_name_len + page_width, definition->name, reading->scaled_sensor_values[0], definition->units);
        }
    }

    /* Then display the sensor values which are paged, all page 0, then all page 1 ... */
    for (page_number = 0; page_number < num_pages; page_number++)
    {
        for (sensor_index = 0; sensor_index < num_sensors; sensor_index++)
        {
            const pmbus_sensor_definition_t *const definition = &sensor_definitions[sensor_index];
            const pmbus_sensor_reading_t *const reading = &sensor_readings[sensor_index];

            if (definition->paged)
            {
                printf ("  %-*s page %u: %7.3f %s\n",
                        (int) max_name_len, definition->name, page_number,
                        reading->scaled_sensor_values[page_number], definition->units);
            }
        }
    }
}
