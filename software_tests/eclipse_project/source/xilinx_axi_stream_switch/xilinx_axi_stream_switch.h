/*
 * @file xilinx_axi_stream_switch.h
 * @date 14 Dec 2024
 * @author Chester Gillon
 * @brief Provides an interface to read or modify the routing for a Xilinx AXI4-Stream Switch
 */

#ifndef XILINX_AXI_STREAM_SWITCH_H_
#define XILINX_AXI_STREAM_SWITCH_H_

#include <stdbool.h>
#include <stdint.h>

/* The maximum number of master or slave ports which can be configured in the switch IP, to be able to arrays */
#define XILINX_AXI_STREAM_SWITCH_MAX_PORTS 16


/* Defines the configuration for one master port on a AXI4-Stream Switch */
typedef struct
{
    /* Which master port to configure */
    uint32_t master_port;
    /* When true the master port is enabled */
    bool enabled;
    /* Which slave port the enabled master port is routed to */
    uint32_t slave_port;
} xilinx_axi_switch_master_port_configuration_t;


bool xilinx_axi_switch_get_selected_slave (const uint8_t *const reg_base, const uint32_t master_port, uint32_t *const slave_port);
void xilinx_axi_switch_set_selected_slaves (uint8_t *const reg_base, const uint32_t num_ports,
                                            const xilinx_axi_switch_master_port_configuration_t ports[const num_ports]);
bool xilinx_axi_switch_update_selected_slaves (uint8_t *const reg_base, const uint32_t num_ports,
                                               const xilinx_axi_switch_master_port_configuration_t requested_ports[const num_ports]);

#endif /* XILINX_AXI_STREAM_SWITCH_H_ */
