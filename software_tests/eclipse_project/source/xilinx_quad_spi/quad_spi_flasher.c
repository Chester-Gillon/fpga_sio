/*
 * @file quad_spi_flasher.c
 * @date 9 Jul 2023
 * @author Chester Gillon
 */

#include "fpga_sio_pci_ids.h"
#include "xilinx_quad_spi.h"
#include "fury_utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


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
    printf ("Manufacturer ID=0x%02x  Memory Interface Type=0x%02x  Density=0x%02x\n",
            controller.manufacturer_id, controller.memory_interface_type, controller.density);
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
