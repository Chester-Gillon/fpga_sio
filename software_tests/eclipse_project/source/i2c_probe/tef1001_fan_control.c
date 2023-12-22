/*
 * @file tef1001_fan_control.c
 * @date 13 May 2023
 * @author Chester Gillon
 * @brief Provides a test of modifying the FPGA fan controller in the CPLD on the TEF1001 board
 * @details
 *   When this program was created the intent was to modify to combinations of the fan being disabled,
 *   or enabled at different PWM's, and report the resulting fan speed.
 *
 *   However, it was found writing the fan controller registers didn't work in the REV03 CPLD.
 *   https://gist.github.com/Chester-Gillon/27d9ed419a25ecb3c4358377da34924b#11-unable-to-correctly-write-to-the-cpld-i2c-fan-control-registers
 *   identified a potential bug in the CPLD source code around how the I2C device for the fan control is implemented in the CPLD
 *   and the test_fan_control() serves to just demonstrate the failure mode.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "i2c_bit_banged.h"
#include "vfio_access.h"
#include "fpga_sio_pci_ids.h"

#define I2C_SLAVE_ADDRESS 0x74
#define FAN_CTRL_REG_ADDRESS 0 /* read/write */
#define FAN1_RPS_REG_ADDRESS 1 /* read */
#define FAN1_PWM_REG_ADDRESS 1 /* write */


/*
 * @brief Write to a register in the CPLD fan controller
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] reg_address The register to write to
 * @param[in] reg_value The register value to write
 */
static void write_fan_register (bit_banged_i2c_controller_context_t *const controller,
                                const uint8_t reg_address, const uint8_t reg_value)
{
    /* @todo Attempt to write the actual value four times to increment the address bits across the range used as inputs
     *       in the i2c_read_proc process in the CPLD VHDL source code. */
    const uint8_t write_data[] = {reg_address, reg_value, reg_value, reg_value, reg_value};

    if (bit_banged_i2c_write (controller, I2C_SLAVE_ADDRESS, sizeof (write_data), write_data, true) != sizeof (write_data))
    {
        printf ("Failed to write fan register\n");
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Read a register in the CPLD fan controller
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] reg_address The register to read from
 * @return Returns the register value read
 */
static uint8_t read_fan_register (bit_banged_i2c_controller_context_t *const controller, const uint8_t reg_address)
{
    uint8_t reg_value;

    if (bit_banged_i2c_write (controller, I2C_SLAVE_ADDRESS, sizeof (reg_address), &reg_address, true) != sizeof (reg_address))
    {
        printf ("Failed to write register address\n");
        exit (EXIT_FAILURE);
    }

    if (!bit_banged_i2c_read (controller, I2C_SLAVE_ADDRESS, sizeof (reg_value), &reg_value, true))
    {
        printf ("Failed to read register value\n");
        exit (EXIT_FAILURE);
    }

    return reg_value;
}


/*
 * @brief Read the fan control registers and report their raw values
 */
static void report_fan_control_registers (bit_banged_i2c_controller_context_t *const controller)
{
    uint8_t fan_enable_reg_value;
    uint8_t fan_rps_reg_value;

    fan_enable_reg_value = read_fan_register (controller, FAN_CTRL_REG_ADDRESS);
    fan_rps_reg_value = read_fan_register (controller, FAN1_RPS_REG_ADDRESS);
    printf ("Fan enable register=%u (0x%02x)  Fan RPS register=%u (0x%02x)\n",
            fan_enable_reg_value, fan_enable_reg_value, fan_rps_reg_value, fan_rps_reg_value);
}


/**
 * @brief Perform an attempt to modify a register in the CPLD fan controller
 * @param[in/out] vfio_device The VFIO device to test
 * @param[in] fan_control_value The value to write to the fan control register
 */
static void test_fan_control (vfio_device_t *const vfio_device, const uint8_t fan_control_value)
{
    bit_banged_i2c_controller_context_t controller = {0};

    /* The FPGA has a single BAR, containing IIC and GPIO registers. This program only used the GPIO registers */
    const uint32_t bar_index = 0;
    const uint32_t gpio_base_offset = 0x1000;
    const uint32_t expected_bar_size = 0x2000;

    map_vfio_device_bar_before_use (vfio_device, bar_index);
    if ((vfio_device->mapped_bars[bar_index] != NULL) && (vfio_device->regions_info[bar_index].size >= expected_bar_size))
    {
        uint8_t *const gpio_regs = &vfio_device->mapped_bars[bar_index][gpio_base_offset];

        printf ("Using BAR %d in device %s of size 0x%llx\n",
                bar_index, vfio_device->device_name, vfio_device->regions_info[bar_index].size);
        select_i2c_controller (true, gpio_regs, &controller);

        /* Report current registers */
        report_fan_control_registers (&controller);

        /* Write the value from the command line argument to the fan control register */
        write_fan_register (&controller, FAN_CTRL_REG_ADDRESS, fan_control_value);
        printf ("Wrote to %u (0x%02x) to fan control register\n", fan_control_value, fan_control_value);

        /* Report values after attempting to write to the fan control register */
        report_fan_control_registers (&controller);
    }
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;
    uint32_t fan_control_value;
    char junk;

    if (argc != 2)
    {
        printf ("Usage: %s <fan_control_value>\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    const char *const fan_control_text = argv[1];
    if ((sscanf (fan_control_text, "%u%c", &fan_control_value, &junk) != 1) || (fan_control_value > 255))
    {
        printf ("%s is not a valid fan_control_value\n", fan_control_text);
        exit (EXIT_FAILURE);
    }

    /* Filters for the FPGA devices tested */
    const vfio_pci_device_identity_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_I2C_PROBE,
            .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    /* Open PCI devices supported by the test */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    /* Display information using the FPGA devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        test_fan_control (vfio_device, (uint8_t) fan_control_value);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
