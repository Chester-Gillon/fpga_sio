/*
 * @file cms_qsfp_management_menu.c
 * @date 14 Nov 2025
 * @author Chester Gillon
 * @brief Menu driven program perform QSFP management, which uses the Xilinx Card Management Solution Subsystem (CMS Subsystem)
 * @details
 *   Written to initially test the QSFP management in the fpga_tests/U200_ibert_100G_ether design.
 *   Assumes a maximum of one device to manage.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_cms.h"
#include "vfio_bitops.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>


/* Reference clock selection GPIO bits */
#define QSFP_FS0_BIT_OFFSET             0u
#define QSFP_FS1_BIT_OFFSET             1u
#define QSFP_REFCLK_RESET_BIT_OFFSET    2u
#define QSFP_REFCLK_SEL_BITS_PER_MODULE 3u


/* Contains the context for performing the QSFP management */
typedef struct
{
    /* Used to communicate with the CMS subsystem */
    xilinx_cms_context_t cms_context;
    /* Controls how the module information is read:
     * - false uses a block read.
     * - true uses reads of individual bytes.
     *
     * This is for testing for any difference between the two module read mechanisms provided by the CMS. */
    bool use_module_byte_read;
    /* Total number of bytes read from a module */
    size_t total_module_bytes_read;
    /* Total duration spent reading bytes from module */
    int64_t total_module_read_time_ns;
    /* The number of QSFP modules on the card, which can be managed by this program */
    uint32_t num_qsfp_modules;
    /* GPIO output used for the QSFP reference clock selection.
     * NULL if the design doesn't support the QSFP reference clock selection. */
    uint8_t *refclk_selection_gpio_output;
    /* GPIO input used for the QSFP reference clock selection, to read back the outputs. */
    const uint8_t *refclk_selection_gpio_input;
} qsfp_management_context_t;


/**
 * @brief Read option text from standard input, trimming any leading and trailing whitespace.
 * @param[out] text The text read, with any leading or trailing white space removed.
 */
#define TEXT_OPTION_LEN 16
static void read_option_text (char text[const TEXT_OPTION_LEN])
{
    size_t len;

    fgets (text, TEXT_OPTION_LEN, stdin);

    /* Trim leading whitespace */
    while ((text[0] != '\0') && isspace (text[0]))
    {
        memmove (&text[0], &text[1], strlen (text));
    }

    /* Trim trailing whitespace */
    len = strlen (text);
    while ((len > 0) && isspace (text[len - 1]))
    {
        text[len - 1] = '\0';
        len--;
    }
}


/**
 * @brief Display the QSFP management status for all modules.
 * @todo Always displays the QSFP module temperature, even when may not be valid. Consider validating by only displaying when both
 *       a. Module is detected as present (via GPIO)
 *       b. Inserted module supports the temperature measurement.
 *          ethtool -m seems to know which modules to display the temperature for. Albeit can display "0 degrees C" for DAC cables.
 * @param[in,out] context The context to obtain the status
 */
static void display_qsfp_status (qsfp_management_context_t *const context)
{
    const int field_name_width = 17;
    const int field_value_width = 19;

    enum
    {
        QSFP_INT_L,
        QSFP_MODPRS_L,
        QSFP_MODSEL_L,
        QSFP_LPMODE,
        QSFP_RESET_L,
        QSFP_TEMPERATURE,
        QSFP_FS0,
        QSFP_FS1,
        QSFP_REFCLK_RESET,

        QSFP_NUM_FIELDS
    };
    const char *const field_names[QSFP_NUM_FIELDS] =
    {
        [QSFP_INT_L]        = "QSFP_INT_L",
        [QSFP_MODPRS_L]     = "QSFP_MODPRS_L",
        [QSFP_MODSEL_L]     = "QSFP_MODSEL_L",
        [QSFP_LPMODE]       = "QSFP_LPMODE",
        [QSFP_RESET_L]      = "QSFP_RESET_L",
        [QSFP_TEMPERATURE]  = "QSFP_TEMPERATURE",
        [QSFP_FS0]          = "QSFP_FS[0]",
        [QSFP_FS1]          = "QSFP_FS[1]",
        [QSFP_REFCLK_RESET] = "QSFP_REFCLK_RESET"
    };
    char field_values[QSFP_NUM_FIELDS][CMS_MAX_NUM_QSFP_MODULES][20];

    const uint32_t current_temperatures[CMS_MAX_NUM_QSFP_MODULES] =
    {
        read_reg32 (context->cms_context.host_cms_shared_memory, cms_sensor_definitions[CMS_SENSOR_CAGE_TEMP0].ins_reg_offset),
        read_reg32 (context->cms_context.host_cms_shared_memory, cms_sensor_definitions[CMS_SENSOR_CAGE_TEMP1].ins_reg_offset)
    };
    cms_qsfp_low_speed_io_read_data_t low_speed_io;
    uint32_t module_index;

    memset (field_values, 0, sizeof (field_values));
    const size_t field_value_num_bytes = sizeof (field_values[0][0]);

    /* Always populate the status obtained from CMS */
    for (module_index = 0; module_index < context->num_qsfp_modules; module_index++)
    {
        if (!cms_read_qsfp_module_low_speed_io (&context->cms_context, module_index, &low_speed_io))
        {
            return;
        }

        snprintf (field_values[QSFP_INT_L][module_index], field_value_num_bytes, "%s",
                low_speed_io.qsfp_int_l ? "Interrupt Clear" : "Interrupt Set");
        snprintf (field_values[QSFP_MODPRS_L][module_index], field_value_num_bytes, "%s",
                low_speed_io.qsfp_modprs_l ? "Module not Present" : "Module Present");
        snprintf (field_values[QSFP_MODSEL_L][module_index], field_value_num_bytes, "%s",
                low_speed_io.qsfp_modsel_l ? "Module not Selected" : "Module Selected");
        snprintf (field_values[QSFP_LPMODE][module_index], field_value_num_bytes, "%s",
                low_speed_io.qsfp_lpmode ? "Low Power Mode" : "High Power Mode");
        snprintf (field_values[QSFP_RESET_L][module_index], field_value_num_bytes, "%s",
                low_speed_io.qsfp_reset_l ? "Reset Clear" : "Reset Active");
        snprintf (field_values[QSFP_TEMPERATURE][module_index], field_value_num_bytes, "%uC", current_temperatures[module_index]);
    }

    /* Populate the refclk selection fields, when supported by the design */
    if (context->refclk_selection_gpio_input != NULL)
    {
        const uint32_t gpio_input = read_reg32 (context->refclk_selection_gpio_input, 0);

        for (module_index = 0; module_index < context->num_qsfp_modules; module_index++)
        {
            const uint32_t start_bit = module_index * QSFP_REFCLK_SEL_BITS_PER_MODULE;

            snprintf (field_values[QSFP_FS0][module_index], field_value_num_bytes, "%u",
                    vfio_extract_field_u32 (gpio_input, 1u << (start_bit + QSFP_FS0_BIT_OFFSET)));
            snprintf (field_values[QSFP_FS1][module_index], field_value_num_bytes, "%u",
                    vfio_extract_field_u32 (gpio_input, 1u << (start_bit + QSFP_FS1_BIT_OFFSET)));
            snprintf (field_values[QSFP_REFCLK_RESET][module_index], field_value_num_bytes, "%u",
                    vfio_extract_field_u32 (gpio_input, 1u << (start_bit + QSFP_REFCLK_RESET_BIT_OFFSET)));
        }
    }

    /* Display the populated fields */
    printf ("%*s", field_name_width, "");
    for (module_index = 0; module_index < context->num_qsfp_modules; module_index++)
    {
        char module_title[8];
        snprintf (module_title, sizeof (module_title), "QSFP%u", module_index);
        printf ("  %*s", field_value_width, module_title);
    }
    printf ("\n");

    const uint32_t last_populated_field = (context->refclk_selection_gpio_input != NULL) ? QSFP_REFCLK_RESET : QSFP_TEMPERATURE;
    for (uint32_t field_index = 0; field_index <= last_populated_field; field_index++)
    {
        printf ("%*s", field_name_width, field_names[field_index]);
        for (module_index = 0; module_index < context->num_qsfp_modules; module_index++)
        {
            printf ("  %*s", field_value_width, field_values[field_index][module_index]);
        }
        printf ("\n");
    }
}


/**
 * @brief Initialise the context for QSFP management
 * @param[out] context The initialised context
 * @param[in] design The design to perform QSFP management for
 * @return Returns true if the initialisation was successful, or false if failed.
 */
static bool initialise_qsfp_management (qsfp_management_context_t *const context, const fpga_design_t *const design)
{
    memset (context, 0, sizeof (*context));

    /* Always initialise the CMS */
    if (!cms_initialise_access (&context->cms_context, design->vfio_device,
            design->cms_subsystem_bar_index, design->cms_subsystem_base_offset))
    {
        return false;
    }
    context->num_qsfp_modules = cms_num_qsfp_modules[context->cms_context.software_profile];

    /* Optionally initialise access to the design specific GPIOs for the reference clock selection */
    switch (design->design_id)
    {
    case FPGA_DESIGN_U200_IBERT_100G_ETHER:
        {
            const size_t gpio_base_offset = 0x43000;
            const size_t gpio_input_offset = 0x0;
            const size_t gpio_output_offset = 0x8;

            context->refclk_selection_gpio_output = map_vfio_registers_block (design->vfio_device, design->cms_subsystem_bar_index,
                    gpio_base_offset + gpio_output_offset, sizeof (uint32_t));
            context->refclk_selection_gpio_input = map_vfio_registers_block (design->vfio_device, design->cms_subsystem_bar_index,
                    gpio_base_offset + gpio_input_offset, sizeof (uint32_t));
        }
        break;

    default:
        /* This design doesn't support reference clock selection */
        context->refclk_selection_gpio_output = NULL;
        context->refclk_selection_gpio_input = NULL;
        break;
    }

    return true;
}


/**
 * @brief Prompt the user for a GPIO refclk selection output signal to toggle for one QSFP module
 * @param[in,out] context The context to toggle the signal on
 * @param[in] module_index Which QSFP module to toggle a GPIO refclk selection on
 * @return Returns true when have toggled a signal, and the new state should be displayed
 */
static bool toggle_refclk_selection_gpio (qsfp_management_context_t *const context, const uint32_t module_index)
{
    char text[TEXT_OPTION_LEN];
    uint32_t signal_index;
    char junk;

    printf ("Signal to toggle: 0=FS[0], 1=FS[1], 2=REFCLK_RESET\n");
    printf (" > ");

    read_option_text (text);
    const int num_items = sscanf (text, "%u%c", &signal_index, &junk);
    const bool valid_output_signal = (num_items == 1) && (signal_index < 3);

    if (valid_output_signal)
    {
        const uint32_t start_bit = module_index * QSFP_REFCLK_SEL_BITS_PER_MODULE;
        const uint32_t gpio_bit_mask = 1u << (start_bit + signal_index);
        uint32_t refclk_selection_gpio = read_reg32 (context->refclk_selection_gpio_input, 0);
        refclk_selection_gpio ^= gpio_bit_mask;
        write_reg32 (context->refclk_selection_gpio_output, 0, refclk_selection_gpio);
    }
    else
    {
        printf ("Invalid signal\n");
    }

    return valid_output_signal;
}


/**
 * @brief Prompt the user for a reference clock frequency plan to set via GPIO outputs.
 * @details
 *   While the Alveo U200 suer guide indicates the SI5335A has two different frequencies which in theory means the FS0 bit is
 *   unused this function allow selection of all combinations of FS[1:0] bits.
 *
 *   @todo The https://www.skyworksinc.com/-/media/SkyWorks/SL/documents/public/data-sheets/Si5335.pdf datashet doesn't seem
 *         to specify if the FS[1:0] bits are sampled synchronously on an RESET edge or asynchronously.
 *
 *         This function changes the FS[1:0] while reset is asserted.
 *
 *         Changing the frequency plan is not reliable, based upon running U200_ibert_100G_ether:
 *         a. For QSFP0 can't seem to select 156.250000 MHz.
 *         b. For QSFP1 sometimes a frequency change doesn't seem to take effect.
 *         c. Leaving REFCLK_RESET high (asserted) seems to drop either QSFP0 or QSFP1 to 154.176 MHz
 * @param[in,out] context The context to toggle the signal on
 * @param[in] module_index Which QSFP module to set the reference clock frequency plan on
 * @return Returns true when have set the frequency plan, and the new state should be displayed
 */
static bool set_refclk_frequency_plan (qsfp_management_context_t *const context, const uint32_t module_index)
{
    char text[TEXT_OPTION_LEN];
    uint32_t frequency_plan;
    char junk;

    printf ("Select frequency plan: 0=reserved 1=156.250000 MHz 2=161.132812 MHz 3=161.132812 MHz\n");
    printf (" > ");

    read_option_text (text);
    const int num_items = sscanf (text, "%u%c", &frequency_plan, &junk);
    const bool frequency_plan_valid = (num_items == 1) && (frequency_plan < 4);

    if (frequency_plan_valid)
    {
        const uint32_t start_bit = module_index * QSFP_REFCLK_SEL_BITS_PER_MODULE;
        const uint32_t fs0_bit = QSFP_FS0_BIT_OFFSET + start_bit;
        const uint32_t fs1_bit = QSFP_FS1_BIT_OFFSET + start_bit;
        const uint32_t reset_bit = QSFP_REFCLK_RESET_BIT_OFFSET + start_bit;
        uint32_t refclk_selection_gpio = read_reg32 (context->refclk_selection_gpio_input, 0);

        /* Assert reset */
        refclk_selection_gpio |= 1u << reset_bit;
        write_reg32 (context->refclk_selection_gpio_output, 0, refclk_selection_gpio);

        /* Set new frequency plan */
        refclk_selection_gpio &= ~(1u << fs0_bit);
        refclk_selection_gpio &= ~(1u << fs1_bit);
        if ((frequency_plan & 0x1) != 0)
        {
            refclk_selection_gpio |= 1u << fs0_bit;
        }
        if ((frequency_plan & 0x2) != 0)
        {
            refclk_selection_gpio |= 1u << fs1_bit;
        }
        write_reg32 (context->refclk_selection_gpio_output, 0, refclk_selection_gpio);

        /* Leave reset asserted for at least 1 microsecond.
         * The SI5335A datasheet gives the "Reset Minimum Pulse Width" as 200 nanoseconds. */
        const struct timespec reset_delay =
        {
            .tv_sec = 0,
            .tv_nsec = 1000
        };
        clock_nanosleep (CLOCK_MONOTONIC, 0, &reset_delay, NULL);

        /* De-assert reset */
        refclk_selection_gpio &= ~(1u << reset_bit);
        write_reg32 (context->refclk_selection_gpio_output, 0, refclk_selection_gpio);
    }
    else
    {
        printf ("Invalid frequency plan\n");
    }

    return frequency_plan_valid;
}


/**
 * @brief Prompt the user for a QSFP GPIO output signal to toggle for one QSFP module
 * @param[in,out] context The context to toggle the signal on
 * @param[in] module_index Which QSFP module to toggle a QSFP GPIO signal on
 * @return Returns true when have toggled a signal, and the new state should be displayed
 */
static bool toggle_qsfp_gpio (qsfp_management_context_t *const context, const uint32_t module_index)
{
    char text[TEXT_OPTION_LEN];
    uint32_t signal_index;
    char junk;
    bool success = false;

    printf ("Signal to toggle: 0=QSFP_RESET_L, 1=LPMODE\n");
    printf (" > ");

    read_option_text (text);
    const int num_items = sscanf (text, "%u%c", &signal_index, &junk);
    const bool valid_output_signal = (num_items == 1) && (signal_index < 2);

    if (valid_output_signal)
    {
        cms_qsfp_low_speed_io_read_data_t low_speed_read;
        cms_qsfp_low_speed_io_write_data_t low_speed_write;

        success = cms_read_qsfp_module_low_speed_io (&context->cms_context, module_index, &low_speed_read);
        if (success)
        {
            low_speed_write.qsfp_lpmode = low_speed_read.qsfp_lpmode;
            low_speed_write.qsfp_reset_l = low_speed_read.qsfp_reset_l;
            switch (signal_index)
            {
            case 0:
                low_speed_write.qsfp_reset_l = !low_speed_write.qsfp_reset_l;
                break;

            case 1:
                low_speed_write.qsfp_lpmode = !low_speed_write.qsfp_lpmode;
                break;
            }

            success = cms_write_qsfp_module_low_speed_io (&context->cms_context, module_index, &low_speed_write);
        }
    }

    return success;
}


/**
 * @brief Read one block of I2C module, using either one CMS block read or multiple CMS byte read operations.
 * @param[in,out] context The context to read the block for.
 * @param[in] block_i2c_addressing The block to read.
 * @param[out] data The data of the block read.
 * @return Returns true if have read the data from the the I2C module, or false if an error.
 *         With a U200 even with no module fitted have seen the CMS not indicate an error, but undefined data was
 *         returned.
 */
static bool read_module_page (qsfp_management_context_t *const context, const cms_i2s_addressing_t *const block_i2c_addressing,
                              uint8_t data[const CMS_I2C_MODULE_PAGE_LEN])
{
    bool success;

    const int64_t read_start_time = get_monotonic_time ();
    if (context->use_module_byte_read)
    {
        cms_i2s_addressing_t byte_i2c_addressing = *block_i2c_addressing;
        const uint32_t page_start_byte = block_i2c_addressing->upper_page_select ? CMS_I2C_MODULE_PAGE_LEN : 0;

        success = true;
        for (uint32_t page_offset = 0; success && (page_offset < CMS_I2C_MODULE_PAGE_LEN); page_offset++)
        {
            byte_i2c_addressing.byte_offset = page_start_byte + page_offset;
            success = cms_i2c_module_byte_read (&context->cms_context, &byte_i2c_addressing, &data[page_offset]);
        }
    }
    else
    {
        success = cms_i2c_module_block_read (&context->cms_context, block_i2c_addressing, data);
    }
    const int64_t read_end_time = get_monotonic_time ();

    if (success)
    {
        context->total_module_bytes_read += CMS_I2C_MODULE_PAGE_LEN;
        context->total_module_read_time_ns += read_end_time - read_start_time;
    }

    return success;
}


/**
 * @brief Verify a check code for fields in a SFF module
 * @param[in] num_bytes_checked The number of bytes in the check code.
 * @param[in] data The bytes covered by the check code.
 * @param[in] expected_sum The expected sum of the bytes in the check code.
 */
static bool verify_sff_check_code (const uint32_t num_bytes_checked, const uint8_t data[const num_bytes_checked],
                                   const uint8_t expected_sum)
{
    uint8_t actual_sum = 0;

    for (uint32_t byte_index = 0; byte_index < num_bytes_checked; byte_index++)
    {
        actual_sum += data[byte_index];
    }

    return actual_sum == expected_sum;
}


/**
 * @brief Display module information as per "SFF-8472 Specification for Management Interface for SFP+"
 * @param[in,out] context The context to read display the module information for.
 * @param[in] module_index Which module to display the information for
 * @param[in] lower_page_zero The page which contains the SFF-8472 base ID fields
 */
static void display_sff_8472_module_information (qsfp_management_context_t *const context, const uint32_t module_index,
                                                 const uint8_t lower_page_zero[const CMS_I2C_MODULE_PAGE_LEN])
{
    bool success;

    if (!verify_sff_check_code (63, &lower_page_zero[0], lower_page_zero[63]))
    {
        printf ("Base ID check code failed\n");
    }
    else if (!verify_sff_check_code (31, &lower_page_zero[64], lower_page_zero[95]))
    {
        printf ("Extended ID check code failed\n");
    }
    else
    {
        const int vendor_name_start = 20;
        const int vendor_name_len = 16;
        printf ("Vendor Name = \"%.*s\"\n", vendor_name_len, &lower_page_zero[vendor_name_start]);

        const int vendor_pn_start = 40;
        const int vendor_pn_len = 16;
        printf ("Vendor PN = \"%.*s\"\n", vendor_pn_len, &lower_page_zero[vendor_pn_start]);

        const int vendor_rev_start = 56;
        const int vendor_rev_len = 4;
        printf ("Vendor rev = \"%.*s\"\n", vendor_rev_len, &lower_page_zero[vendor_rev_start]);

        const int vendor_sn_start = 68;
        const int vendor_sn_len = 16;
        printf ("Vendor SN = \"%.*s\"\n", vendor_sn_len, &lower_page_zero[vendor_sn_start]);

        /* If implemented, display the digital diagnostic monitoring defined by SFF-8472 */
        const uint32_t diagnostic_monitoring_type = lower_page_zero[92];
        const bool digital_diagnostic_monitoring_implemented = (diagnostic_monitoring_type & VFIO_BIT (6)) != 0;
        const bool internally_calibrated = (diagnostic_monitoring_type & VFIO_BIT (5)) != 0;
        const bool average_receive_power = (diagnostic_monitoring_type & VFIO_BIT (3)) != 0;
        const bool address_change_required = (diagnostic_monitoring_type & VFIO_BIT (2)) != 0;

        if (digital_diagnostic_monitoring_implemented)
        {
            if (address_change_required)
            {
                printf ("Address change required for digital diagnostic monitoring - no support in this program\n");
            }
            else if (internally_calibrated)
            {
                uint8_t digital_diagnostic_monitoring[CMS_I2C_MODULE_PAGE_LEN];
                const cms_i2s_addressing_t diagnostic_monitoring_i2c_addressing =
                {
                    .cage_select = module_index,
                    .page_select = 0,
                    .cmis_bank_field_valid = false,
                    .use_sfp_plus_diagonistic_i2c_address = true,
                    .upper_page_select = false
                };

                success = read_module_page (context, &diagnostic_monitoring_i2c_addressing, digital_diagnostic_monitoring);
                if (success)
                {
                    /* Units of power are 0.1 micro Watts */
                    const uint32_t tx_power_int = (digital_diagnostic_monitoring[102] * 256u) + digital_diagnostic_monitoring[103];
                    const uint32_t rx_power_int = (digital_diagnostic_monitoring[104] * 256u) + digital_diagnostic_monitoring[105];
                    const double tx_power_mw = (double) tx_power_int / 1E4;
                    const double rx_power_mw = (double) rx_power_int / 1E4;
                    const double tx_power_dbm = 10 * log10 (tx_power_mw);
                    const double rx_power_dbm = 10 * log10 (rx_power_mw);

                    printf ("Measured TX output power: %6.4f mW / %6.2f dBm\n", tx_power_mw, tx_power_dbm);
                    printf ("Measured RX output power: %6.4f mW / %6.2f dBm (%s)\n", rx_power_mw, rx_power_dbm,
                            average_receive_power ? "average receiver power" : "Optical modulation amplitude");
                }
                else
                {
                    printf ("Failed to read digital diagnostic monitoring\n");
                }
            }
            else
            {
                printf ("This program only supported internally calibrated digital diagnostic monitoring\n");
            }
        }
        else
        {
            printf ("Digital diagnostic monitoring not implemented\n");
        }
    }
}


/**
 * @brief Display module information as per "SFF-8636 Specification for Management Interface for 4-lane Modules and Cables"
 * @param[in,out] context The context to read display the module information for.
 * @param[in] module_index Which module to display the information for
 * @param[in] lower_page_zero The page which contains the SFF-8636 channel monitoring values
 */
static void display_sff_8636_module_information (qsfp_management_context_t *const context, const uint32_t module_index,
                                                 const uint8_t lower_page_zero[const CMS_I2C_MODULE_PAGE_LEN])
{
    uint32_t lane;
    uint8_t identification_page[CMS_I2C_MODULE_PAGE_LEN];
    bool success;

    const cms_i2s_addressing_t identification_i2c_addressing =
    {
        .cage_select = module_index,
        .page_select = 0,
        .cmis_bank_field_valid = false,
        .use_sfp_plus_diagonistic_i2c_address = false,
        .upper_page_select = true
    };

    /* Always report the measured Tx power, TX bias current and Rx power without checking if implemented since:
     * a. SFF-8636 doesn't seem to define a field in Diagnostic Monitoring Type to indicate if measured Rx power is provided or not.
     * b. While the Diagnostic Monitoring Type does have a bit to indicate if measured Tx power is provided, read_module_page()
     *    doesn't seem to be able to read the identification_page if I2C bytes reads are used. */
    for (lane = 0; lane < 4; lane++)
    {
        /* Units of power are 0.1 micro Watts */
        const uint32_t msb_byte_index = 50 + (lane * 2);
        const uint32_t tx_power_int = (lower_page_zero[msb_byte_index] * 256u) + lower_page_zero[msb_byte_index + 1];
        const double tx_power_mw = (double) tx_power_int / 1E4;
        const double tx_power_dbm = 10 * log10 (tx_power_mw);

        printf ("Measured Tx%u output power: %6.4f mW / %6.2f dBm\n", lane + 1, tx_power_mw, tx_power_dbm);
    }

    for (lane = 0; lane < 4; lane++)
    {
        /* Units of TX bias current are 2uA */
        const uint32_t msb_byte_index = 42 + (lane * 2);
        const uint32_t tx_bias_current_int = (lower_page_zero[msb_byte_index] * 256u) + lower_page_zero[msb_byte_index + 1];
        const double tx_bias_current_milliamps = (double) tx_bias_current_int / 500.0;

        printf ("Tx%u bias current: %.3f mA\n", lane + 1, tx_bias_current_milliamps);
    }

    for (lane = 0; lane < 4; lane++)
    {
        /* Units of power are 0.1 micro Watts */
        const uint32_t msb_byte_index = 34 + (lane * 2);
        const uint32_t rx_power_int = (lower_page_zero[msb_byte_index] * 256u) + lower_page_zero[msb_byte_index + 1];
        const double rx_power_mw = (double) rx_power_int / 1E4;
        const double rx_power_dbm = 10 * log10 (rx_power_mw);

        printf ("Measured Rx%u output power: %6.4f mW / %6.2f dBm\n", lane + 1, rx_power_mw, rx_power_dbm);
    }

    /* Temperature is 16 bit twos-complement, with least significant bit representing 1/256 Celsius */
    const int16_t temperature_int = (int16_t) ((lower_page_zero[22] * 256u) + lower_page_zero[23]);
    const double temperature_celsuis = (double) temperature_int / 256.0;
    printf ("Temperature: %.3f °C\n", temperature_celsuis);

    /* Units of supply voltage are 100 microvolts */
    const uint32_t vcc_int = (lower_page_zero[26] * 256u) + lower_page_zero[27];
    const double vcc_volts = (double) vcc_int / 1E4;
    printf ("Vcc: %.4f V\n", vcc_volts);

    /* Read and then report the identification information */
    success = read_module_page (context, &identification_i2c_addressing, identification_page);
    if (success)
    {
        if (!verify_sff_check_code (63, &identification_page[0], identification_page[63]))
        {
            printf ("Base ID check code failed\n");
        }
        else if (!verify_sff_check_code (31, &identification_page[64], identification_page[95]))
        {
            printf ("Extended ID check code failed\n");
        }
        else
        {
            const int vendor_name_start = 20;
            const int vendor_name_len = 16;
            printf ("Vendor Name = \"%.*s\"\n", vendor_name_len, &identification_page[vendor_name_start]);

            const int vendor_pn_start = 40;
            const int vendor_pn_len = 16;
            printf ("Vendor PN = \"%.*s\"\n", vendor_pn_len, &identification_page[vendor_pn_start]);

            const int vendor_rev_start = 56;
            const int vendor_rev_len = 2;
            printf ("Vendor rev = \"%.*s\"\n", vendor_rev_len, &identification_page[vendor_rev_start]);

            const int vendor_sn_start = 68;
            const int vendor_sn_len = 16;
            printf ("Vendor SN = \"%.*s\"\n", vendor_sn_len, &identification_page[vendor_sn_start]);
        }
    }
    else
    {
        printf ("Failed to read identification page\n");
    }
}


/**
 * @brief Display module information, handling SFP+ or QSFP modules
 * @details Doesn't attempt to use cms_read_qsfp_module_low_speed_io() to check for the presence of a module before reading
 *          the information. This is to allow investigation of the CMS error handling.
 * @param[in,out] context The context to read display the module information for.
 * @param[in] module_index Which module to display the information for
 */
static void display_module_information (qsfp_management_context_t *const context, const uint32_t module_index)
{
    cms_i2s_addressing_t i2c_addressing =
    {
        .cage_select = module_index
    };
    uint8_t lower_page_zero[CMS_I2C_MODULE_PAGE_LEN];
    bool success;

    context->total_module_bytes_read = 0;
    context->total_module_read_time_ns = 0;

    /* Read the block which all modules have the module identification type in */
    i2c_addressing.page_select = 0;
    i2c_addressing.cmis_bank_field_valid = false;
    i2c_addressing.use_sfp_plus_diagonistic_i2c_address = false;
    i2c_addressing.upper_page_select = false;
    success = read_module_page (context, &i2c_addressing, lower_page_zero);

    if (success)
    {
        const uint8_t identifier_value = lower_page_zero[0];
        switch (identifier_value)
        {
        case 0x02:
            printf ("Module/connector soldered to motherboard (using SFF-8472)\n");
            display_sff_8472_module_information (context, module_index, lower_page_zero);
            break;

        case 0x03:
            printf ("SFP/SFP+/SFP28 and later with SFF-8472 management interface\n");
            display_sff_8472_module_information (context, module_index, lower_page_zero);
            break;

        case 0x0D:
            printf ("QSFP+ or later with SFF-8636 or SFF-8436 management interface\n");
            display_sff_8636_module_information (context, module_index, lower_page_zero);
            break;

        case 0x11:
            printf ("QSFP28 or later with SFF-8636 management interface\n");
            display_sff_8636_module_information (context, module_index, lower_page_zero);
            break;

        default:
            printf ("Unknown module identifier 0x%02x\n", identifier_value);
            break;
        }
    }

    printf ("Read %zu bytes in %.6f seconds using %s\n",
            context->total_module_bytes_read, (double) context->total_module_read_time_ns / 1E9,
            context->use_module_byte_read ? "I2C byte read" : "I2C block read");
}


/**
 * @brief Perform the top level menu for QSFP management
 * @param[in] design The design to perform QSFP management for
 */
static void qsfp_management_menu (const fpga_design_t *const design)
{
    qsfp_management_context_t context;
    char text[TEXT_OPTION_LEN];
    char junk;
    uint32_t module_index;
    bool exit_requested;
    bool display_menu;
    uint32_t menu_option;
    bool valid_option;

    if (!initialise_qsfp_management (&context, design))
    {
        /* Initialise function has reported an error */
        return;
    }

    display_qsfp_status (&context);
    module_index = 0;
    display_menu = true;
    do
    {
        exit_requested = false;
        printf ("\nCurrent module for control operations: %u\n", module_index);
        if (display_menu)
        {
            printf ("Menu:\n");
            printf ("0: Select module for control operations\n");
            printf ("1: Display QSFP status\n");
            printf ("2: Toggle QSFP GPIO output\n");
            printf ("3: Display module information, using block read\n");
            printf ("4: Display module information, using byte read\n");
            if (context.refclk_selection_gpio_output != NULL)
            {
                printf ("5: Toggle refclk selection output\n");
                printf ("6: Set refclk frequency plan\n");
            }
            printf ("98: Display menu\n");
            printf ("99: Exit\n");
            display_menu = false;
        }

        printf ("Option >");
        read_option_text (text);
        const int num_items = sscanf (text, "%u%c", &menu_option, &junk);
        valid_option = num_items == 1;
        if (valid_option)
        {
            switch (menu_option)
            {
            case 0:
                {
                    uint32_t option_value;

                    printf ("Module to select >");
                    read_option_text (text);
                    const int num_items = sscanf (text, "%u%c", &option_value, &junk) == 1;
                    valid_option = (num_items == 1) && (option_value < context.num_qsfp_modules);
                    if (valid_option)
                    {
                        module_index = option_value;
                    }
                }
                break;

            case 1:
                display_qsfp_status (&context);
                break;

            case 2:
                if (toggle_qsfp_gpio (&context, module_index))
                {
                    display_qsfp_status (&context);
                }
                break;

            case 3:
                context.use_module_byte_read = false;
                display_module_information (&context, module_index);
                break;

            case 4:
                context.use_module_byte_read = true;
                display_module_information (&context, module_index);
                break;

            case 5:
                valid_option = context.refclk_selection_gpio_output != NULL;
                if (valid_option)
                {
                    if (toggle_refclk_selection_gpio (&context, module_index))
                    {
                        display_qsfp_status (&context);
                    }
                }
                break;

            case 6:
                valid_option = context.refclk_selection_gpio_output != NULL;
                if (valid_option)
                {
                    if (set_refclk_frequency_plan (&context, module_index))
                    {
                        display_qsfp_status (&context);
                    }
                }
                break;

            case 98:
                display_menu = true;
                break;

            case 99:
                exit_requested = true;
                break;

            default:
                valid_option = false;
                break;
            }
        }

        if (!valid_option)
        {
            printf ("Invalid menu option\n");
        }
    } while (!exit_requested);
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    const fpga_design_t *design = NULL;

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    uint32_t num_supported_designs = 0;
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        if (designs.designs[design_index].cms_subsystem_present)
        {
            num_supported_designs++;
            design = &designs.designs[design_index];
        }
    }

    if (num_supported_designs == 1)
    {
        qsfp_management_menu (design);
    }
    else
    {
        printf ("Found %u supported designs, this program can only be used with a single supported design\n", num_supported_designs);
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
