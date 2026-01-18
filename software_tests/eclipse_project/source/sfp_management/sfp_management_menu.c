/*
 * @file sfp_management_menu.c
 * @date 18 Jan 2026
 * @author Chester Gillon
 * @brief Menu driven program perform SFP management, which uses the IIC IP with a direct connection for I2C interface
 * @details
 *   Created to test the VD100_10G_ether_dual design. This has limited SFP management options:
 *   a. While the board has two SFP ports, only SFP1 has the I2C pins connected.
 *   b. Only slow speed signal connected to FPGA pins for SFP1 and SFP2 is the TX_DISABLE
 *      (via a transistor which inverts the polarity so displayed as a TX_ENABLE).
 *
 *   Implemented as a menu to keep the VFIO device open in case the settings get reset on VFIO device close.
 *   Consider investigating the effect of the PCIe Interface "Reset Source" in the DMA Bridge IP.
 */

#include "fpga_sio_pci_ids.h"
#include "vfio_access.h"
#include "vfio_bitops.h"
#include "xilinx_axi_iic_transfers.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include <linux/ethtool.h>


/* GPIO bit numbers */
#define SFP1_TX_ENABLE VFIO_BIT(0)
#define SFP2_TX_ENABLE VFIO_BIT(1)


/* Contains the registers mapped for management of the SFP ports */
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
} sfp_management_registers_t;


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
 * @brief Toggle the Tx Enable for one SFP port
 * @param[in,out] management_regs The SFP management registers, containing the GPIO registers
 * @param[in] bit_toggle_mask Specifies which Tx Enable to toggle
 */
static void toggle_sfp_tx_enable (sfp_management_registers_t *const management_regs, const uint32_t bit_toggle_mask)
{
    uint32_t gpio_value = read_reg32 (management_regs->gpio_input, 0);
    gpio_value ^= bit_toggle_mask;
    write_reg32 (management_regs->gpio_output, 0, gpio_value);
}


/**
 * @brief Perform a single I2C read from a SFP module
 * @details
 *  This attempts to work-around a race condition in iic_read(). If num_bytes==1 will actually read 2 bytes in a I2C transaction,
 *  and only return the 1st byte to the caller.
 * @param[in,out] management_regs Which SFP port to read from
 * @param[in] i2c_slave_address The module I2C address to read from
 * @param[in] data_address The start address to read data from from
 * @param[in] num_bytes The number of data bytes to read
 * @param[out] data_read The data read from I2C, using a single transaction.
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the read was successful
 */
static iic_transfer_status_t sfp_i2c_single_read (sfp_management_registers_t *const management_regs, const uint8_t i2c_slave_address,
                                                  const uint8_t data_address, size_t num_bytes, uint8_t data[const num_bytes])
{
    iic_transfer_status_t status;

    status = iic_write (&management_regs->iic_controller, i2c_slave_address, sizeof (data_address), &data_address,
            IIC_TRANSFER_OPTION_REPEATED_START);

    if (status == IIC_TRANSFER_STATUS_SUCCESS)
    {
        if (num_bytes == 1)
        {
            uint8_t read_buffer[2];

            /* Attempt to work-around the race condition in iic_read() which may get stuck when attempt to read a single byte.
             * This reads 2 bytes, and copies only the 1st into the callers buffer. */
            status = iic_read (&management_regs->iic_controller, i2c_slave_address, sizeof (read_buffer), read_buffer, IIC_TRANSFER_OPTION_STOP);
            data[0] = read_buffer[0];
        }
        else
        {
            status = iic_read (&management_regs->iic_controller, i2c_slave_address, num_bytes, data, IIC_TRANSFER_OPTION_STOP);
        }
    }

    return status;
}


/**
 * @brief Perform an I2C read from a SFP module
 * @details
 *  The data_reverse_read is for verifying that the I2C operation can perform addressing as expected.
 *  For data items which are constant, data_single_read and data_reverse_read should have the same values.
 * @param[in,out] management_regs Which SFP port to read from
 * @param[in] i2c_slave_address The module I2C address to read from
 * @param[in] data_address The start address to read data from from
 * @param[in] num_bytes The number of data bytes to read
 * @param[out] data_single_read The data read from I2C, using a single transaction.
 * @param[out] data_reverse_read When non-NULL the data read from I2C, using multiple transactions which work backwards
 *                               with a decrementing data address for every transaction.
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the read was successful
 */
static iic_transfer_status_t sfp_module_read (sfp_management_registers_t *const management_regs, const uint8_t i2c_slave_address,
                                              const uint8_t data_address, size_t num_bytes,
                                              uint8_t data_single_read[const num_bytes],
                                              uint8_t data_reverse_read[const num_bytes])
{
    iic_transfer_status_t status;

    /* Always perform a single read transaction */
    status = sfp_i2c_single_read (management_regs, i2c_slave_address, data_address, num_bytes, data_single_read);

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
            status = sfp_i2c_single_read (management_regs, i2c_slave_address, chunk_data_address, bytes_in_chunk,
                    &data_reverse_read[bytes_remaining]);
        }
    }

    return status;
}

/**
 * @brief Display SFP module information
 * @details
 *  Currently only displays a sample of values.
 *  As a way of validating the I2C communication, displays the values obtains read in both forward and reverse directions.
 * @param[in,out] management_regs Which SFP port to read from
 */
static void display_module_information (sfp_management_registers_t *const management_regs)
{
    uint8_t data_forward[ETH_MODULE_SFF_8079_LEN];
    uint8_t data_reverse[ETH_MODULE_SFF_8079_LEN];
    iic_transfer_status_t status;

    status = sfp_module_read (management_regs, 0x50, 0, sizeof (data_forward), data_forward, data_reverse);
    if (status == IIC_TRANSFER_STATUS_SUCCESS)
    {
        printf ("Module identifier = 0x%02x (0x%02x)\n", data_forward[0], data_reverse[0]);

        const int vendor_name_start = 20;
        const int vendor_name_len = 16;
        printf ("Vendor Name = \"%.*s\" (\"%.*s\")\n",
                vendor_name_len, &data_forward[vendor_name_start],
                vendor_name_len, &data_reverse[vendor_name_start]);

        const int vendor_pn_start = 40;
        const int vendor_pn_len = 16;
        printf ("Vendor PN = \"%.*s\" (\"%.*s\")\n",
                vendor_pn_len, &data_forward[vendor_pn_start],
                vendor_pn_len, &data_reverse[vendor_pn_start]);

        const int vendor_rev_start = 56;
        const int vendor_rev_len = 4;
        printf ("Vendor rev = \"%.*s\" (\"%.*s\")\n",
                vendor_rev_len, &data_forward[vendor_rev_start],
                vendor_rev_len, &data_reverse[vendor_rev_start]);

        const int vendor_sn_start = 68;
        const int vendor_sn_len = 16;
        printf ("Vendor SN = \"%.*s\" (\"%.*s\")\n",
                vendor_sn_len, &data_forward[vendor_sn_start],
                vendor_sn_len, &data_reverse[vendor_sn_start]);
    }
}


/**
 * @brief Perform the top level menu for SFP management
 * @param[in,out] vfio_device The device to perform SFP management for
 */
static void sfp_management_menu (vfio_device_t *const vfio_device)
{
    sfp_management_registers_t management_regs;
    char text[TEXT_OPTION_LEN];
    char junk;
    uint32_t menu_option;
    bool valid_option;
    bool exit_requested;
    bool display_menu;
    bool display_gpios;

    const uint32_t peripherals_bar_index = 0;
    const size_t gpio_input_offset = 0x0;
    const size_t gpio_output_offset = 0x8;
    const size_t gpio_base_offset     = 0x10000;
    const size_t iic_base_offset      = 0x11000;
    const size_t iic_frame_size       = 0x01000;

    management_regs.gpio_input =
            map_vfio_registers_block (vfio_device, peripherals_bar_index, gpio_base_offset + gpio_input_offset, sizeof (uint32_t));
    management_regs.gpio_output =
            map_vfio_registers_block (vfio_device, peripherals_bar_index, gpio_base_offset + gpio_output_offset, sizeof (uint32_t));
    management_regs.iic_regs =
            map_vfio_registers_block (vfio_device, peripherals_bar_index, iic_base_offset, iic_frame_size);
    iic_initialise_controller (&management_regs.iic_controller, management_regs.iic_regs);

    display_menu = true;
    display_gpios = true;
    do
    {
        exit_requested = false;
        if (display_gpios || display_menu)
        {
            const uint32_t input_reg = read_reg32 (management_regs.gpio_input, 0);

            printf ("SFP1 Tx %s  SFP2 Tx %s\n",
                    (input_reg & SFP1_TX_ENABLE) ? "Enabled " : "Disabled",
                    (input_reg & SFP2_TX_ENABLE) ? "Enabled " : "Disabled");
            display_gpios = false;
        }

        if (display_menu)
        {
            printf ("Menu:\n");
            printf ("0: Display module information\n");
            printf ("1: Toggle SFP1 Tx Enable\n");
            printf ("2: Toggle SFP2 Tx Enable\n");
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
                display_module_information (&management_regs);
                break;

            case 1:
                toggle_sfp_tx_enable (&management_regs, SFP1_TX_ENABLE);
                display_gpios = true;
                break;

            case 2:
                toggle_sfp_tx_enable (&management_regs, SFP2_TX_ENABLE);
                display_gpios = true;
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
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_VD100_10G_ETHER_DUAL,
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
        sfp_management_menu (&vfio_devices.devices[0]);
    }
    else
    {
        printf ("No compatible device found\n");
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}

