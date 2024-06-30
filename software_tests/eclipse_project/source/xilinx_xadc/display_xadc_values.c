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

#include <unistd.h>


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


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    xadc_sample_collection_t collection;

    parse_command_line_arguments (argc, argv);

    identify_pcie_fpga_designs (&designs);

    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->xadc_regs != NULL)
        {
            read_xadc_samples (&collection, design->xadc_regs);
            printf ("Displaying XADC values for design %s in PCI device %s IOMMU group %s:\n",
                    fpga_design_names[design->design_id], design->vfio_device->device_name,
                    design->vfio_device->group->iommu_group_name);
            display_xadc_samples (&collection);
            printf ("\n");
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
