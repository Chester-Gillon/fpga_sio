/*
 * @file display_identified_pcie_fpga_designs.c
 * @date 22 Oct 2023
 * @author Chester Gillon
 * @brief Display the FPGA designs with a PCIe interface in the PC which are known by the identify_pcie_fpga_design library
 */

#include "identify_pcie_fpga_design.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>


/**
 * @brief Display information about a peripheral which is present in an identified design
 * @param[in] design The identified design to check for the peripheral
 * @param[in] peripheral_name The name of the peripheral
 * @param[in] peripheral_mapped_base If non-NULL the mapped base of the peripheral which is present in the design.
 */
static void display_design_present_peripheral (const fpga_design_t *const design,
                                               const char *const peripheral_name, const uint8_t *const peripheral_mapped_base)
{
    if (peripheral_mapped_base != NULL)
    {
        /* The peripheral is present since its registers are mapped.
         * Search to find the offset into which BAR the registers are mapped to. */
        bool found_bar;
        uint32_t bar_number;
        ptrdiff_t bar_offset;

        found_bar = false;
        bar_number = 0;
        bar_offset = 0;
        while (!found_bar && (bar_number < PCI_STD_NUM_BARS))
        {
            if (design->vfio_device->mapped_bars[bar_number] != NULL)
            {
                const uint8_t *const mapped_bar_start = design->vfio_device->mapped_bars[bar_number];
                const uint8_t *const mapped_bar_end = &mapped_bar_start[design->vfio_device->regions_info[bar_number].size];

                if ((peripheral_mapped_base >= mapped_bar_start) && (peripheral_mapped_base < mapped_bar_end))
                {
                    bar_offset = peripheral_mapped_base - mapped_bar_start;
                    found_bar = true;
                }
            }

            if (!found_bar)
            {
                bar_number++;
            }
        }

        if (found_bar)
        {
            printf ("  %s registers at bar %u offset 0x%lx\n", peripheral_name, bar_number, bar_offset);
        }
        else
        {
            printf ("  %s register at mapped address %p (unable to identify bar)\n", peripheral_name, peripheral_mapped_base);
        }
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Display the identified designs */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        const fpga_design_t *const design = &designs.designs[design_index];

        printf ("\nDesign %s", fpga_design_names[design->design_id]);
        if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
        {
            printf (" version 0x%x", design->board_version);
        }
        printf (":\n");
        printf ("  PCI device %s IOMMU group %s\n", design->vfio_device->device_name, design->vfio_device->iommu_group);
        if (design->dma_bridge_present)
        {
            printf ("  DMA bridge bar %u memory size 0x%zx\n", design->dma_bridge_bar, design->dma_bridge_memory_size_bytes);
        }

        display_design_present_peripheral (design, "Quad SPI", design->quad_spi_regs);
        display_design_present_peripheral (design, "XADC", design->xadc_regs);
        display_design_present_peripheral (design, "IIC", design->iic_regs);
        display_design_present_peripheral (design, "bit-banged I2C GPIO", design->bit_banged_i2c_gpio_regs);
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
