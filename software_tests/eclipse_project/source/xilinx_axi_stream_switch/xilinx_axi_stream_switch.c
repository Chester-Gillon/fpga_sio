/*
 * @file xilinx_axi_stream_switch.c
 * @date 14 Dec 2024
 * @author Chester Gillon
 * @brief An implementation to read or modify the routing for a Xilinx AXI4-Stream Switch
 * @details
 *  https://docs.amd.com/r/en-US/pg085-axi4stream-infrastructure/Register-Space defines the registers.
 *  The caller is responsible for knowing how many ports are configuration in the switch IP, since
 *  the registers don't make this information available.
 */

#include "xilinx_axi_stream_switch.h"
#include "vfio_access.h"

/* Used for committing the MI selector values from the control register block to the AXI4-Stream Switch block. */
#define GENERAL_CONTROL_OFFSET 0x0
#define GENERAL_CONTROL_REG_UPDATE 0x2


/* Offset to the first MI_MUX[0-15] Register. There is one register for each master interface port in the IP.
 * Each register is 32-bits, at consecutive offsets. */
#define MI_MUX_BASE_OFFSET 0x40u

/* Mask of the bits used to control the slave interface selection */
#define MI_MUX_VALUE_MASK 0xfu

/* Set to explicitly disable the master interface */
#define MI_MUX_DISABLE (1u << 31)


/**
 * @brief Obtain the selected slave port for a master port on a AXI4-Stream Switch
 * @param[in] reg_base Base of the control registers on the AXI4-Stream Switch to query
 * @param[in] master_port Which master port to query
 * @param[out] slave_port The selected slave port
 * @return Returns true if the master port is enabled
 */
bool xilinx_axi_switch_get_selected_slave (const uint8_t *const reg_base, const uint32_t master_port, uint32_t *const slave_port)
{
    const uint64_t mux_reg_offset = MI_MUX_BASE_OFFSET + (master_port * sizeof (uint32_t));
    const uint32_t mux_reg_value = read_reg32 (reg_base, mux_reg_offset);

    *slave_port = mux_reg_value & MI_MUX_VALUE_MASK;
    return (mux_reg_value & MI_MUX_DISABLE) == 0;
}


/**
 * @brief Set the routing for ports on a AXI4-Stream Switch
 * @details
 *   When the registers are committed, this causes the AXI4-Stream switch to go into a soft reset for approximately 16 cycles.
 *
 *   pg085 doesn't seem to define the effect of a soft reset, and tests with setting the routes while
 *   test_dma_bridge_parallel_streams was running showed:
 *   1. Forcing an update of unchanged routes, to cause a soft reset, didn't trigger any test failures.
 *   2. Disabling one route, and re-enabling a few seconds later so that the route was disabled for less than the DMA timeout
 *      caused a dip in in the throughput of the disabled route but otherwise didn't result in test failures.
 *   3. Changing the routes to be different to that initially set at the start of the test didn't result in DMA timeouts,
 *      and as expected resulted in an incorrect test pattern at the end of the test.
 * @param[in,out] reg_base Based of the the control registers on the AXI4-Stream Switch to set
 * @param[in] num_ports The number of ports to set
 * @param[in] ports Defines the port routing to set
 */
void xilinx_axi_switch_set_selected_slaves (uint8_t *const reg_base, const uint32_t num_ports,
                                            const xilinx_axi_switch_master_port_configuration_t ports[const num_ports])
{
    /* Setup registers with the required routing */
    for (uint32_t port_index = 0; port_index < num_ports; port_index++)
    {
        const xilinx_axi_switch_master_port_configuration_t *const port = &ports[port_index];
        const uint64_t mux_reg_offset = MI_MUX_BASE_OFFSET + (port->master_port * sizeof (uint32_t));

        uint32_t mux_reg_value = port->slave_port;
        if (!port->enabled)
        {
            mux_reg_value |= MI_MUX_DISABLE;
        }
        write_reg32 (reg_base, mux_reg_offset, mux_reg_value);
    }

    /* Commit registers */
    write_reg32 (reg_base, GENERAL_CONTROL_OFFSET, GENERAL_CONTROL_REG_UPDATE);
}


/**
 * @brief Update the routing for ports on a AXI4-Stream Switch
 * @details
 *   Compared to xilinx_axi_switch_set_selected_slaves() this function only modifies the routing if the current routing doesn't
 *   match the requested routing. This is to avoid an unnecessary soft reset of the switch.
 * @param[in,out] reg_base Based of the the control registers on the AXI4-Stream Switch to update
 * @param[in] num_ports The number of ports to update
 * @param[in] ports Defines the port routing to update
 * @return Returns true when the routing in the switch was updated
 */
bool xilinx_axi_switch_update_selected_slaves (uint8_t *const reg_base, const uint32_t num_ports,
                                               const xilinx_axi_switch_master_port_configuration_t requested_ports[const num_ports])
{
    bool updated = false;
    uint32_t current_slave_port;
    bool current_enabled;
    uint32_t num_matching_ports = 0;

    /* Determine the number of master ports which currently match the requested configuration */
    for (uint32_t port_index = 0; port_index < num_ports; port_index++)
    {
        const xilinx_axi_switch_master_port_configuration_t *const requested = &requested_ports[port_index];

        current_enabled = xilinx_axi_switch_get_selected_slave (reg_base, requested->master_port, &current_slave_port);
        if ((current_enabled == requested->enabled) && (current_slave_port == requested->slave_port))
        {
            num_matching_ports++;
        }
    }

    /* When the current configuration doesn't match the requested configuration, set the switch routing to the requested
     * configuration. */
    if (num_matching_ports != num_ports)
    {
        xilinx_axi_switch_set_selected_slaves (reg_base, num_ports, requested_ports);
        updated = true;
    }

    return updated;
}
