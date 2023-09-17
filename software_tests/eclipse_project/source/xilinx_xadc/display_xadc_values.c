/*
 * @file display_xadc_values.c
 * @date 16 Sep 2023
 * @author Chester Gillon
 * @brief Display the current Xilinx "Analog-to-Digital Converter (XADC)" values for supported designs
 */

#include "xilinx_xadc.h"
#include "identify_pcie_fpga_design.h"

#include <stdlib.h>
#include <stdio.h>


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    xadc_sample_collection_t collection;

    identify_pcie_fpga_designs (&designs);

    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->xadc_regs != NULL)
        {
            read_xadc_samples (&collection, design->xadc_regs);
            printf ("Displaying XADC values for design %s in PCI device %s IOMMU group %s:\n",
                    fpga_design_names[design->design_id], design->vfio_device->device_name, design->vfio_device->iommu_group);
            display_xadc_samples (&collection);
            printf ("\n");
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
