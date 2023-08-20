/*
 * @file quad_spi_flasher.c
 * @date 9 Jul 2023
 * @author Chester Gillon
 */

#include "fpga_sio_pci_ids.h"
#include "xilinx_quad_spi.h"
#include "fury_utils.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/* Different modes for reading flash, for the purpose of checking address handling */
typedef enum
{
    /* Read the flash using one SPI transaction */
    FLASH_READ_ONE_TRANSACTION,
    /* Perform a separate SPI transaction for every byte, and reads in decreasing address order */
    FLASH_READ_BYTES_BACKWARDS,
    /* Perform a separate SPI transaction for every byte, and reads in increasing address order */
    FLASH_READ_BYTES_FORWARDS
} flash_read_mode_t;

static const char *const flash_read_mode_names[] =
{
    [FLASH_READ_ONE_TRANSACTION] = "one transaction",
    [FLASH_READ_BYTES_BACKWARDS] = "bytes backwards",
    [FLASH_READ_BYTES_FORWARDS ] = "bytes forwards"
};


/**
 * @brief Read the contents of a SPI flash, using different possible "modes" in terms of the number and order of SPI transactions.
 * @param[in/out] controller The controller to use for the read
 * @param[in] mode Selects the mode used for the read
 * @param[in] num_data_bytes The number of bytes to read
 */
static uint8_t *read_spi_flash_different_modes (quad_spi_controller_context_t *const controller,
                                                const flash_read_mode_t read_mode, uint32_t num_data_bytes)
{
    /* Allocate buffer for the entire flash contents */
    uint8_t *data = malloc (num_data_bytes);
    if (data == NULL)
    {
        printf ("Failed to allocate %u bytes to store entire flash contents\n", num_data_bytes);
        exit (EXIT_FAILURE);
    }

    bool success = true;
    uint32_t flash_address;
    transfer_timing_t timing;
    char description[128];

    snprintf (description, sizeof (description), "read flash %s using opcode=0x%02X address_bytes=%u dummy_bytes=%u",
            flash_read_mode_names[read_mode], controller->read_opcode,
            controller->num_address_bytes, controller->read_num_dummy_bytes);
    initialise_transfer_timing (&timing, description, num_data_bytes);
    transfer_time_start (&timing);

    switch (read_mode)
    {
    case FLASH_READ_ONE_TRANSACTION:
        /* Read the flash contents in one transaction */
        flash_address = 0;
        success = quad_spi_read_flash (controller, flash_address, num_data_bytes, data);
        break;

    case FLASH_READ_BYTES_BACKWARDS:
        {
            /* Read the flash contents one byte at a time, with decreasing addresses */
            uint32_t num_bytes_remaining = num_data_bytes;

            while (success && (num_bytes_remaining > 0))
            {
                flash_address = num_bytes_remaining - 1;
                success = quad_spi_read_flash (controller, flash_address, 1, &data[flash_address]);
                num_bytes_remaining--;
            }
        }
        break;

    case FLASH_READ_BYTES_FORWARDS:
        /* Read the flash contents one byte at a time, with decreasing addresses */
        for (flash_address = 0; success && (flash_address < num_data_bytes); flash_address++)
        {
            success = quad_spi_read_flash (controller, flash_address, 1, &data[flash_address]);
        }
        break;
    }

    transfer_time_stop (&timing);

    if (success)
    {
        display_transfer_timing_statistics (&timing);
    }
    else
    {
        free (data);
        data = NULL;
    }

    return data;
}


/**
 * @brief Compare two buffers containing flash contents read using different modes
 * @details Any differences are summarised on stdout.
 *          The count of differences is ordered by the least significant nibble of the address, for investigating
 *          the effect of "mode" bits in Spansion which in quad-IO mode are output following the least significant
 *          nibble of the address.
 * @param[in] num_data_bytes The number of bytes to compare
 * @param[in] buffer_a, buffer_b The two buffers to compare which were read using different modes
 * @param[in] compare_description Describes the buffers being compared
 */
static void compare_flash_buffers (const uint32_t num_data_bytes,
                                   const uint8_t buffer_a[const num_data_bytes], const uint8_t buffer_b[const num_data_bytes],
                                   const char *const compare_description)
{
    uint32_t num_compare_errors_per_ls_address_nibble[16] = {0};
    bool compare_error = false;

    for (uint32_t address = 0; address < num_data_bytes; address++)
    {
        if (buffer_a[address] != buffer_b[address])
        {
            num_compare_errors_per_ls_address_nibble[address & 0xf]++;
            compare_error = true;
        }
    }

    printf ("Compare %u bytes of %s %s", num_data_bytes, compare_description, compare_error ? "FAIL:\n" : "PASS\n" );
    if (compare_error)
    {
        for (uint32_t ls_address_nibble = 0; ls_address_nibble < 16; ls_address_nibble++)
        {
            const uint32_t num_differences = num_compare_errors_per_ls_address_nibble[ls_address_nibble];

            if (num_differences != 0)
            {
                printf ("  %u Bytes different for least significant address nibble 0x%X\n", num_differences, ls_address_nibble);

            }
        }
    }
}


/**
 * @brief Test reading SPI flash using different modes
 * @details The same flash area is read by different "modes" which use different numbers and sizes of transactions as a way
 *          of testing that the number of dummy bytes is configured correctly and so that the data bytes returned are "valid".
 *          For this test to be meaningful requires the flash area to be programmed rather than just erased.
 * @param[in/out] controller The controller to use for the read
 */
static void test_spi_flash_read_modes (quad_spi_controller_context_t *const controller)
{
    /* Limit testing to initial 2Mbytes of flash since:
     * a. Populated by most of the FPGA designs under test.
     * b. Reading is slow. */
    const uint32_t num_data_bytes = 2048 * 1024;

    /* Read the flash using the different modes */
    const uint8_t *const data_read_one_transaction =
            read_spi_flash_different_modes (controller, FLASH_READ_ONE_TRANSACTION, num_data_bytes);
    const uint8_t *const data_read_bytes_backwards =
            read_spi_flash_different_modes (controller, FLASH_READ_BYTES_BACKWARDS, num_data_bytes);
    const uint8_t *const data_read_bytes_forwards =
            read_spi_flash_different_modes (controller, FLASH_READ_BYTES_FORWARDS, num_data_bytes);
    if ((data_read_one_transaction == NULL) || (data_read_bytes_backwards == NULL) || (data_read_bytes_forwards == NULL))
    {
        return;
    }

    /* Perform the comparison */
    compare_flash_buffers (num_data_bytes, data_read_one_transaction, data_read_bytes_backwards,
            "one transaction .vs. bytes backwards");
    compare_flash_buffers (num_data_bytes, data_read_one_transaction, data_read_bytes_forwards,
            "one transaction .vs. bytes forwards");
}


/**
 * @brief Display information about the SPI flash connected to one device, without reference to a bitstream file
 * @param[in/out] vfio_device The VFIO device containing the quad SPI controller, for reporting diagnostic information.
 * @param[in/out] quad_spi_regs The mapped registers for the quad SPI controller to use.
 */
static void display_spi_flash_information (const vfio_device_t *const vfio_device, uint8_t *const quad_spi_regs)
{
    quad_spi_controller_context_t controller;
    bool success;

    printf ("\nDisplaying information for SPI flash in PCI device %s IOMMU group %s\n",
            vfio_device->device_name, vfio_device->iommu_group);
    success = quad_spi_initialise_controller (&controller, quad_spi_regs);
    if (!success)
    {
        printf ("Failed to initialise Quad SPI controller\n");
        return;
    }

    printf ("FIFO depth=%u\n", controller.fifo_depth);
    printf ("Flash device : %s\n", quad_spi_flash_names[controller.flash_type]);
    printf ("Manufacturer ID=0x%02x  Memory Interface Type=0x%02x  Density=0x%02x\n",
            controller.manufacturer_id, controller.memory_interface_type, controller.density);
    printf ("Flash Size Bytes=%u  Page Size Bytes=%u  Num Address Bytes=%u\n",
            controller.flash_size_bytes, controller.page_size_bytes, controller.num_address_bytes);
    quad_spi_dump_raw_parameters (&controller);

    test_spi_flash_read_modes (&controller);
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;
    uint32_t num_spi_controllers = 0;
    vfio_device_t *spi_vfio_devices[MAX_VFIO_DEVICES] = {NULL};
    uint8_t *spi_controller_regs[MAX_VFIO_DEVICES] = {NULL};

    /* Allow Fury and i2c_probe devices to be used */
    const size_t num_pci_device_filters = fury_num_pci_device_filters + 1;
    vfio_pci_device_filter_t *const pci_device_filters = calloc (num_pci_device_filters, sizeof (pci_device_filters[0]));

    memcpy (pci_device_filters, fury_pci_device_filters, fury_num_pci_device_filters * sizeof (pci_device_filters[0]));
    pci_device_filters[fury_num_pci_device_filters].vendor_id = FPGA_SIO_VENDOR_ID;
    pci_device_filters[fury_num_pci_device_filters].device_id = VFIO_PCI_DEVICE_FILTER_ANY;
    pci_device_filters[fury_num_pci_device_filters].subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID;
    pci_device_filters[fury_num_pci_device_filters].subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_I2C_PROBE;
    pci_device_filters[fury_num_pci_device_filters].enable_bus_master = false;

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, num_pci_device_filters, pci_device_filters);

    /* Create array of SPI controllers available in the opened FPGA devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        if (vfio_device_pci_filter_match (vfio_device, &pci_device_filters[fury_num_pci_device_filters]))
        {
            /* Add SPI controller in i2c_probe FPGA */
            const uint32_t bar_index = 0;

            map_vfio_device_bar_before_use (vfio_device, bar_index);
            if ((vfio_device->mapped_bars[bar_index] != NULL) && (vfio_device->regions_info[bar_index].size >= 0x3000))
            {
                uint8_t *const quad_spi_regs = &vfio_device->mapped_bars[bar_index][0x2000];

                spi_vfio_devices[num_spi_controllers] = vfio_device;
                spi_controller_regs[num_spi_controllers] = quad_spi_regs;
                num_spi_controllers++;
            }
        }
        else
        {
            /* Check for SPI controller in a NiteFury */
            uint32_t board_version;
            fury_type_t fury_type;

            fury_type = identify_fury (vfio_device, &board_version);
            if (fury_type != DEVICE_OTHER)
            {
                uint8_t *const quad_spi_regs = &vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR][FURY_AXI_QUAD_SPI_BASE_OFFSET];

                spi_vfio_devices[num_spi_controllers] = vfio_device;
                spi_controller_regs[num_spi_controllers] = quad_spi_regs;
                num_spi_controllers++;
            }
        }
    }

    /* Display SPI flash information from available controllers */
    for (uint32_t spi_index = 0; spi_index < num_spi_controllers; spi_index++)
    {
        display_spi_flash_information (spi_vfio_devices[spi_index], spi_controller_regs[spi_index]);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
