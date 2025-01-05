/*
 * @file qsfp_management_menu.c
 * @date 5 Jan 2025
 * @author Chester Gillon
 * @brief Menu driven program perform QSFP management
 * @details
 *   Written to initially test the QSFP management in the fpga_tests/XCKU5P_DUAL_QSFP_ibert_4.166 design.
 *   Assumes a maximum of one device to manage.
 *
 *   Implemented as a menu to keep the VFIO device open in case the settings get reset on VFIO device close.
 *   Consider investigating the effect of the PCIe Interface "Reset Source" in the DMA Bridge IP.
 */

#include "fpga_sio_pci_ids.h"
#include "vfio_access.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>


/* The QSFP management discrete signals controlled by GPIO, as bit numbers */
typedef enum
{
    /* Inputs */
    GPIO_MOD_PRSN,
    GPIO_INTERRUPT,

    /* Outputs, where the output value can be read back from the GPIO input register */
    GPIO_RESET,
    GPIO_MOD_SEL,
    GPIO_LP_MODE,
    GPIO_LED,

    GPIO_ARRAY_SIZE
} gpio_signals_t;

#define GPIO_FIRST_OUTPUT_SIGNAL GPIO_RESET
#define GPIO_LAST_OUTPUT_SIGNAL  GPIO_LED


/* The names to displays for the QSFP management GPIO signals */
static const char *const gpio_signal_names[GPIO_ARRAY_SIZE] =
{
   [GPIO_MOD_PRSN ] = "MOD_PRS",
   [GPIO_INTERRUPT] = "INTERRUPT",
   [GPIO_RESET    ] = "RESET",
   [GPIO_MOD_SEL  ] = "MOD_SEL",
   [GPIO_LP_MODE  ] = "LP_MODE",
   [GPIO_LED      ] = "LED"
};


/* The number of QSFP ports which can be managed */
#define NUM_QSFP_PORTS 2
static const char *const qsfp_port_names[NUM_QSFP_PORTS] =
{
    [0] = "A",
    [1] = "B"
};


/* Contains the registers mapped for management of one QSFP port */
typedef struct
{
    /* Used to read the inputs signal and the current state of the output signals */
    const uint8_t *gpio_input;
    /* Write only for the output signals */
    uint8_t *gpio_output;
} qsfp_management_port_registers_t;


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
 * @brief Display the current state of the GPIO signals for all QSFP management ports
 * @param[in] qsfp_ports The registers to get the state of the GPIO signals from
 */
static void display_gpio_signals (const qsfp_management_port_registers_t qsfp_ports[const NUM_QSFP_PORTS])
{
    uint32_t port_index;
    uint32_t current_signals[NUM_QSFP_PORTS];

    for (port_index = 0; port_index < NUM_QSFP_PORTS; port_index++)
    {
        current_signals[port_index] = read_reg32 (qsfp_ports[port_index].gpio_input, 0);
    }

    printf ("\n");
    printf ("  Signal ");
    for (port_index = 0; port_index < NUM_QSFP_PORTS; port_index++)
    {
        printf ("  %s", qsfp_port_names[port_index]);
    }
    printf ("\n");

    for (uint32_t signal_index = 0; signal_index < GPIO_ARRAY_SIZE; signal_index++)
    {
        printf ("%9s", gpio_signal_names[signal_index]);
        for (port_index = 0; port_index < NUM_QSFP_PORTS; port_index++)
        {
            printf ("  %u", (current_signals[port_index] >> signal_index) & 1);
        }
        printf ("\n");
    }
}


/**
 * @brief Prompt the user for a GPIO output signal to toggle on one QSFP management port
 * @param[in] qsfp_port Which QSFP port to toggle a GPIO output on
 * @return Returns true when have toggled a signal, and the new state should be displayed
 */
static bool toggle_gpio_output (const qsfp_management_port_registers_t *const qsfp_port)
{
    char text[TEXT_OPTION_LEN];
    uint32_t signal_index;
    char junk;

    printf ("Signal to toggle:");
    for (signal_index = GPIO_FIRST_OUTPUT_SIGNAL; signal_index <= GPIO_LAST_OUTPUT_SIGNAL; signal_index++)
    {
        printf (" %u=%s", signal_index, gpio_signal_names[signal_index]);
    }
    printf (" > ");

    read_option_text (text);
    const int num_items = sscanf (text, "%u%c", &signal_index, &junk);
    const bool valid_output_signal = (num_items == 1) &&
            (signal_index >= GPIO_FIRST_OUTPUT_SIGNAL) && (signal_index <= GPIO_LAST_OUTPUT_SIGNAL);

    if (valid_output_signal)
    {
        uint32_t port_value = read_reg32 (qsfp_port->gpio_input, 0);

        port_value ^= 1 << signal_index;

        write_reg32 (qsfp_port->gpio_output, 0, port_value);
    }
    else
    {
        printf ("Invalid signal\n");
    }

    return valid_output_signal;
}


/**
 * @brief Perform the top level menu for QSFP management
 * @param[in,out] vfio_device The device to perform QSFP management for
 */
static void qsfp_management_menu (vfio_device_t *const vfio_device)
{
    qsfp_management_port_registers_t qsfp_ports[NUM_QSFP_PORTS];
    char text[TEXT_OPTION_LEN];
    char junk;
    uint32_t port_index;
    bool exit_requested;
    bool display_menu;
    uint32_t menu_option;
    bool valid_option;

    /* Map the registers used for QSFP management */
    const uint32_t bar_index = 0;
    const size_t frame_size_per_port = 0x2000;
    const size_t overall_frame_size = NUM_QSFP_PORTS * frame_size_per_port;
    const size_t gpio_input_offset = 0x0;
    const size_t gpio_output_offset = 0x8;
    for (port_index = 0; port_index < NUM_QSFP_PORTS; port_index++)
    {
        const size_t port_start_offset = port_index * frame_size_per_port;
        qsfp_management_port_registers_t *const port = &qsfp_ports[port_index];

        port->gpio_input = map_vfio_registers_block (vfio_device, bar_index, port_start_offset + gpio_input_offset, overall_frame_size);
        port->gpio_output = map_vfio_registers_block (vfio_device, bar_index, port_start_offset + gpio_output_offset, overall_frame_size);
        if ((port->gpio_input == NULL) || (port->gpio_output == NULL))
        {
            printf ("Failed to map registers for port %s\n", qsfp_port_names[port_index]);
            return;
        }
    }

    display_gpio_signals (qsfp_ports);
    port_index = 0;
    display_menu = true;
    do
    {
        exit_requested = false;
        printf ("\nCurrent port for control operations: %s\n", qsfp_port_names[port_index]);
        if (display_menu)
        {
            printf ("Menu:\n");
            printf ("0: Select port for control operations\n");
            printf ("1: Display GPIO signals\n");
            printf ("2: Toggle GPIO output\n");
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
                    printf ("Port to select >");
                    read_option_text (text);
                    valid_option = false;
                    for (uint32_t candidate_port = 0; !valid_option && (candidate_port < NUM_QSFP_PORTS); candidate_port++)
                    {
                        if (strcasecmp (text, qsfp_port_names[candidate_port]) == 0)
                        {
                            valid_option = true;
                            port_index = candidate_port;
                        }
                    }
                }
                break;

            case 1:
                display_gpio_signals (qsfp_ports);
                break;

            case 2:
                if (toggle_gpio_output (&qsfp_ports[port_index]))
                {
                    display_gpio_signals (qsfp_ports);
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
    vfio_devices_t vfio_devices;

    /* Filters for the FPGA devices tested */
    const vfio_pci_device_identity_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_XCKU5P_DUAL_QSFP_IBERT,
            .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    /* Open PCI devices supported by the test */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    if (vfio_devices.num_devices > 0)
    {
        if (vfio_devices.num_devices > 1)
        {
            printf ("%u devices found, only using the 1st one\n", vfio_devices.num_devices);
        }
        qsfp_management_menu (&vfio_devices.devices[0]);
    }
    else
    {
        printf ("No compatible device found\n");
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
