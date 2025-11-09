/*
 * @file display_sensor_values.c
 * @date 16 Sep 2023
 * @author Chester Gillon
 * @brief Display the current FPGA sensor values for supported designs
 * @details
 *  Can display sensor values for either:
 *  a. Xilinx "Analog-to-Digital Converter (XADC)"
 *  b. Xilinx "UltraScale Architecture System Monitor (SYSMON)"
 *
 *  This was originally created to just support XADC, with SYSMON support added later. Even though there is some overlap
 *  between XADC and SYSMON there are separate implementations of the code to read and display the samples.
 */

#include "xilinx_xadc.h"
#include "xilinx_sysmon.h"
#include "identify_pcie_fpga_design.h"
#include "xilinx_cms.h"

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
    xadc_sample_collection_t xadc_collection;
    sysmon_device_collection_t sysmon_collection;
    xilinx_cms_context_t cms_context;
    cms_sensor_collection_t cms_collection;

    parse_command_line_arguments (argc, argv);

    identify_pcie_fpga_designs (&designs);

    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->xadc_regs != NULL)
        {
            read_xadc_samples (&xadc_collection, design->xadc_regs);
            printf ("Displaying XADC values for design %s in PCI device %s IOMMU group %s:\n",
                    fpga_design_names[design->design_id], design->vfio_device->device_name,
                    design->vfio_device->group->iommu_group_name);
            display_xadc_samples (&xadc_collection);
            printf ("\n");
        }

        if (design->sysmon_regs != NULL)
        {
            read_sysmon_samples (&sysmon_collection, design->sysmon_regs, design->num_sysmon_slaves);
            printf ("Displaying SYSMON values for design %s in PCI device %s IOMMU group %s:\n",
                    fpga_design_names[design->design_id], design->vfio_device->device_name,
                    design->vfio_device->group->iommu_group_name);
            display_sysmon_samples (&sysmon_collection);
            printf ("\n");
        }

        if (design->cms_subsystem_present)
        {
            if (cms_initialise_access (&cms_context, design->vfio_device,
                    design->cms_subsystem_bar_index, design->cms_subsystem_base_offset))
            {
                cms_read_sensors (&cms_context, &cms_collection);
                printf ("Displaying CMS values for design %s in PCI device %s IOMMU group %s:\n",
                        fpga_design_names[design->design_id], design->vfio_device->device_name,
                        design->vfio_device->group->iommu_group_name);
                cms_display_sensors (&cms_collection);
                printf ("\n");
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
