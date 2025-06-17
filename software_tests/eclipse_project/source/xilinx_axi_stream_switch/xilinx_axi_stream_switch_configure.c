/*
 * xilinx_axi_stream_switch_configure.c
 * @date 20 Dec 2024
 * @author An implementation to configure a Xilinx AXI4-Stream Switch at the design level
 * @details
 *  A library which can handle configuring the switch from multiple programs, since the switch gets reset and all ports disabled
 *  when VFIO resets the device.
 */

#include "xilinx_axi_stream_switch_configure.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/** Delimiter for comma-separated command line arguments */
#define DELIMITER ","


/** Specifies which devices to set routes for */
static device_routing_t device_routing[MAX_VFIO_DEVICES];
static uint32_t num_devices_routed;


/* The default stream loopback routing for the designs which support it.
 * The defaults have been set to those hard coded prior to the revision of the designs which added a AXI4-Stream Switch
 * to connect the AXI-4 Streams of the "DMA/Bridge Subsystem for PCI Express".
 *
 * The device_filter field isn't used, since this array is per design rather than per device instance. */
static const device_routing_t default_stream_loopback[FPGA_DESIGN_ARRAY_SIZE] =
{
    [FPGA_DESIGN_TEF1001_DMA_STREAM_LOOPBACK] =
    {
        .num_routes = 2,
        .routes =
        {
             {.enabled = true, .master_port = 0, .slave_port = 1},
             {.enabled = true, .master_port = 1, .slave_port = 0}
        }
    },
    [FPGA_DESIGN_NITEFURY_DMA_STREAM_LOOPBACK] =
    {
        .num_routes = 2,
        .routes =
        {
             {.enabled = true, .master_port = 0, .slave_port = 1},
             {.enabled = true, .master_port = 1, .slave_port = 0}
        }
    },
    [FPGA_DESIGN_TOSING_160T_DMA_STREAM_LOOPBACK] =
    {
        .num_routes = 2,
        .routes =
        {
             {.enabled = true, .master_port = 0, .slave_port = 1},
             {.enabled = true, .master_port = 1, .slave_port = 0}
        }
    },
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_LOOPBACK] =
    {
        .num_routes = 4,
        .routes =
        {
             {.enabled = true, .master_port = 0, .slave_port = 1},
             {.enabled = true, .master_port = 1, .slave_port = 0},
             {.enabled = true, .master_port = 2, .slave_port = 3},
             {.enabled = true, .master_port = 3, .slave_port = 2}
        }
    },

    /* For this design the output packet length is a fixed size of 8 bytes as in the CRC64 result rather than a looped back
     * copy of the input packet. Adding this design did allow the stream loopback tests to be run to see what they reported. */
    [FPGA_DESIGN_XCKU5P_DUAL_QSFP_DMA_STREAM_CRC64] =
    {
        .num_routes = 4,
        .routes =
        {
             {.enabled = true, .master_port = 0, .slave_port = 0},
             {.enabled = true, .master_port = 1, .slave_port = 1},
             {.enabled = true, .master_port = 2, .slave_port = 2},
             {.enabled = true, .master_port = 3, .slave_port = 3}
        }
    }
};


/**
 * @brief Process a command line argument which is used to set the AXI4-Stream Switch for one device
 * @param[in] argument The argument text which specifies the PCI device location and zero or more routes to enable
 * @param[in] add_pci_device_location_filter When true the device is added to the vfio_access location filter
 */
void process_device_routing_argument (const char *const argument, const bool add_pci_device_location_filter)
{
    char *const device_routing_text = strdup (argument);
    char junk;
    int num_values;
    char *saveptr = NULL;
    char *device_name;
    char *route_string;

    if (num_devices_routed == MAX_VFIO_DEVICES)
    {
        fprintf (stderr, "Maximum number of devices reached\n");
        exit (EXIT_FAILURE);
    }

    /* Extract the device the routing is for */
    device_routing_t *const routes = &device_routing[num_devices_routed];
    device_name = strtok_r (device_routing_text, DELIMITER, &saveptr);
    if (device_name == NULL)
    {
        fprintf (stderr, "Failed to extract device from %s\n", argument);
        exit (EXIT_FAILURE);
    }
    num_values = sscanf (device_name, "%d:%" SCNx8 ":%" SCNx8 ".%" SCNx8 "%c",
            &routes->device_filter.domain, &routes->device_filter.bus,
            &routes->device_filter.dev, &routes->device_filter.func, &junk);
    if (num_values != 4)
    {
        fprintf (stderr, "Failed to extract device from %s\n", argument);
        exit (EXIT_FAILURE);
    }

    if (add_pci_device_location_filter)
    {
        /* Only open the devices for which routes need to set on */
        vfio_add_pci_device_location_filter (device_name);
    }

    /* Extract the routes to be set for the device */
    routes->num_routes = 0;
    route_string = strtok_r (NULL, DELIMITER, &saveptr);
    while (route_string != NULL)
    {
        xilinx_axi_switch_master_port_configuration_t *const route = &routes->routes[routes->num_routes];

        if (routes->num_routes == XILINX_AXI_STREAM_SWITCH_MAX_PORTS)
        {
            fprintf (stderr, "Maximum number of routes reached for %s\n", device_name);
            exit (EXIT_FAILURE);
        }

        num_values = sscanf (route_string, "%" SCNu32 ":%" SCNu32 "%c", &route->master_port, &route->slave_port, &junk);
        if (num_values != 2)
        {
            fprintf (stderr, "Failed to extract ports from %s\n", route_string);
            exit (EXIT_FAILURE);
        }
        route->enabled = true;

        routes->num_routes++;
        route_string = strtok_r (NULL, DELIMITER, &saveptr);
    }

    num_devices_routed++;
    free (device_routing_text);
}


/**
 * @brief Get the requested AXI4-Stream Switch routing to be used for a device
 * @details
 *  This is the routing specified on the command line arguments, or failing that default routing.
 *
 *  Exits the process with an error if the command line arguments for the port numbers are out of range.
 *  The validation checks on the port numbers are not done in process_device_routing_argument() since
 *  process_device_routing_argument() is called before the designs have been identified.
 * @param[in] The device instance of a design to get the routing for
 * @param[out] routing The routing to be used for the design
 * @return Returns how the routing was selected
 */
device_routing_selection_t get_requested_routing_for_device (const fpga_design_t *const design, device_routing_t *const routing)
{
    device_routing_selection_t selection = DEVICE_ROUTING_NONE;

    /* First priority is the routing specified by the command line arguments */
    memset (routing, 0, sizeof (*routing));
    for (uint32_t device_index = 0; (selection == DEVICE_ROUTING_NONE) && (device_index < num_devices_routed); device_index++)
    {
        device_routing_t *const routes = &device_routing[device_index];

        if ((routes->device_filter.domain == design->vfio_device->pci_dev->domain) &&
            (routes->device_filter.bus    == design->vfio_device->pci_dev->bus   ) &&
            (routes->device_filter.dev    == design->vfio_device->pci_dev->dev   ) &&
            (routes->device_filter.func   == design->vfio_device->pci_dev->func  )   )
        {
            /* Initialise the requested routes for all master ports to disable */
            routing->num_routes = design->axi_switch_num_master_ports;
            for (uint32_t master_port = 0; master_port < design->axi_switch_num_master_ports; master_port++)
            {
                routing->routes[master_port].master_port = master_port;
                routing->routes[master_port].enabled = false;
            }

            /* Add the enabled routes set from the command line arguments */
            for (uint32_t route_index = 0; route_index < routes->num_routes; route_index++)
            {
                const xilinx_axi_switch_master_port_configuration_t *const route = &routes->routes[route_index];

                if (route->master_port >= design->axi_switch_num_master_ports)
                {
                    printf ("master_port %u outside of range for device %s design %s\n",
                            route->master_port, design->vfio_device->device_name, fpga_design_names[design->design_id]);
                    exit (EXIT_FAILURE);
                }
                else if (route->slave_port >= design->axi_switch_num_slave_ports)
                {
                    printf ("slave_port %u outside of range for device %s design %s\n",
                            route->slave_port, design->vfio_device->device_name, fpga_design_names[design->design_id]);
                    exit (EXIT_FAILURE);
                }

                routing->routes[route->master_port] = *route;
            }

            selection = DEVICE_ROUTING_COMMAND_LINE;
        }
    }

    /* Second priority is the defaults for the design */
    if (selection == DEVICE_ROUTING_NONE)
    {
        if (default_stream_loopback[design->design_id].num_routes > 0)
        {
            *routing = default_stream_loopback[design->design_id];
            routing->device_filter.domain = design->vfio_device->pci_dev->domain;
            routing->device_filter.bus    = design->vfio_device->pci_dev->bus;
            routing->device_filter.dev    = design->vfio_device->pci_dev->dev;
            routing->device_filter.func   = design->vfio_device->pci_dev->func;
            selection = DEVICE_ROUTING_DEFAULT;
        }
    }

    return selection;
}


/**
 * @brief Configure the routing for a device, and return the routing in use
 * @details
 *  Handles conditions of:
 *  1. Setting routing specified on the command line, as a result of previous calls to process_device_routing_argument()
 *  2. Setting default routing if none specified on the command line and the switch currently has no routes enabled.
 *  3. When no routing specified on the command line and at least one route currently enabled in the switch, return
 *     the current routing without changing it.
 *  4. When the design revision doesn't contain a switch, return the fixed routing.
 *
 *  AXI4-Stream Switch connections in the stream_loopback designs are:
 *  - Switch master ports connected to C2H streams
 *  - Switch slave ports connected to H2C streams
 * @param[in] The device instance of a design to get the routing for
 * @param[out] routing The routing which has been configured.
 */
void configure_routing_for_device (const fpga_design_t *const design, device_routing_t *const routing)
{
    bool routes_updated;
    const device_routing_selection_t selection = get_requested_routing_for_device (design, routing);

    switch (selection)
    {
    case DEVICE_ROUTING_NONE:
        /* It is a bug to get here, since this function should only be called for designs in which routing can be selected */
        printf ("No routing available for device %s design %s\n",
                design->vfio_device->device_name, fpga_design_names[design->design_id]);
        exit (EXIT_FAILURE);
        break;

    case DEVICE_ROUTING_COMMAND_LINE:
        /* When routing was specified on the command line, always update the actual routing to match that specified
         * on the command line.
         *
         * get_requested_routing_for_device() will have aborted the process with an error if the device revision doesn't
         * contain a AXI4-Stream Switch, so no need for a conditional check on check axi_switch_regs being non-NULL. */
        routes_updated = xilinx_axi_switch_update_selected_slaves (design->axi_switch_regs,
                design->axi_switch_num_master_ports, routing->routes);
        if (routes_updated)
        {
            printf ("Device %s design %s routes updated\n",
                    design->vfio_device->device_name, fpga_design_names[design->design_id]);
        }
        break;

    case DEVICE_ROUTING_DEFAULT:
        /* With default routing may be called for a design revision which contains fixed routing without a AXI4-Stream Switch.
         * Therefore, if no switch present just return the fixed routing populate by get_requested_routing_for_device(). */
        if (design->axi_switch_regs != NULL)
        {
            uint32_t master_port;
            bool enabled_ports[XILINX_AXI_STREAM_SWITCH_MAX_PORTS];
            uint32_t slave_ports[XILINX_AXI_STREAM_SWITCH_MAX_PORTS];
            uint32_t num_enabled_ports = 0;

            /* Read the current routing from the switch */
            for (master_port = 0; master_port < design->axi_switch_num_master_ports; master_port++)
            {
                enabled_ports[master_port] =
                        xilinx_axi_switch_get_selected_slave (design->axi_switch_regs, master_port, &slave_ports[master_port]);
                if (enabled_ports[master_port])
                {
                    num_enabled_ports++;
                }
           }

            if (num_enabled_ports == 0)
            {
                /* With no enabled ports in the current routing, set the actual routing to the defaults */
                routes_updated = xilinx_axi_switch_update_selected_slaves (design->axi_switch_regs,
                        design->axi_switch_num_master_ports, routing->routes);
                if (routes_updated)
                {
                    printf ("Device %s design %s routes updated\n",
                            design->vfio_device->device_name, fpga_design_names[design->design_id]);
                }
            }
            else
            {
                /* With at least one enabled port in the current routing, return the current routing leaving
                 * the actual routing in the switch unchanged. */
                routing->num_routes = design->axi_switch_num_master_ports;
                for (master_port = 0; master_port < design->axi_switch_num_master_ports; master_port++)
                {
                    xilinx_axi_switch_master_port_configuration_t *const route = &routing->routes[master_port];

                    route->master_port = master_port;
                    route->enabled = enabled_ports[master_port];
                    route->slave_port = slave_ports[master_port];
                }
            }
        }
        break;
    }
}
