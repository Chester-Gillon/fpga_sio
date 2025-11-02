/*
 * @file xilinx_cms.c
 * @date Oct 25, 2025
 * @author Chester Gillon
 * @brief Implements a mechanism to access the Xilinx Card Management Solution Subsystem (CMS Subsystem) via VFIO
 * @details
 *   https://docs.amd.com/r/en-US/pg348-cms-subsystem documents the CMS Subsystem.
 */

#include "xilinx_cms.h"
#include "xilinx_cms_host_interface.h"
#include "generic_pci_access.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/* The integer encodings in the PROFILE_NAME_REG for each software profile */
static uint32_t cms_software_profile_encodings[CMS_SOFTWARE_PROFILE_ARRAY_SIZE] =
{
    [CMS_SOFTWARE_PROFILE_U200_U250] = 0x55325858,
    [CMS_SOFTWARE_PROFILE_U280     ] = 0x55323830,
    [CMS_SOFTWARE_PROFILE_U50      ] = 0x55353041,
    [CMS_SOFTWARE_PROFILE_U55      ] = 0x5535354E,
    [CMS_SOFTWARE_PROFILE_UL3524   ] = 0x55333234,
    [CMS_SOFTWARE_PROFILE_U45N     ] = 0x55323641,
    [CMS_SOFTWARE_PROFILE_X3       ] = 0x58334100,
    [CMS_SOFTWARE_PROFILE_UL3422   ] = 0x55333432
};


/* The display names for the software profiles */
const char *const cms_software_profile_names[CMS_SOFTWARE_PROFILE_ARRAY_SIZE] =
{
    [CMS_SOFTWARE_PROFILE_U200_U250] = "U200/U250",
    [CMS_SOFTWARE_PROFILE_U280     ] = "U280",
    [CMS_SOFTWARE_PROFILE_U50      ] = "U50",
    [CMS_SOFTWARE_PROFILE_U55      ] = "U55",
    [CMS_SOFTWARE_PROFILE_UL3524   ] = "UL3524",
    [CMS_SOFTWARE_PROFILE_U45N     ] = "U45N",
    [CMS_SOFTWARE_PROFILE_X3       ] = "X3",
    [CMS_SOFTWARE_PROFILE_UL3422   ] = "UL3422"
};


/* The number of QSFP modules for each software profile */
const uint32_t cms_num_qsfp_modules[CMS_SOFTWARE_PROFILE_ARRAY_SIZE] =
{
    [CMS_SOFTWARE_PROFILE_U200_U250] = 2,
    [CMS_SOFTWARE_PROFILE_U280     ] = 2,
    [CMS_SOFTWARE_PROFILE_U50      ] = 1,
    [CMS_SOFTWARE_PROFILE_U55      ] = 2,
    [CMS_SOFTWARE_PROFILE_U45N     ] = 2
};


/* Description of errors in HOST_MSG_ERROR_REG */
static const char *const cms_host_msg_error_reg_names[] =
{
   [0x0] = "CMS_HOST_MSG_NO_ERR",
   [0x1] = "CMS_HOST_MSG_BAD_OPCODE_ERR",
   [0x2] = "CMS_HOST_MSG_BRD_INFO_MISSING_ERR",
   [0x3] = "CMS_HOST_MSG_LENGTH_ERR",
   [0x4] = "CMS_HOST_MSG_SAT_FW_WRITE_FAIL",
   [0x5] = "CMS_HOST_MSG_SAT_FW_UPDATE_FAIL",
   [0x6] = "CMS_HOST_MSG_SAT_FW_LOAD_FAIL",
   [0x7] = "CMS_HOST_MSG_SAT_FW_ERASE_FAIL",
   [0x9] = "CMS_HOST_MSG_CSDR_FAILED",
   [0xA] = "CMS_HOST_MSG_QSFP_FAIL"
};
static uint32_t cms_num_host_msg_error_reg_names = sizeof (cms_host_msg_error_reg_names) / sizeof (cms_host_msg_error_reg_names[0]);


/* The key values for the card information sensors */
static uint8_t cms_snsr_id_keys[CMS_SNSR_ID_ARRAY_SIZE] =
{
    [CMS_SNSR_ID_CARD_SN          ] = 0x21,
    [CMS_SNSR_ID_MAC_ADDRESS0     ] = 0x22,
    [CMS_SNSR_ID_MAC_ADDRESS1     ] = 0x23,
    [CMS_SNSR_ID_MAC_ADDRESS2     ] = 0x24,
    [CMS_SNSR_ID_MAC_ADDRESS3     ] = 0x25,
    [CMS_SNSR_ID_CARD_REV         ] = 0x26,
    [CMS_SNSR_ID_CARD_NAME        ] = 0x27,
    [CMS_SNSR_ID_SAT_VERSION      ] = 0x28,
    [CMS_SNSR_ID_TOTAL_POWER_AVAIL] = 0x29,
    [CMS_SNSR_ID_FAN_PRESENCE     ] = 0x2A,
    [CMS_SNSR_ID_CONFIG_MODE      ] = 0x2B,
    [CMS_SNSR_ID_NEW_MAC_SCHEME   ] = 0x4B,
    [CMS_SNSR_ID_CAGE_TYPE_00     ] = 0x50,
    [CMS_SNSR_ID_CAGE_TYPE_01     ] = 0x51,
    [CMS_SNSR_ID_CAGE_TYPE_02     ] = 0x52,
    [CMS_SNSR_ID_CAGE_TYPE_03     ] = 0x53
};


/* The display names for the card information sensors */
static const char *const cms_snsr_id_names[CMS_SNSR_ID_ARRAY_SIZE] =
{
    [CMS_SNSR_ID_CARD_SN          ] = "Card S/N             ",
    [CMS_SNSR_ID_MAC_ADDRESS0     ] = "MAC address 0        ",
    [CMS_SNSR_ID_MAC_ADDRESS1     ] = "MAC address 1        ",
    [CMS_SNSR_ID_MAC_ADDRESS2     ] = "MAC address 2        ",
    [CMS_SNSR_ID_MAC_ADDRESS3     ] = "MAC address 3        ",
    [CMS_SNSR_ID_CARD_REV         ] = "Card revision        ",
    [CMS_SNSR_ID_CARD_NAME        ] = "Card name            ",
    [CMS_SNSR_ID_SAT_VERSION      ] = "Satellite version    ",
    [CMS_SNSR_ID_TOTAL_POWER_AVAIL] = "Total power available",
    [CMS_SNSR_ID_FAN_PRESENCE     ] = "Fan presence         ",
    [CMS_SNSR_ID_CONFIG_MODE      ] = "Config mode          ",
    [CMS_SNSR_ID_NEW_MAC_SCHEME   ] = "New MAC scheme       ",
    [CMS_SNSR_ID_CAGE_TYPE_00     ] = "Cage type 0          ",
    [CMS_SNSR_ID_CAGE_TYPE_01     ] = "Cage type 1          ",
    [CMS_SNSR_ID_CAGE_TYPE_02     ] = "Cage type 2          ",
    [CMS_SNSR_ID_CAGE_TYPE_03     ] = "Cage type 3          "
};


/**
 * @brief Start a timeout for a CMS operation
 * @details
 *   Uses a fixed 10 second timeout.
 *   The CMS documentation doesn't seem to define the expected time for the firmware to react to a request.
 * @param[in,out] context Context to start the timeout for
 */
static void cms_start_timeout (xilinx_cms_context_t *const context)
{
    clock_gettime (CLOCK_MONOTONIC, &context->cms_timeout);
    context->cms_timeout.tv_sec += 10;
}


/**
 * @brief Check if a timeout started by a previous call to cms_start_timeout() as expired
 * @param[in,out] context The context to check for a timeout.
 * @return Returns true if the timeout has expired, or false otherwise.
 */
static bool cms_check_for_timeout (xilinx_cms_context_t *const context)
{
    struct timespec now;
    bool timed_out;

    clock_gettime (CLOCK_MONOTONIC, &now);
    timed_out = (now.tv_sec > context->cms_timeout.tv_sec) ||
            ((now.tv_sec == context->cms_timeout.tv_sec) && (now.tv_nsec > context->cms_timeout.tv_nsec));

    if (!timed_out)
    {
        /* If the timeout hasn't expired, delay with a hold-off before allowing the caller to retry.
         * This is because checking for completion involves polling memory shared with the CMS firmware.
         * Therefore, polling the shared memory in a tight loop could potentially block the CMS firmware. */
        const struct timespec holdoff_delay =
        {
            .tv_sec = 0,
            .tv_nsec = 100000
        };

        nanosleep (&holdoff_delay, NULL);
    }

    return timed_out;
}


/*
 * @brief Perform one CMS mailbox transaction
 * @details Sends a request to the CMS, and waits for the response
 * @param[in,out] context The context to use for the transaction
 * @param[in,out] transaction The transaction to perform.
 * @return Returns true if the transaction was successful, or false otherwise
 */
bool cms_mailbox_transaction (xilinx_cms_context_t *const context, cms_mailbox_t *const transaction)
{
    uint32_t control_reg;
    uint32_t word_index;

    /* Check for availability of the mailbox */
    control_reg = read_reg32 (context->host_cms_shared_memory, CMS_CONTROL_REG_OFFSET);
    if ((control_reg & CMS_CONTROL_REG_MAILBOX_MESSAGE_STATUS) != 0)
    {
        printf ("Mailbox is busy at start of transaction (control_reg=0x%08x)\n", control_reg);
        return false;
    }

    /* Write the request to the mailbox */
    const size_t request_payload_size_bytes = ((transaction->request_fixed_size) ?
            transaction->request_payload_size_bytes :
            generic_pci_access_extract_field (transaction->header, CMS_MAILBOX_HEADER_LENGTH_BYTES_MASK));
    const size_t request_payload_size_words = (request_payload_size_bytes + (sizeof (uint32_t) - 1u)) / sizeof (uint32_t);

    write_reg32 (context->cms_mailbox_header, 0, transaction->header);
    for (word_index = 0; word_index < request_payload_size_words; word_index++)
    {
        write_reg32 (context->cms_mailbox_payload, word_index * sizeof (uint32_t), transaction->payload.words[word_index]);
    }

    /* Notify the CMS of the request */
    control_reg |= CMS_CONTROL_REG_MAILBOX_MESSAGE_STATUS;
    write_reg32 (context->host_cms_shared_memory, CMS_CONTROL_REG_OFFSET, control_reg);

    /* Wait for CMS response, indicated by the message status bit clearing */
    bool response_available = false;
    cms_start_timeout (context);
    do
    {
        control_reg = read_reg32 (context->host_cms_shared_memory, CMS_CONTROL_REG_OFFSET);
        if ((control_reg & CMS_CONTROL_REG_MAILBOX_MESSAGE_STATUS) == 0)
        {
            response_available = true;
        }
        else if (cms_check_for_timeout (context))
        {
            printf ("Timeout waiting for CMS mailbox response for header=0x%08X\n", transaction->header);
            return false;
        }
    } while (!response_available);

    /* Check if the transaction completed without error */
    transaction->host_msg_error_reg = read_reg32 (context->host_cms_shared_memory, CMS_HOST_MSG_ERROR_REG_OFFSET);
    if (transaction->host_msg_error_reg != 0)
    {
        printf ("CMS mailbox for header=0x%08X failed with ", transaction->header);
        if ((transaction->host_msg_error_reg < cms_num_host_msg_error_reg_names) &&
            (cms_host_msg_error_reg_names[transaction->host_msg_error_reg]) != NULL)
        {
            printf ("error %s\n", cms_host_msg_error_reg_names[transaction->host_msg_error_reg]);
        }
        else
        {
            printf ("unknown error 0x%08X\n", transaction->host_msg_error_reg);
        }
        return false;
    }

    /* Copy the response from the mailbox */
    transaction->header = read_reg32 (context->cms_mailbox_header, 0);
    transaction->response_payload_size_bytes =
            ((transaction->response_fixed_size) ?
            transaction->response_payload_size_bytes :
            generic_pci_access_extract_field (transaction->header, CMS_MAILBOX_HEADER_LENGTH_BYTES_MASK));
    const size_t response_payload_size_words =
            (transaction->response_payload_size_bytes + (sizeof (uint32_t) - 1u)) / sizeof (uint32_t);

    for (word_index = 0; word_index < response_payload_size_words; word_index++)
    {
        transaction->payload.words[word_index] = read_reg32 (context->cms_mailbox_payload, word_index * sizeof (uint32_t));
    }

    return true;
}


/**
 * @brief Initialise host access to the CMS Subsystem
 * @details This includes taking the CMS Subsystem out of reset.
 *          The description of MB_RESETN_REG contains:
 *            "Note: Following power-up or assertion of aresetn_ctrl, MB_RESETN_REG will be reset to 0x0 placing
 *             the MicroBlaze subsystem into the reset state. Driver firmware will be required to write 0x1 to this register
 *             to take the MicroBlaze Subsystem out of reset and start CMS Firmware."
 * @param context[out] The initialised context
 * @param vfio_device[in/out] The VFIO device which contains the CMS Subsystem to access.
 * @param cms_subsystem_bar_index[in] Which BAR the CMS registers are mapped to.
 * @param cms_subsystem_base_offset[in] The base offset of the CMS registers.
 * @return Returns true if the have successfully initialised host access to the CMS Subsystem
 */
bool cms_initialise_access (xilinx_cms_context_t *const context,
                            vfio_device_t *const vfio_device,
                            const uint32_t cms_subsystem_bar_index, const size_t cms_subsystem_base_offset)
{
    uint32_t status2_value;
    bool success = false;

    memset (context, 0, sizeof (*context));

    /* Map the registers */
    const size_t microblaze_reset_register_base_offset = cms_subsystem_base_offset + 0x020000;
    const size_t microblaze_reset_register_frame_size  = 0x4;
    const size_t host_interrupt_controller_base_offset = cms_subsystem_base_offset + 0x022000;
    const size_t host_interrupt_controller_frame_size  = 0x1000;
    const size_t host_cms_shared_memory_base_offset    = cms_subsystem_base_offset + 0x028000;
    const size_t host_cms_shared_memory_frame_size     = 0x2000;
    context->microblaze_reset_register = map_vfio_registers_block (vfio_device, cms_subsystem_bar_index,
            microblaze_reset_register_base_offset, microblaze_reset_register_frame_size);
    context->host_interrupt_controller = map_vfio_registers_block (vfio_device, cms_subsystem_bar_index,
            host_interrupt_controller_base_offset, host_interrupt_controller_frame_size);
    context->host_cms_shared_memory    = map_vfio_registers_block (vfio_device, cms_subsystem_bar_index,
            host_cms_shared_memory_base_offset, host_cms_shared_memory_frame_size);

    if ((context->microblaze_reset_register != NULL) &&
        (context->host_interrupt_controller != NULL) &&
        (context->host_cms_shared_memory != NULL))
    {
        /* If CMS Subsystem is held in reset, de-assert reset */
        const uint32_t reset_register = read_reg32 (context->microblaze_reset_register, 0);
        if (reset_register == 0)
        {
            /* When the CMS Subsystem reset is asserted following having previously being used, the REG_MAP ready bit doesn't
             * seem to be cleared by the reset.
             *
             * Write to the HOST_STATUS2_REG to clear the REG_MAP ready bit while the reset is still asserted.
             *
             * That means once the reset is de-asserted the REG_MAP ready won't be set until the CMS firmware initialisation has
             * completed.
             *
             * While PG348 indicates HOST_STATUS2_REG is read-only, with a U200 are able to modify the register.
             *
             * Without this clearing of REG_MAP ready it was possible to sample as ready before the CMS firmware had re-initialised
             * and the card information was read as all empty. */
            status2_value = read_reg32 (context->host_cms_shared_memory, CMS_HOST_STATUS2_REG_OFFSET);
            if ((status2_value & CMS_REG_MAP_READY_MASK) == CMS_REG_MAP_READY_MASK)
            {
                write_reg32 (context->host_cms_shared_memory, CMS_HOST_STATUS2_REG_OFFSET, 0);
            }

            /* Read back to ensure the write has been posted */
            status2_value = read_reg32 (context->host_cms_shared_memory, CMS_HOST_STATUS2_REG_OFFSET);

            /* Now de-assert reset */
            write_reg32 (context->microblaze_reset_register, 0, 0x1);
        }

        /* Wait for the CMS REG_MAP to be ready. */
        cms_start_timeout (context);
        bool ready = false;
        do
        {
            status2_value = read_reg32 (context->host_cms_shared_memory, CMS_HOST_STATUS2_REG_OFFSET);
            if ((status2_value & CMS_REG_MAP_READY_MASK) == CMS_REG_MAP_READY_MASK)
            {
                ready = true;
            }
            else if (cms_check_for_timeout (context))
            {
                /* As diagnostic information report the HOST_INTC Interrupt Status Register value in case a watchdog timeout
                 * is indicated. */
                const uint32_t isr_value = read_reg32 (context->host_interrupt_controller, 0);

                printf ("Timeout waiting for CMS REG_MAP to be ready (ISR=0x%08X)\n", isr_value);
                return false;
            }
        } while (!ready);

        /* Validate that the Register map ID has the expected value */
        const uint32_t reg_map_id = read_reg32 (context->host_cms_shared_memory, CMS_REG_MAP_ID_REG_OFFSET);
        if (reg_map_id != CMS_EXPECTED_REG_MAP_ID)
        {
            printf ("Actual REF_MAP_ID 0x%08X != expected value 0x%08X\n",
                    reg_map_id, CMS_EXPECTED_REG_MAP_ID);
            return false;
        }

        /* Get the software profile */
        const uint32_t profile_name_reg = read_reg32 (context->host_cms_shared_memory, CMS_PROFILE_NAME_REG_OFFSET);
        bool profile_identified = false;
        context->software_profile = 0;
        while ((!profile_identified) && (context->software_profile < CMS_SOFTWARE_PROFILE_ARRAY_SIZE))
        {
            if (profile_name_reg == cms_software_profile_encodings[context->software_profile])
            {
                profile_identified = true;
            }
            else
            {
                context->software_profile++;
            }
        }
        if (!profile_identified)
        {
            printf ("Unknown PROFILE_NAME_REG=0x%08X\n", profile_name_reg);
            return false;
        }

        /* Map the CMS mailbox */
        const uint32_t mailbox_offset_reg = read_reg32 (context->host_cms_shared_memory, CMS_HOST_MSG_OFFSET_REG_OFFSET);
        const size_t mailbox_end = mailbox_offset_reg + CMS_MAILBOX_FRAME_SIZE_BYTES;
        if (mailbox_end > host_cms_shared_memory_frame_size)
        {
            printf ("mailbox offset 0x%08x places outside of the frame\n", mailbox_offset_reg);
        }
        context->cms_mailbox_header = &context->host_cms_shared_memory[mailbox_offset_reg];
        context->cms_mailbox_payload = &context->host_cms_shared_memory[mailbox_offset_reg + CMS_MAILBOX_PAYLOAD_START_OFFSET];

        /* Enable card specific features */
        const uint32_t current_control_reg = read_reg32 (context->host_cms_shared_memory, CMS_CONTROL_REG_OFFSET);
        uint32_t new_control_reg = current_control_reg;
        switch (context->software_profile)
        {
        case CMS_SOFTWARE_PROFILE_U280:
        case CMS_SOFTWARE_PROFILE_U50:
        case CMS_SOFTWARE_PROFILE_U55:
            new_control_reg |= CMS_CONTROL_REG_HBM_TEMPERATURE_MONITORING;
            break;

        case CMS_SOFTWARE_PROFILE_U200_U250:
            new_control_reg |= CMS_CONTROL_REG_QSFP_GPIO_ENABLE;
            break;

        default:
            /* No card specific features to enable */
            break;
        }
        if (new_control_reg != current_control_reg)
        {
            write_reg32 (context->host_cms_shared_memory, CMS_CONTROL_REG_OFFSET, new_control_reg);
        }

        /* Get the card information */
        context->card_information_mailbox.request_fixed_size = true;
        context->card_information_mailbox.request_payload_size_bytes = 0u;
        context->card_information_mailbox.response_fixed_size = false;
        generic_pci_access_update_field (&context->card_information_mailbox.header,
                CMS_MAILBOX_HEADER_OPCODE_MASK, CMS_OP_CARD_INFO_REQ_OPCODE);
        success = cms_mailbox_transaction (context, &context->card_information_mailbox);
        if (!success)
        {
            return false;
        }

        /* Index the card information */
        size_t response_payload_offset = 0;
        while (response_payload_offset < context->card_information_mailbox.response_payload_size_bytes)
        {
            /* Lookup the sensor for the key value */
            const uint8_t key = context->card_information_mailbox.payload.bytes[response_payload_offset++];
            cms_card_information_sensor_t *sensor = NULL;
            for (cms_snsr_id_t sensor_id = 0; (sensor == NULL) && (sensor_id < CMS_SNSR_ID_ARRAY_SIZE); sensor_id++)
            {
                if (key == cms_snsr_id_keys[sensor_id])
                {
                    sensor = &context->card_information_sensors[sensor_id];
                }
            }

            if (sensor == NULL)
            {
                printf ("Card information sensor with unknown key 0x%x\n", key);
                return false;
            }
            else if (sensor->data != NULL)
            {
                printf ("Card information sensor key 0x%x defined more than once\n", key);
                return false;
            }

            /* Store the length and data for the sensor */
            sensor->data_len = context->card_information_mailbox.payload.bytes[response_payload_offset++];
            sensor->data = &context->card_information_mailbox.payload.bytes[response_payload_offset];
            response_payload_offset += sensor->data_len;

            if (response_payload_offset > context->card_information_mailbox.response_payload_size_bytes)
            {
                printf ("response_payload_offset %zu off end of response payload size %u\n",
                        response_payload_offset, context->card_information_mailbox.request_payload_size_bytes);
                return false;
            }
        }
    }

    return success;
}


/**
 * @brief Read the low speed I/O signals for one QSFP module
 * @param[in,out] context The context to use for the read
 * @param[in] cage_select Which SFPDP module to read
 * @param[out] low_speed_io The I/O signals state
 * @return Returns true if have read the state of I/O signals, or false if an error.
 */
bool cms_read_qsfp_module_low_speed_io (xilinx_cms_context_t *const context, const uint32_t cage_select,
                                        cms_qsfp_low_speed_io_read_data_t *const low_speed_io)
{
    cms_mailbox_t mailbox = {0};
    bool success;

    generic_pci_access_update_field (&mailbox.header, CMS_MAILBOX_HEADER_OPCODE_MASK, CMS_OP_READ_MODULE_LOW_SPEED_IO_OPCODE);
    mailbox.request_fixed_size = true;
    mailbox.request_payload_size_bytes = 4;
    mailbox.payload.words[0] = cage_select;
    mailbox.response_fixed_size = true;
    mailbox.response_payload_size_bytes = 8;
    success = cms_mailbox_transaction (context, &mailbox);

    if (success)
    {
        const uint32_t low_speed_signals = mailbox.payload.words[1];

        low_speed_io->qsfp_int_l    = (low_speed_signals & (1u << 4)) != 0;
        low_speed_io->qsfp_modprs_l = (low_speed_signals & (1u << 3)) != 0;
        low_speed_io->qsfp_modsel_l = (low_speed_signals & (1u << 2)) != 0;
        low_speed_io->qsfp_lpmode   = (low_speed_signals & (1u << 1)) != 0;
        low_speed_io->qsfp_reset_l  = (low_speed_signals & (1u << 0)) != 0;
    }

    return success;
}


/**
 * @brief Report diagnostic information about the CMS configuration.
 * @param[in] context Used to access the CMS
 */
void cms_display_configuration (const xilinx_cms_context_t *const context)
{
    printf ("\n  CMS software profile %s\n", cms_software_profile_names[context->software_profile]);

    /* From PG348 the lower 3 bytes seem to be a BCD version. Not sure of the encoding of the most significant byte.
     * Display as both a BCD version and raw hex value. */
    const uint32_t fw_version = read_reg32 (context->host_cms_shared_memory, CMS_FW_VERSION_REG_OFFSET);
    printf ("  CMS firmware version %u.%u.%u (0x%08X)\n",
            (fw_version & 0x00ff0000) >> 16,
            (fw_version & 0x0000ff00) >> 8,
            (fw_version & 0x000000ff),
            fw_version);

    /* Display all available card information sensors */
    for (cms_snsr_id_t sensor_id = 0; sensor_id < CMS_SNSR_ID_ARRAY_SIZE; sensor_id++)
    {
        const cms_card_information_sensor_t *sensor = &context->card_information_sensors[sensor_id];

        if (sensor->data != NULL)
        {
            printf ("  %s: ", cms_snsr_id_names[sensor_id]);
            switch (sensor_id)
            {
            case CMS_SNSR_ID_CARD_SN:
            case CMS_SNSR_ID_MAC_ADDRESS0:
            case CMS_SNSR_ID_MAC_ADDRESS1:
            case CMS_SNSR_ID_MAC_ADDRESS2:
            case CMS_SNSR_ID_MAC_ADDRESS3:
            case CMS_SNSR_ID_CARD_REV:
            case CMS_SNSR_ID_CARD_NAME:
            case CMS_SNSR_ID_SAT_VERSION:
            case CMS_SNSR_ID_FAN_PRESENCE:
                /* These are ASCII text. Some are NULL terminated but limit output to the data length. */
                printf ("%.*s", (int) sensor->data_len, sensor->data);
                break;

            case CMS_SNSR_ID_TOTAL_POWER_AVAIL:
                switch (sensor->data[0])
                {
                case 0: printf ("75W"); break;
                case 1: printf ("150W"); break;
                case 2: printf ("225W"); break;
                case 3: printf ("300W"); break;
                default:
                    printf ("Unknown (0x%x)", sensor->data[0]);
                    break;
                }
                break;

            case CMS_SNSR_ID_CONFIG_MODE:
                switch (sensor->data[0])
                {
                case 0x00: printf ("Slave_Serial_x1"); break;
                case 0x01: printf ("Slave_Select_Map_x8"); break;
                case 0x02: printf ("Slave_Map_x16"); break;
                case 0x03: printf ("Slave_Select_Map_x32"); break;
                case 0x04: printf ("JTag_Boundary_Scan_x1"); break;
                case 0x05: printf ("Master_SPI_x1"); break;
                case 0x06: printf ("Master_SPI_x2"); break;
                case 0x07: printf ("Master_SPI_x4"); break;
                case 0x08: printf ("Master_SPI_x8"); break;
                case 0x09: printf ("Master_BPI_x8"); break;
                case 0x0a: printf ("Master_BPI_x16"); break;
                case 0x0b: printf ("Master_Serial_x1"); break;
                case 0x0c: printf ("Master_Select_Map_x8"); break;
                case 0x0d: printf ("Master_Select_Map_x16"); break;
                default:
                    printf ("Unknown (0x%x)", sensor->data[0]);
                    break;
                }
                break;

            case CMS_SNSR_ID_NEW_MAC_SCHEME:
                printf ("%u contiguous MAC addresses starting from %02X:%02X:%02X:%02X:%02X:%02X",
                        sensor->data[0],
                        sensor->data[2], sensor->data[3], sensor->data[4], sensor->data[5], sensor->data[6], sensor->data[7]);
                break;

            case CMS_SNSR_ID_CAGE_TYPE_00:
            case CMS_SNSR_ID_CAGE_TYPE_01:
            case CMS_SNSR_ID_CAGE_TYPE_02:
            case CMS_SNSR_ID_CAGE_TYPE_03:
                switch (sensor->data[0])
                {
                case 0x00: printf ("QSFP/QSFP+"); break;
                case 0x01: printf ("DSFP"); break;
                case 0x02: printf ("SFP/SFP+"); break;
                default:
                    printf ("Unknown (0x%x)", sensor->data[0]);
                    break;
                }
                break;

            case CMS_SNSR_ID_ARRAY_SIZE:
                /* Shouldn't get here */
                break;
            }
            printf ("\n");
        }
    }
}
