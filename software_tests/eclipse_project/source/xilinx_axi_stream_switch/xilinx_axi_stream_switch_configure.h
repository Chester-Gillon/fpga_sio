/*
 * @file xilinx_axi_stream_switch_configure.h
 * @date 20 Dec 2024
 * @author Chester Gillon
 * @brief Provides an interface to configure a Xilinx AXI4-Stream Switch at the design level
 */

#ifndef XILINX_AXI_STREAM_SWITCH_CONFIGURE_H_
#define XILINX_AXI_STREAM_SWITCH_CONFIGURE_H_

#include "xilinx_axi_stream_switch.h"
#include "identify_pcie_fpga_design.h"


/* Contains the switch routing for one device */
typedef struct
{
    /* The location of the PCI device containing the switch to set the routing for */
    vfio_pci_device_location_filter_t device_filter;
    /* The number of master ports with routes */
    uint32_t num_routes;
    /* The switch routes */
    xilinx_axi_switch_master_port_configuration_t routes[XILINX_AXI_STREAM_SWITCH_MAX_PORTS];
} device_routing_t;


/* Indicates how the switch routing was selected for one device */
typedef enum
{
    /* No routing defined */
    DEVICE_ROUTING_NONE,
    /* Using routing specified from the command line */
    DEVICE_ROUTING_COMMAND_LINE,
    /* Using compiled in defaults */
    DEVICE_ROUTING_DEFAULT
} device_routing_selection_t;


void process_device_routing_argument (const char *const argument, const bool add_pci_device_location_filter);
device_routing_selection_t get_requested_routing_for_device (const fpga_design_t *const design, device_routing_t *const routing);
void configure_routing_for_device (const fpga_design_t *const design, device_routing_t *const routing);


#endif /* XILINX_AXI_STREAM_SWITCH_CONFIGURE_H_ */
