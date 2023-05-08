/*
 * @file i2c_dump_info.c
 * @date 8 May 2023
 * @author Chester Gillon
 * @details
 *   Use the /fpga_tests/i2c_probe FPGA image to dump information from the I2C devices on the Trenz Electronic TEF1001-02-B2IX4-A
 */

#include <stdlib.h>
#include <stdio.h>

#include "i2c_bit_banged.h"
#include "vfio_access.h"
#include "fpga_sio_pci_ids.h"


/**
 * @brief Display information by reading the fan control register in the CPLD
 * @details https://wiki.trenz-electronic.de/display/PD/TEF1001+CPLD#TEF1001CPLD-FAN1 documents the register information.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 */
static void dump_tef1001_fan_info (bit_banged_i2c_controller_context_t *const controller)
{
    const uint8_t i2c_slave_address = 0x74;
    const uint8_t fan_ctrl_reg_address = 0;
    const uint8_t fan1_rps_reg_address = 1;

    uint8_t reg_value;

    /* @todo Read without repeated start seem to work */
    if (bit_banged_i2c_write (controller, i2c_slave_address, sizeof (fan_ctrl_reg_address), &fan_ctrl_reg_address, true)
            == sizeof (fan_ctrl_reg_address))
    {
        if (bit_banged_i2c_read (controller, i2c_slave_address, sizeof (reg_value), &reg_value, true))
        {
            printf ("FAN Control register = 0x%02x (fan %s)\n", reg_value, ((reg_value & 0x80) != 0) ? "Enabled" : "Disabled");
        }
        else
        {
            printf ("Failed to read FAN Control register\n");
        }
    }

    if (bit_banged_i2c_write (controller, i2c_slave_address, sizeof (fan1_rps_reg_address), &fan1_rps_reg_address, true)
            == sizeof (fan_ctrl_reg_address))
    {
        if (bit_banged_i2c_read (controller, i2c_slave_address, sizeof (reg_value), &reg_value, true))
        {
            printf ("FAN1 Revolutions per second = %u\n", reg_value);
        }
        else
        {
            printf ("Failed to read FAN1 Revolutions per second register\n");
        }
    }

    /* @todo Whereas read with repeated start fail */
    if (bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address, fan_ctrl_reg_address,
            sizeof (reg_value), &reg_value))
    {
        printf ("FAN Control register = 0x%02x (fan %s)\n", reg_value, ((reg_value & 0x80) != 0) ? "Enabled" : "Disabled");
    }
    else
    {
        printf ("Failed to read FAN Control register using repeated start\n");
    }

    if (bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address, fan1_rps_reg_address,
            sizeof (reg_value), &reg_value))
    {
        printf ("FAN1 Revolutions per second = %u\n", reg_value);
    }
    else
    {
        printf ("Failed to read FAN1 Revolutions per second register using repeated start\n");
    }
}


/**
 * @brief Read one 16-bit register in a DDR temperature sensor
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] reg_address Which temperature sensor register to read
 * @param[out] reg_value The value of the register
 * @return Returns true of the register was read, or false if a NACK from the I2C slave.
 */
static bool read_ddr_temperature_register (bit_banged_i2c_controller_context_t *const controller, const uint8_t reg_address,
                                           uint16_t *const reg_value)
{
    bool success;
    const uint8_t i2c_slave_address = 0x19;
    uint8_t data[sizeof (uint16_t)];

    success = bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address, reg_address, sizeof (data), data);
    if (success)
    {
        /* Convert the bytes read as big-endian to native format */
        *reg_value = (uint16_t) ((data[0] << 8u) | data[1]);
    }

    return success;
}


/**
 * @brief Display the value read from a DDR temperature sensor in degrees-C
 * @param[in] reg_value The register value to display in degrees-C
 */
static void decode_ddr_temperature (const uint16_t reg_value)
{
    const uint32_t temperature_setting_mask =     0x0FFF;
    const uint32_t temperature_sign_mask    =     0x1000;
    const uint32_t sign_extension           = 0xFFFFF000;
    uint32_t unsigned_temp;
    int32_t signed_temp;
    const double temperature_scaling = 0.0625; /* 12 bits, with least significant bit 0.0625 C */

    unsigned_temp = reg_value & temperature_setting_mask;
    if ((reg_value & temperature_sign_mask) != 0)
    {
        unsigned_temp |= sign_extension;
    }
    signed_temp = (int32_t) unsigned_temp;
    printf ("(%.1lf C)", (double) signed_temp * temperature_scaling);
}


/**
 * @brief Display information from a DDR temperature sensor
 * @details JEDEC Standard No. 21-C 4.1.4 "Definition of the TSE2002av Serial Presence Detect (SPD) EEPROM with
 *           Temperature Sensor (TS) for Memory Module Applications" was used to obtain the register definitions.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 */
static void dump_ddr_temperature_information (bit_banged_i2c_controller_context_t *const controller)
{
    const uint8_t capabilities_reg_address = 0x00;
    const uint8_t configuration_reg_address = 0x01;
    const uint8_t high_limit_reg_address = 0x02;
    const uint8_t low_limit_reg_address = 0x03;
    const uint8_t tcrit_limit_reg_address = 0x04;
    const uint8_t ambient_temperature_reg_address = 0x05;
    const uint8_t manufacter_id_reg_address = 0x6;

    uint16_t reg_value;

    if (read_ddr_temperature_register (controller, capabilities_reg_address, &reg_value))
    {
        printf ("DDR temperature Capabilities = 0x%04x\n", reg_value);
    }
    else
    {
        printf ("Failed to read DDR temperature Capabilities\n");
    }

    if (read_ddr_temperature_register (controller, configuration_reg_address, &reg_value))
    {
        printf ("DDR temperature configuration register = 0x%04x\n", reg_value);
    }
    else
    {
        printf ("Failed to read DDR configuration register\n");
    }

    if (read_ddr_temperature_register (controller, high_limit_reg_address, &reg_value))
    {
        printf ("DDR temperature high limit register = 0x%04x ", reg_value);
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR temperature high limit\n");
    }

    if (read_ddr_temperature_register (controller, low_limit_reg_address, &reg_value))
    {
        printf ("DDR temperature low limit register = 0x%04x ", reg_value);
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR temperature low limit\n");
    }

    if (read_ddr_temperature_register (controller, tcrit_limit_reg_address, &reg_value))
    {
        printf ("DDR temperature critical limit register = 0x%04x ", reg_value);
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR temperature critical limit\n");
    }

    if (read_ddr_temperature_register (controller, ambient_temperature_reg_address, &reg_value))
    {
        /* Display the ambient temperature value, along with any alert flags */
        const uint16_t tcrit_mask = 0x8000;
        const uint16_t high_mask = 0x4000;
        const uint16_t low_mask = 0x2000;

        printf ("DDR ambient temperature register = 0x%04x%s%s%s ",
                reg_value,
                (reg_value & tcrit_mask) != 0 ? " above TCRIT" : "",
                (reg_value & high_mask) != 0 ? " above HIGH" : "",
                (reg_value & low_mask) != 0 ? " below LOW" : "");
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR ambient temperature\n");
    }

    if (read_ddr_temperature_register (controller, manufacter_id_reg_address, &reg_value))
    {
        printf ("DDR temperature sensor manufacturer ID register = 0x%04x\n", reg_value);
    }
    else
    {
        printf ("Failed to read DDR temperature sensor manufacturer ID\n");
    }
}


/**
 * @brief Dump information from I2C devices TEF1001-02-B2IX4-A
 * @param[in/out] vfio_device The VFIO device containing the Xilinx FPGA to use for the probe
 */
static void dump_tef1001_information (vfio_device_t *const vfio_device)
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

        dump_tef1001_fan_info (&controller);
        dump_ddr_temperature_information (&controller);
    }
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    /* Filters for the FGPA devices tested */
    const vfio_pci_device_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_I2C_PROBE,
            .enable_bus_master = false
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    /* Open PCI devices supported by the test */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    /* Display information using the FPGA devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        dump_tef1001_information (vfio_device);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
