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

#include "vfio_access.h"
#include "identify_pcie_fpga_design.h"

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
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

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Process any NiteFury or LiteFury devices found */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        /* The version at which the M2_LED output was changed from push-pull to open-collector */
        const uint32_t min_supported_board_version = 3;

        /* The M2 led signal is connected to bit 1 of the GPIO2 output register in the axi_gpio_2 AXI GPIO IP */
        const uint32_t gpio2_o_offset = 8;
        const uint32_t m2_led_mask    = 2;

        if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
        {
            if (design->board_version >= min_supported_board_version)
            {
                uint8_t *const axi_gpio_2_regs = map_vfio_registers_block (design->vfio_device, FURY_PROJECT0_AXI_PERIPHERALS_BAR,
                        FURY_PROJECT0_GPIO_2_BASE_OFFSET, FURY_PROJECT0_PERIPHERAL_FRAME_SIZE);

                if (axi_gpio_2_regs != NULL)
                {
                    printf ("Testing %s board version 0x%x for PCI device %s IOMMU group %s\n",
                            fpga_design_names[design->design_id], design->board_version,
                            design->vfio_device->device_name, design->vfio_device->iommu_group);

                    /* Set the M2 LED signal active, delay, and then back to inactive.
                     * Since the AXI GPIO IP doesn't support read-back of the current outputs, this may change
                     * other output bits. */
                    write_reg32 (axi_gpio_2_regs, gpio2_o_offset, m2_led_mask);
                    usleep (led_on_time_us);
                    write_reg32 (axi_gpio_2_regs, gpio2_o_offset, 0);
                }
                else
                {
                    printf ("Unable to map GPIO 2 registers\n");
                }
            }
            else
            {
                printf ("Board version 0x%x doesn't support correct M2 LED signal drive\n", design->board_version);
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
