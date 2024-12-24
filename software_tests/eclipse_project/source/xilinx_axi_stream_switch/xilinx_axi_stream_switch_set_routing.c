/*
 * @file xilinx_axi_stream_switch_set_routing.c
 * @date 22 Dec 2024
 * @author Chester Gillon
 * @brief Utility to set routing for a Xilinx AXI4-Stream Switch
 */

#include "xilinx_axi_stream_switch_configure.h"
#include "xilinx_axi_stream_switch.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <getopt.h>


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"device_routing", required_argument, NULL, 0},
    {"pause_before_vfio_close", no_argument, NULL, 0},
    {"force_update", no_argument, NULL, 0},
    {"default_routing", no_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};


/** Command line argument which enabled pausing before starting the VFIO close */
static bool arg_pause_before_vfio_close;


/** Command line argument which forces an update of the switch, even if no change to the routing */
static bool arg_force_update;


/** Command line argument which causes default routing to set in all supported devices */
static bool arg_default_routing;


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("  xilinx_axi_stream_switch_set_routing <options>\n");
    printf ("  Utility to set routing for a Xilinx AXI4-Stream Switch\n");
    printf ("\n");
    printf ("--device_routing <domain>:<bus>:<dev>.<func>[,<master_port>:<slave_port>]\n");
    printf ("  Specify a PCI device to set the AXI4-Stream Switch routing for.\n");
    printf ("  The routing in specified as zero or more pairs of the master port and the\n");
    printf ("  slave port used for the route. Unspecified master ports are left disabled\n");
    printf ("  May be used more than once.\n");
    printf ("--pause_before_vfio_close\n");
    printf ("  Pauses before closing the VFIO devices. This is because the switch gets reset\n");
    printf ("  and all ports disabled when VFIO resets the device when no longer open by any\n");
    printf ("  process.\n");
    printf ("--force_update\n");
    printf ("  Forces an update of the switch, even if no change to the routing.\n");
    printf ("  May be used to investigate if the soft-reset when updating the routing causes\n");
    printf ("  failures on stream transfers in progress.\n");
    printf ("--default_routing\n");
    printf ("  Causes default routing to be set in all supported devices.\n");
    printf ("  Can't be used at the same time as --device_routing\n");

    exit (EXIT_FAILURE);
}


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    int opt_status;
    bool device_routing_specified = false;

    do
    {
        int option_index = 0;

        opt_status = getopt_long (argc, argv, "", command_line_options, &option_index);
        if (opt_status == '?')
        {
            display_usage ();
        }
        else if (opt_status >= 0)
        {
            const struct option *const optdef = &command_line_options[option_index];

            if (optdef->flag != NULL)
            {
                /* Argument just sets a flag */
            }
            else if (strcmp (optdef->name, "device_routing") == 0)
            {
                /* Only open devices for which want to set the routing for */
                const bool add_pci_device_location_filter = true;

                process_device_routing_argument (optarg, add_pci_device_location_filter);
                device_routing_specified = true;
            }
            else if (strcmp (optdef->name, "pause_before_vfio_close") == 0)
            {
                arg_pause_before_vfio_close = true;
            }
            else if (strcmp (optdef->name, "force_update") == 0)
            {
                arg_force_update = true;
            }
            else if (strcmp (optdef->name, "default_routing") == 0)
            {
                arg_default_routing = true;
            }
            else
            {
                /* This is a program error, and shouldn't be triggered by the command line options */
                fprintf (stderr, "Unexpected argument definition %s\n", optdef->name);
                exit (EXIT_FAILURE);
            }
        }
    } while (opt_status != -1);

    if (!arg_default_routing && !device_routing_specified)
    {
        fprintf (stderr, "Either default_routing or at least one device_routing option must be specified\n");
        exit (EXIT_FAILURE);
    }
    else if (arg_default_routing && device_routing_specified)
    {
        fprintf (stderr, "Use of both default_routing and device_routing options is invalid\n");
        exit (EXIT_FAILURE);
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    uint32_t num_devices_processed;
    bool routes_updated;
    device_routing_selection_t selection;
    device_routing_t routing;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned, and have been selected by the command line arguments */
    identify_pcie_fpga_designs (&designs);

    /* Process the devices which have been selected to set the routes for */
    num_devices_processed = 0;
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        const fpga_design_t *const design = &designs.designs[design_index];

        if (design->axi_switch_regs != NULL)
        {
            selection = get_requested_routing_for_device (design, &routing);
            if (selection != DEVICE_ROUTING_NONE)
            {

                if (arg_force_update)
                {
                    /* Force an update of the routes in the device */
                    xilinx_axi_switch_set_selected_slaves (design->axi_switch_regs,
                            design->axi_switch_num_master_ports, routing.routes);
                    printf ("Device %s design %s routes update forced\n",
                            design->vfio_device->device_name, fpga_design_names[design->design_id]);
                }
                else
                {
                    /* Update the routes in the device, indicating if there was any actual updates to the switch */
                    routes_updated = xilinx_axi_switch_update_selected_slaves (design->axi_switch_regs,
                            design->axi_switch_num_master_ports, routing.routes);
                    printf ("Device %s design %s routes %s\n",
                            design->vfio_device->device_name, fpga_design_names[design->design_id],
                            routes_updated ? "updated" : "unchanged");
                }

                num_devices_processed++;
            }
        }
    }

    if (num_devices_processed == 0)
    {
        printf ("No devices processed, the devices specified on the command line either:\n");
        printf ("- Don't exist\n");
        printf ("- Don't have a AXI4-Stream Switch\n");
    }
    else if (arg_pause_before_vfio_close)
    {
        printf ("Routes processed in %u devices. Press return to close the VFIO devices.\n", num_devices_processed);
        getchar ();
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
