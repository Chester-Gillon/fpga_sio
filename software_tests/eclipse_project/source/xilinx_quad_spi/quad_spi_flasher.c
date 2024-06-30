/*
 * @file quad_spi_flasher.c
 * @date 9 Jul 2023
 * @author Chester Gillon
 */

#include "xilinx_quad_spi.h"
#include "xilinx_7_series_bitstream.h"
#include "identify_pcie_fpga_design.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>


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
 * @brief Parse the command line arguments
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "d:";
    int option;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case '?':
        default:
            printf ("Usage %s -d <pci_device_location>\n", argv[0]);
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


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
 * @param[in] design The FPGA design containing the quad SPI controller to use.
 */
static void display_spi_flash_information (const fpga_design_t *const design)
{
    quad_spi_controller_context_t controller;
    bool success;

    printf ("\nDisplaying information for SPI flash using %s design in PCI device %s IOMMU group %s\n",
            fpga_design_names[design->design_id], design->vfio_device->device_name, design->vfio_device->group->iommu_group_name);
    success = quad_spi_initialise_controller (&controller, design->quad_spi_regs);
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

    //@todo make these command line options
    if (false)
    {
        quad_spi_dump_raw_parameters (&controller);
        test_spi_flash_read_modes (&controller);
    }

    x7_bitstream_context_t bitstream_context;
    x7_bitstream_read_from_spi_flash (&bitstream_context, &controller, 0);
    x7_bitstream_summarise (&bitstream_context);
    x7_bitstream_free (&bitstream_context);
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Display SPI flash information from available controllers */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->quad_spi_regs != NULL)
        {
            display_spi_flash_information (design);
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
