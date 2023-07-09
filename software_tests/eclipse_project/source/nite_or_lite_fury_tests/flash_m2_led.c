/*
 * @file flash_m2_led.c
 * @date 18 Jun 2023
 * @author Chester Gillon
 * @brief Cause the M2 disk access LED on a NiteFury or LiteFury to flash
 * @details This uses the M2 LED signal on the NiteFury / LiteFury to toggle, which should light the disk access LED on the
 *          PC the NiteFury / LiteFury is fitted in a M.2 NVME slot on the PC motherboard.
 *
 *          As the disk access LED might be driven by other disks in the PC, to see the effect should be run when no
 *          other disk access is occurring.
 */

#include "fury_utils.h"

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;
    uint32_t board_version;
    fury_type_t fury_type;
    useconds_t led_on_time_us;
    char junk;

    /* Read command line arguments */
    if (argc != 2)
    {
        printf ("Usage: %s <led_on_time_us>\n", argv[0]);
        exit (EXIT_FAILURE);
    }
    const char *const led_on_time_str = argv[1];
    if (sscanf (led_on_time_str, "%u%c", &led_on_time_us, &junk) != 1)
    {
        printf ("Invalid led_on_time_us %s\n", led_on_time_str);
        exit (EXIT_FAILURE);
    }

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    /* Process any NiteFury or LiteFury devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        /* The version at which the M2_LED output was changed from push-pull to open-collector */
        const uint32_t min_supported_board_version = 3;

        /* The M2 led signal is connected to bit 1 of the GPIO2 output register in the axi_gpio_2 AXI GPIO IP */
        const uint32_t gpio2_o_offset = 8;
        const uint32_t m2_led_mask    = 2;

        fury_type = identify_fury (vfio_device, &board_version);
        if (fury_type != DEVICE_OTHER)
        {
            if (board_version >= min_supported_board_version)
            {
                uint8_t *const axi_gpio_2_regs = &vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR][FURY_AXI_GPIO_2_BASE_OFFSET];

                printf ("Testing %s board version 0x%x for PCI device %s IOMMU group %s\n",
                        fury_names[fury_type], board_version, vfio_device->device_name, vfio_device->iommu_group);

                /* Set the M2 LED signal active, delay, and then back to inactive.
                 * Since the AXI GPIO IP doesn't support read-back of the current outputs, this may change
                 * other output bits. */
                write_reg32 (axi_gpio_2_regs, gpio2_o_offset, m2_led_mask);
                usleep (led_on_time_us);
                write_reg32 (axi_gpio_2_regs, gpio2_o_offset, 0);
            }
            else
            {
                printf ("Board version 0x%x doesn't support correct M2 LED signal drive\n", board_version);
            }
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
