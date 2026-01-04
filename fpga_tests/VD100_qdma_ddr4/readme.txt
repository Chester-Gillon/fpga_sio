This is a variation of the VDS100_dma_ddr4 project, which uses the QDMA Subsystem for PCI Express,
rather than DMA/Bridge Subsystem for PCI Express.

It is a also a test of a multi-function device by having 4 functions. The functions are identical and
each only exposes the QDMA bar. A memory mapped DMA interface is connected to 4 GB of DDR4.

The DDR4 is configured in the same way as the VD100_dma_ddr4 project.

For configuring the PCIe interface:
1. In qdma_0 set:
   - Lane width: x4
   - Maximum link speed: 5.0 GT/s (initial maximum allowed)
2. Used the following TCL to allow all speeds:
   set_property -dict [list CONFIG.all_speeds_all_sides {YES}] [get_ips VD100_qdma_ddr4_qdma_0_0]
3. In qdma_0 increased Maximum link speed to 16.0 GT/s
3. Ran Block Automation selecting:
   - Base IP: PL-PCIE
   - PCIe Maximum Link Speed: 16.0 GT/s
   - PCIe Maximum Link Width: x4
   - AXI Interface Strategy: Maximum Data Width
4. TCL console output of the block automation was:
   apply_bd_automation -rule xilinx.com:bd_rule:qdma -config { axi_strategy {max_data} link_speed {4} link_width {4} pl_pcie_cpm {PL-PCIE}}  [get_bd_cells qdma_0]
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_sriov_bar5_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_sriov_bar5_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_sriov_bar5_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_sriov_bar5_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_sriov_bar4_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_sriov_bar4_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_sriov_bar4_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_sriov_bar4_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_sriov_bar3_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_sriov_bar3_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_sriov_bar3_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_sriov_bar3_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_sriov_bar1_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_sriov_bar1_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_sriov_bar1_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_sriov_bar1_size' from '2' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_bar2_size' from '128' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_bar2_size' from '128' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_bar2_size' from '128' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_bar2_size' from '128' to '4' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_bar1_scale' from 'Kilobytes' to 'Megabytes' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_bar1_scale' from 'Kilobytes' to 'Megabytes' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_bar1_scale' from 'Kilobytes' to 'Megabytes' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_bar1_scale' from 'Kilobytes' to 'Megabytes' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'PF0_MSIX_CAP_TABLE_OFFSET' from '00000000' to '30000' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'PF0_MSIX_CAP_PBA_OFFSET' from '00000000' to '34000' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'PF3_MSIX_CAP_TABLE_OFFSET' from '00000000' to '30000' has been ignored for IP 'qdma_0_support/pcie'
     WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'PF3_MSIX_CAP_PBA_OFFSET' from '00000000' to '34000' has been ignored for IP 'qdma_0_support/pcie'
     INFO: [Common 17-14] Message 'IP_Flow 19-3374' appears 100 times and further instances of the messages will be disabled. Use the Tcl command set_msg_config to change the current settings.
     WARNING: [BD 41-1306] The connection to interface pin </qdma_0_support/gtwiz_versal_0/QUAD0_ch0_phyready> is being overridden by the user with net <gtwiz_versal_0_QUAD0_ch0_phyready>. This pin will not be connected as a part of interface connection <Quad0_CH0_DEBUG>, and might not recreate the connectivity as expected using the writ_bd_tcl command.
     WARNING: [BD 41-1306] The connection to interface pin </qdma_0_support/gtwiz_versal_0/QUAD0_ch1_phyready> is being overridden by the user with net <gtwiz_versal_0_QUAD0_ch1_phyready>. This pin will not be connected as a part of interface connection <Quad0_CH1_DEBUG>, and might not recreate the connectivity as expected using the writ_bd_tcl command.
     WARNING: [BD 41-1306] The connection to interface pin </qdma_0_support/gtwiz_versal_0/QUAD0_ch2_phyready> is being overridden by the user with net <gtwiz_versal_0_QUAD0_ch2_phyready>. This pin will not be connected as a part of interface connection <Quad0_CH2_DEBUG>, and might not recreate the connectivity as expected using the writ_bd_tcl command.
     WARNING: [BD 41-1306] The connection to interface pin </qdma_0_support/gtwiz_versal_0/QUAD0_ch3_phyready> is being overridden by the user with net <gtwiz_versal_0_QUAD0_ch3_phyready>. This pin will not be connected as a part of interface connection <Quad0_CH3_DEBUG>, and might not recreate the connectivity as expected using the writ_bd_tcl command.
     apply_bd_automation: Time (s): cpu = 00:00:10 ; elapsed = 00:00:09 . Memory (MB): peak = 13267.004 ; gain = 260.305 ; free physical = 4470 ; free virtual = 84316
     apply_bd_automation: Time (s): cpu = 00:00:19 ; elapsed = 00:00:18 . Memory (MB): peak = 13410.566 ; gain = 403.867 ; free physical = 4316 ; free virtual = 84162
     apply_bd_automation: Time (s): cpu = 00:00:22 ; elapsed = 00:00:21 . Memory (MB): peak = 13410.566 ; gain = 403.867 ; free physical = 4322 ; free virtual = 84169
     ERROR: [xilinx.com:ip:axi_noc:1.1-101]  Automation rule cannot be applied since multiple CIPS IP instances found within the design. These are : /qdma_0_support/cips_ip /versal_cips_0
     ERROR: [Common 17-39] 'send_msg_id' failed due to earlier errors.
   The errors are due the the block automation adding a cips_ip instance in the qdma_0_support block diagram,
   when the parent diagram had versal_cips_0 manually added before the block automation was run.
   To resolve this just delete the unwanted cips_ip instance from the qdma_0_support block diagram.

   Looking at the IP in the qdma_0_support block diagram:
   a. qdma_0_support/pcie has:
      - Maximum Link Speed: 16.0 GT/s
      - Maximum Link Width: x4
   b. qdma_0_support/pcie_phy has:
      - Lane Width: x4
      - Maximum Link Speed: 16.0 GT/s
      - Input Reference Clock Frequency: 100 MHz
      - Output User Clock Frequency: 250 MHz
      - Output Core Clock Frequency: 500 MHz

   Therefore, the bd_rule:qdma allowed the block automation to use configure a 16.0 GT/s maximum link speed,
   unlike the bd_rule:xdma (as per VD100_enum/readme.txt)

   On looking at the Vivado installation:
   - /opt/Xilinx/2025.2/Vivado/data/rsb/design_assist/block/qdma/bd.tcl has code to handle the all_speeds_all_sides parameter
   - Whereas /opt/Xilinx/2025.2/data/rsb/design_assist/block/xdma/bd.tcl doesn't.
5. After running the block automation the qdma interface pcie_cfg_external_msix_without_msi_if has been
   connected to an external interface. Can't find a description of the interface in either:
   a. PG302 QDMA Subsystem for PCI Express v5.1
   b. PG344 Versal Adaptive SoC DMA and Bridge Subsystem for PCI Express v2.0
   c. PG346 Versal Adaptive SoC CPM Mode for PCI Express v3.4
   d. PG347 Versal Adaptive SoC CPM DMA and Bridge Mode for PCI Express v3.4

   Searching /opt/Xilinx/2025.2/Vivado/data the following two files have pcie_cfg_external_msix_without_msi_if:
   - /opt/Xilinx/2025.2/Vivado/data/ip/xilinx/qdma_v5_1/component.xml
   - /opt/Xilinx/2025.2/Vivado/data/rsb/design_assist/block/qdma/bd.tcl

   From looking at the above files it isn't clear what the interface is used for.
   In the QDMA configuration should have disable MSI-X

