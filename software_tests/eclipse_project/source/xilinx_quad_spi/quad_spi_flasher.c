/*
 * @file quad_spi_flasher.c
 * @date 9 Jul 2023
 * @author Chester Gillon
 */

#include "xilinx_quad_spi.h"
#include "fury_utils.h"

#include <stdlib.h>
#include <stdio.h>


static void display_spi_flash_information (const vfio_device_t *const vfio_device, uint8_t *const quad_spi_regs)
{
    quad_spi_controller_context_t controller;
    bool success;

    printf ("\nDisplaying information to SPI flash in PCI device %s IOMMU group %s\n",
            vfio_device->device_name, vfio_device->iommu_group);
    success = quad_spi_initialise_controller (&controller, quad_spi_regs);
    if (!success)
    {
        printf ("Failed to initialise Quad SPI controller\n");
        return;
    }

    printf ("FIFO depth=%u\n", controller.fifo_depth);
    printf ("Manufacturer ID=0x%02x  Memory Interface Type=0x%02x  Density=0x%02x\n",
            controller.manufacturer_id, controller.memory_interface_type, controller.density);
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];
        uint32_t board_version;
        fury_type_t fury_type;

        fury_type = identify_fury (vfio_device, &board_version);
        if (fury_type != DEVICE_OTHER)
        {
            uint8_t *const quad_spi_regs = &vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR][FURY_AXI_QUAD_SPI_BASE_OFFSET];

            display_spi_flash_information (vfio_device, quad_spi_regs);
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
