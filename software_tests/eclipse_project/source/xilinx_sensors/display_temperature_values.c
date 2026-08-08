/*
 * @file display_temperature_values.c
 * @date 8 Aug 2026
 * @author Chester Gillon
 * @brief Display current temperature measurements
 * @details
 *   Displays just the current temperature measurements from either:
 *   a. Xilinx "Analog-to-Digital Converter (XADC)"
 *   b. Xilinx "UltraScale Architecture System Monitor (SYSMON)"
 *   c. Xilinx Card Management Solution Subsystem (CMS Subsystem)
 *
 *   This is designed to be a cut-down output compared to display_sensor_values, for comparing the temperatures against other
 *   standard sensors read by "sensors".
 *
 *   Only command line options are optional PCIe device filters, to select a sub-set of VFIO devices to open.
 */

#include "xilinx_xadc.h"
#include "xilinx_sysmon.h"
#include "identify_pcie_fpga_design.h"
#include "xilinx_cms.h"

#include <stdlib.h>
#include <stdio.h>


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    xadc_sample_collection_t xadc_collection;
    sysmon_device_collection_t sysmon_collection;
    xilinx_cms_context_t cms_context;
    cms_sensor_collection_t cms_collection;

    if (argc > 1)
    {
        /* Process any optional device filter arguments */
        for (int arg_index = 1; arg_index < argc; arg_index++)
        {
            vfio_add_pci_device_location_filter (argv[arg_index]);
        }
    }

    identify_pcie_fpga_designs (&designs);

    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if ((design->xadc_regs != NULL) || (design->sysmon_regs != NULL) || design->cms_subsystem_present)
        {
            printf ("\n%s in PCI device %s IOMMU group %s:\n",
                    fpga_design_names[design->design_id], design->vfio_device->device_name,
                    design->vfio_device->group->iommu_group_name);

            if (design->xadc_regs != NULL)
            {
                read_xadc_samples (&xadc_collection, design->xadc_regs);
                display_xadc_temperatures (&xadc_collection);
            }

            if (design->sysmon_regs != NULL)
            {
                read_sysmon_samples (&sysmon_collection, design->sysmon_regs, design->num_sysmon_slaves);
                display_sysmon_temperatures (&sysmon_collection);
            }

            if (design->cms_subsystem_present)
            {
                if (cms_initialise_access (&cms_context, design->vfio_device,
                        design->cms_subsystem_bar_index, design->cms_subsystem_base_offset))
                {
                    cms_read_sensors (&cms_context, &cms_collection);
                    cms_display_temperatures (&cms_collection);
                }
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
