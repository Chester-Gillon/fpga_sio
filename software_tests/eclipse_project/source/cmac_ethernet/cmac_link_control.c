/*
 * @file cmac_link_control.c
 * @date Oct 12, 2025
 * @author Chester Gillon
 * @brief Initial test of controlling CMAC 100G Ethernet module to see if can get link to come up on a ConnectX device
 */

#include "cmac_axi4_lite_registers.h"
#include "identify_pcie_fpga_design.h"
#include "vfio_bitops.h"

#include <stdlib.h>
#include <stdio.h>


/**
 * @brief Display a sample of CMAC registers for diagnostic information
 * @param[in] cmac_registers Base of the CMAC registers
 */
static void display_cmac_registers (const uint8_t *const cmac_registers)
{
    const uint32_t core_version_reg = read_reg32 (cmac_registers, CORE_VERSION_REG_OFFSET);
    printf ("core_version = %u.%u\n", vfio_extract_field_u32 (core_version_reg, CORE_VERSION_REG_MAJOR_MASK),
            vfio_extract_field_u32 (core_version_reg, CORE_VERSION_REG_MINOR_MASK));

    const uint32_t gt_reset_reg = read_reg32 (cmac_registers, GT_RESET_REG_OFFSET);
    printf ("gt_reset_all = %u\n", vfio_extract_field_u32 (gt_reset_reg, GT_RESET_REG_GT_RESET_ALL_MASK));

    const uint32_t reset_reg = read_reg32 (cmac_registers, RESET_REG_OFFSET);
    printf ("usr_rx_serdes_reset = %u\n", vfio_extract_field_u32 (reset_reg, RESET_REG_USR_RX_SERDES_RESET_MASK));
    printf ("usr_rx_reset = %u\n", vfio_extract_field_u32 (reset_reg, RESET_REG_USR_RX_RESET_MASK));
    printf ("usr_tx_reset = %u\n", vfio_extract_field_u32 (reset_reg, RESET_REG_USR_TX_RESET_MASK));

    /* This register is valid for Runtime Switch mode only.
     * When Runtime Switch mode wasn't configured, read back all all ones, which think is invalid. */
    const uint32_t switch_core_mode_reg = read_reg32 (cmac_registers, SWITCH_CORE_MODE_REG_OFFSET);
    printf ("switch_core_mode_reg = %u\n", vfio_extract_field_u32 (switch_core_mode_reg, SWITCH_CORE_MODE_REG_MASK));

    const uint32_t configuration_tx_reg1 = read_reg32 (cmac_registers, CONFIGURATION_TX_REG1_OFFSET);
    printf ("configuration_tx_reg1_ctl_tx_enable = %u\n",
            vfio_extract_field_u32 (configuration_tx_reg1, CONFIGURATION_TX_REG1_CTL_TX_ENABLE_MASK));
    printf ("configuration_tx_reg1_ctl_tx_send_lfi = %u\n",
            vfio_extract_field_u32 (configuration_tx_reg1, CONFIGURATION_TX_REG1_CTL_TX_SEND_LFI_MASK));
    printf ("configuration_tx_reg1_ctl_tx_send_rfi = %u\n",
            vfio_extract_field_u32 (configuration_tx_reg1, CONFIGURATION_TX_REG1_CTL_TX_SEND_RFI_MASK));
    printf ("configuration_tx_reg1_ctl_tx_send_idle = %u\n",
            vfio_extract_field_u32 (configuration_tx_reg1, CONFIGURATION_TX_REG1_CTL_TX_SEND_IDLE_MASK));
    printf ("configuration_tx_reg1_ctl_tx_test_pattern = %u\n",
            vfio_extract_field_u32 (configuration_tx_reg1, CONFIGURATION_TX_REG1_CTL_TX_TEST_PATTERN_MASK));

    const uint32_t core_mode_reg = read_reg32 (cmac_registers, CORE_MODE_REG_OFFSET);
    printf ("core_mode_reg = %u\n", vfio_extract_field_u32 (core_mode_reg, CORE_MODE_REG_MASK));

    const uint32_t rsfec_config_enable = read_reg32 (cmac_registers, RSFEC_CONFIG_ENABLE_OFFSET);
    printf ("rsfec_config_enable_ctl_rx_rsfec_enable = %u\n",
            vfio_extract_field_u32 (rsfec_config_enable, RSFEC_CONFIG_ENABLE_CTL_RX_RSFEC_ENABLE_MASK));
    printf ("rsfec_config_enable_ctl_tx_rsfec_enable = %u\n",
            vfio_extract_field_u32 (rsfec_config_enable, RSFEC_CONFIG_ENABLE_CTL_TX_RSFEC_ENABLE_MASK));
}



int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    bool keep_open = false;

    identify_pcie_fpga_designs (&designs);

    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        for (uint32_t port_index = 0; port_index < design->num_cmac_ports; port_index++)
        {
            uint8_t *const cmac_registers = design->cmac_ports[port_index].cmac_regs;

            if (cmac_registers != NULL)
            {
                display_cmac_registers (cmac_registers);

                /* If RSFEC is disabled, enable it and then re-display the registers */
                uint32_t rsfec_config_enable = read_reg32 (cmac_registers, RSFEC_CONFIG_ENABLE_OFFSET);
                if ((rsfec_config_enable & RSFEC_CONFIG_ENABLE_CTL_TX_RSFEC_ENABLE_MASK) == 0)
                {
                    printf ("\nEnabling TX RSFEC\n");
                    rsfec_config_enable |= RSFEC_CONFIG_ENABLE_CTL_TX_RSFEC_ENABLE_MASK;
                    write_reg32 (cmac_registers, RSFEC_CONFIG_ENABLE_OFFSET, rsfec_config_enable);
                    display_cmac_registers (cmac_registers);
                    keep_open = true;
                }

                /* If transmit is disabled, enable it and then re-display the registers */
                uint32_t configuration_tx_reg1 = read_reg32 (cmac_registers, CONFIGURATION_TX_REG1_OFFSET);
                if ((configuration_tx_reg1 & CONFIGURATION_TX_REG1_CTL_TX_ENABLE_MASK) == 0)
                {
                    printf ("\nSetting TX_ENABLE\n");
                    configuration_tx_reg1 |= CONFIGURATION_TX_REG1_CTL_TX_ENABLE_MASK;
                    write_reg32 (cmac_registers, CONFIGURATION_TX_REG1_OFFSET, configuration_tx_reg1);
                    display_cmac_registers (cmac_registers);
                    keep_open = true;
                }
            }
        }
    }

    if (keep_open)
    {
        /* If changed the settings, pause in case the VFIO close triggers a reset */
        printf ("Settings changed. Press return to close the VFIO devices.\n");
        getchar ();
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
