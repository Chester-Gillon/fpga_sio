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
#include "generic_pci_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>


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
                    generic_pci_access_extract_field (gpio_input, 1u << (start_bit + QSFP_FS0_BIT_OFFSET)));
            snprintf (field_values[QSFP_FS1][module_index], field_value_num_bytes, "%u",
                    generic_pci_access_extract_field (gpio_input, 1u << (start_bit + QSFP_FS1_BIT_OFFSET)));
            snprintf (field_values[QSFP_REFCLK_RESET][module_index], field_value_num_bytes, "%u",
                    generic_pci_access_extract_field (gpio_input, 1u << (start_bit + QSFP_REFCLK_RESET_BIT_OFFSET)));
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
            if (context.refclk_selection_gpio_output != NULL)
            {
                printf ("3: Toggle refclk selection output\n");
                printf ("4: Set refclk frequency plan\n");
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
                valid_option = context.refclk_selection_gpio_output != NULL;
                if (valid_option)
                {
                    if (toggle_refclk_selection_gpio (&context, module_index))
                    {
                        display_qsfp_status (&context);
                    }
                }
                break;

            case 4:
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
