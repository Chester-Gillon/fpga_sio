/*
 * @file qsfp_management_menu.c
 * @date 5 Jan 2025
 * @author Chester Gillon
 * @brief Menu driven program perform QSFP management, which uses the IIC IP with a direct connection for I2C interface
 * @details
 *   Written to initially test the QSFP management in the fpga_tests/XCKU5P_DUAL_QSFP_ibert_4.166 design.
 *   Assumes a maximum of one device to manage.
 *
 *   The GPIOs were set up for the XCKU5P_DUAL_QSFP board to have a LED for each QSFP port, in addition to the QSFP discrete
 *   signals.
 *
 *   Implemented as a menu to keep the VFIO device open in case the settings get reset on VFIO device close.
 *   Consider investigating the effect of the PCIe Interface "Reset Source" in the DMA Bridge IP.
 */

#include "fpga_sio_pci_ids.h"
#include "vfio_access.h"
#include "xilinx_axi_iic_transfers.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <linux/ethtool.h>


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
    /* The mapped registers for the Xilinx IIC */
    uint8_t *iic_regs;
    /* The controller for I2C transfers */
    iic_controller_context_t iic_controller;
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
 * @brief Take action to set up a QSFP module for access over I2C
 * @param[in/out] qsfp_port Which QSFP port will be accessed
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS is the module is ready for access
 */
static iic_transfer_status_t qsfp_module_access_setup (qsfp_management_port_registers_t *const qsfp_port)
{
    uint32_t port_value;
    const uint32_t module_present_mask = 1u << GPIO_MOD_PRSN;
    const uint32_t module_select_mask = 1u << GPIO_MOD_SEL;

    /* Check that a module is present */
    port_value = read_reg32 (qsfp_port->gpio_input, 0);
    if ((port_value & module_present_mask) != 0)
    {
        return IIC_TRANSFER_STATUS_NO_ACK;
    }

    /* Ensure the QSFP module is enabled for I2C access */
    if ((port_value & module_select_mask) != 0)
    {
        port_value &= ~module_select_mask;
        write_reg32 (qsfp_port->gpio_output, 0, port_value);
    }

    return IIC_TRANSFER_STATUS_SUCCESS;
}


/**
 * @brief Perform a single I2C read from a QSFP module
 * @details
 *  This:
 *  a. Takes action to set up a QSFP module for access over I2C
 *  b. Attempts to work-around a race condition in iic_read(). If num_bytes==1 will actually read 2 bytes in a I2C transaction,
 *     and only return the 1st byte to the caller.
 * @param[in,out] qsfp_port Which QSFP port to read from
 * @param[in] i2c_slave_address The module I2C address to read from
 * @param[in] data_address The start address to read data from from
 * @param[in] num_bytes The number of data bytes to read
 * @param[out] data_read The data read from I2C, using a single transaction.
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the read was successful
 */
static iic_transfer_status_t qsfp_i2c_single_read (qsfp_management_port_registers_t *const qsfp_port, const uint8_t i2c_slave_address,
                                                   const uint8_t data_address, size_t num_bytes, uint8_t data[const num_bytes])
{
    iic_transfer_status_t status;

    status = qsfp_module_access_setup (qsfp_port);

    if (status == IIC_TRANSFER_STATUS_SUCCESS)
    {
        status = iic_write (&qsfp_port->iic_controller, i2c_slave_address, sizeof (data_address), &data_address,
                IIC_TRANSFER_OPTION_REPEATED_START);
    }

    if (status == IIC_TRANSFER_STATUS_SUCCESS)
    {
        if (num_bytes == 1)
        {
            uint8_t read_buffer[2];

            /* Attempt to work-around the race condition in iic_read() which may get stuck when attempt to read a single byte.
             * This reads 2 bytes, and copies only the 1st into the callers buffer. */
            status = iic_read (&qsfp_port->iic_controller, i2c_slave_address, sizeof (read_buffer), read_buffer, IIC_TRANSFER_OPTION_STOP);
            data[0] = read_buffer[0];
        }
        else
        {
            status = iic_read (&qsfp_port->iic_controller, i2c_slave_address, num_bytes, data, IIC_TRANSFER_OPTION_STOP);
        }
    }

    return status;
}


/**
 * @brief Perform an I2C read from a QSFP module
 * @details
 *  The data_reverse_read is for verifying that the I2C operation can perform addressing as expected.
 *  For data items which are constant, data_single_read and data_reverse_read should have the same values.
 * @param[in,out] qsfp_port Which QSFP port to read from
 * @param[in] i2c_slave_address The module I2C address to read from
 * @param[in] data_address The start address to read data from from
 * @param[in] num_bytes The number of data bytes to read
 * @param[out] data_single_read The data read from I2C, using a single transaction.
 * @param[out] data_reverse_read When non-NULL the data read from I2C, using multiple transactions which work backwards
 *                               with a decrementing data address for every transaction.
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the read was successful
 */
static iic_transfer_status_t qsfp_module_read (qsfp_management_port_registers_t *const qsfp_port, const uint8_t i2c_slave_address,
                                               const uint8_t data_address, size_t num_bytes,
                                               uint8_t data_single_read[const num_bytes],
                                               uint8_t data_reverse_read[const num_bytes])
{
    iic_transfer_status_t status;

    /* Always perform a single read transaction */
    status = qsfp_i2c_single_read (qsfp_port, i2c_slave_address, data_address, num_bytes, data_single_read);

    /* Perform a reverse read when requested */
    if (data_reverse_read != NULL)
    {
        uint8_t chunk_data_address = (uint8_t) (data_address + num_bytes);
        size_t bytes_remaining = num_bytes;

        while ((status == IIC_TRANSFER_STATUS_SUCCESS) && (bytes_remaining > 0))
        {
            /* Due to the work-around applied in qsfp_i2c_single_read() read chunks of 2 bytes in reverse */
            const size_t bytes_in_chunk = (bytes_remaining > 1) ? 2 : 1;

            chunk_data_address -= (uint8_t) bytes_in_chunk;
            bytes_remaining -= bytes_in_chunk;
            status = qsfp_i2c_single_read (qsfp_port, i2c_slave_address, chunk_data_address, bytes_in_chunk,
                    &data_reverse_read[bytes_remaining]);
        }
    }

    return status;
}

/* @todo An initial test of reading module information over I2C, checking the results using the debugger */
static void display_module_information (qsfp_management_port_registers_t *const qsfp_port)
{
    uint8_t data_forward[ETH_MODULE_SFF_8079_LEN];
    uint8_t data_reverse[ETH_MODULE_SFF_8079_LEN];
    iic_transfer_status_t status;

    status = qsfp_module_read (qsfp_port, 0x50, 0, sizeof (data_forward), data_forward, data_reverse);
    if (status == IIC_TRANSFER_STATUS_SUCCESS)
    {
        printf ("Module identifier = 0x%02x (0x%02x)\n", data_forward[0], data_reverse[0]);
    }
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
    const size_t iic_base_offset = 0x1000;
    for (port_index = 0; port_index < NUM_QSFP_PORTS; port_index++)
    {
        const size_t port_start_offset = port_index * frame_size_per_port;
        qsfp_management_port_registers_t *const port = &qsfp_ports[port_index];

        port->gpio_input = map_vfio_registers_block (vfio_device, bar_index, port_start_offset + gpio_input_offset, overall_frame_size);
        port->gpio_output = map_vfio_registers_block (vfio_device, bar_index, port_start_offset + gpio_output_offset, overall_frame_size);
        port->iic_regs = map_vfio_registers_block (vfio_device, bar_index, port_start_offset + iic_base_offset, overall_frame_size);
        if ((port->gpio_input == NULL) || (port->gpio_output == NULL) || (port->iic_regs == NULL))
        {
            printf ("Failed to map registers for port %s\n", qsfp_port_names[port_index]);
            return;
        }

        iic_initialise_controller (&port->iic_controller, port->iic_regs);
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
            printf ("3: Display module information\n");
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

            case 3:
                display_module_information (&qsfp_ports[port_index]);
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
