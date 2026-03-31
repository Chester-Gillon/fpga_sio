1. The sub projects were initially created to investigate how the MRMAC Block Automation generates the transceiver configuration
================================================================================================================================

For the different projects the steps performed were:
a. Create project in Vivado 2025.2
b. Create a block diagram.
c. Add the MRMAC IP and configure the ports as required.
   Need to select "Use Legacy GT Wizard in Example Design" for the reasons in
   https://gist.github.com/Chester-Gillon/d87759ab5fb5dd1cb5c12ac3c7a8f292#31-block-automation-only-supported-with-legacy-gt-wizard
d. Run Block Automation, using the defaults.
e. Validate the design.
f. Save the project.
g. Close and re-open project. Without opening the block design run the following:
     write_project_tcl -force create_project.tcl

   The above just avoids the create_project.tcl containing commands to set the block diagram visual layout.


For the different project configurations:
dual_10G:
  Static configuration with two 10G ports and no FEC, the same as used in fpga_tests/VD100_10G_ether_dual.

dual_10G_fec:
  Static configuration with four 10G ports. Selected:
  - MRMAC Configuration Preset: Start from Scratch
  - MRMAC Mode: MAC+PCS+FEC
  - MRMAC Data Rate: Mixed/Custom
  - Ports 0 and 1 Data Rate: 10GE
  - Ports 2 and 3 Data Rate: N/U
  - AXI Datapath Interface: Independent 32b Non-Segmented
  - FEC Slice 0 and 1 Mode: 10G (IEEE 802.3 CL74) - Fire(2112 2080)
  - FEC Slice 2 and 3 Mode: FEC Disabled (Bypass) 

quad_10G_fec:
  Static configuration with four 10G ports. Selected:
  - MRMAC Configuration Preset: 4x10GE Wide
  - MRMAC Mode: MAC+PCS+FEC
  - AXI Datapath Interface: Independent 32b Non-Segmented
  - FEC Mode: 10G (IEEE 802.3 CL74) - Fire(2112 2080)

dual_25G:
  Static configuration with two 25G ports.
  FEC is configured as "25G (IEEE 802.3 CL108)- RS(528 514)" since:
  a. https://arista.my.site.com/AristaCommunity/s/article/monitoring-link-quality-using-forward-error-correction-fec-data-on-arista-switches
     indicates RS(514,528) is a "stronger" FEC than "Firecode (FC), also known as BASE-R or Clause 74 FEC".
     Albeit at the expense of more latency.
  b. Not sure how "25G (IEEE 802.3 CL108)- RS(528 514)" differs from "25/50GE Consortium RS(528,514)".
     Perhaps could compare https://ethernettechnologyconsortium.org/wp-content/uploads/2020/03/25G-50G-Specification-FINAL.pdf
     against the IEEE RS-FEC specification.

     Is "25/50GE Consortium RS(528,514)" the same as "RS-FEC (Clause 91)"?

  The AXI Datapath Interface has been set to "Independent 128b Non-Segmented" since:
  a. As found with VD100_10G_ether_dual the speed grade of the xcve2302-sfva784-1LP-e-S part used by the VD100 doesn't allow
     block RAM or ultra RAM (for FIFOs) to operate at 644.531 MHz which is the clock used for the "Low Latency 64b Non-Segmented" option.
  b. While PG314 Table 4: MRMAC AXI4-Stream Modes suggests that 25G allows use of "Independent 64b Non-Segmented" with a minimum AXI clock
     frequency of 390.625 MHz the "Independent 64b Non-Segmented" option isn't listed in the MRMAC IP configuration GUI.

  /opt/Xilinx/2025.2/Vivado/data/ip/xilinx/mrmac_v3_2/component.xml has "Low Latency 64b  Non-Segmented" as an enumeration, but from that
  file it isn't clear under what conditions that is a valid enumeration. Some of the TCL files under that IP directory are encrypted so
  can't see all of the code which could be determining the available options.

dual_25G_dynamic
  Same as dual_25G, except select Dynamic instead of Static configuration in the MRMAC.

quad_25G
  Static configuration with four 25G ports. Selected:
  - MRMAC Configuration Preset: 4x25GE Wide
  - MRMAC Mode: MAC+PCS+FEC
  - FEC is configured as "25G (IEEE 802.3 CL108)- RS(528 514)"
  - AXI Datapath Interface: Independent 128b Non-Segmented"

single_100G
  Selected:
  - Part xcvm1102-sfva784-2LP-e-L. 
    This is the only Versal Prime device with a 100G capable MRMAC supported by Vivado webpack, in order to allow the full
    output products to be generated which needs a Synthesis license.
  - A single 100G port
  - MRMAC Mode: "MAC+PCS+FEC"
  - FEC: "100G (IEEE 8.203) - RS(528 514)"
  - GT RefClk: 156.25 MHz
  - AXI Datapath Interface: "Low Latency 256b Non-Segmented"

Comparing the resulting create_project.tcl files for combinations of different projects:
1. dual_10G .vs. dual_25G:
   a. xilinx.com:ip:mrmac:3.2 properties are different, which is expected since are changing the MRMAC configuration.
   b. The gt_quad_base protocol properties are different. E.g.
      - TX_LINE_RATE 10.3125 .vs. 25.78125
      - TX_INT_DATA_WIDTH 32 .vs. 80
      - RXPROGDIV_FREQ_SOURCE RPLL (ring-based channel PLL) .vs. LCPLL (LC-Tank PLL)
   c. The CONFIG.REFCLK_STRING is HSCLK0_RPLLGTREFCLK0 .vs. HSCLK0_LCPLLGTREFCLK0
      Which matches the change in PLL type in the protocol properties.
   d. dual_25G has additional ports connected:
      - rx_axis_tdata1
      - rx_axis_tdata3
      - rx_axis_tkeep_user1
      - rx_axis_tkeep_user3
      - tx_axis_tdata1
      - tx_axis_tdata3
      - tx_axis_tkeep_user1
      - tx_axis_tkeep_user3

      Which is explained by dual_25G using a 128 bit AXI data path for each port, and the tdata MRMAC ports being 64-bits.

2. dual_25G .vs. dual_25G_dynamic
   No change to the GT quad configuration.
   Only change is that all 8 AXI data path ports are connected.

   I.e. enabling Dynamic Configuration hasn't caused block automation to enable multiple rate support in the GT quad configuration.


2. Using the project to investigate enabling FEC
================================================

When the fpga_tests/VD100_10G_ether_dual project was used, which had the same MRMAC configuration as dual_25G, it was found the
MRMAC_CTL_FEC_MODE_MASK field was read back as 'b0000 "FEC Disabled" rather than the expected 'b0011 "IEEE 802.3 CL108 RS(528,514) FEC"
based upon MRAC configuration.

For the sub-projects generated the output products and looked at the generated MRMAC_block_automation_mrmac_0_0_wrapper.v files
for FEC related settings:
$ grep parameter ./dual_10G/MRMAC_block_automation/MRMAC_block_automation.gen/sources_1/bd/MRMAC_block_automation/ip/MRMAC_block_automation_mrmac_0_0/mrmac_v3_2_0/MRMAC_block_automation_mrmac_0_0_wrapper.v|grep FEC
     parameter integer NUM_100G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_WITH_FEC_PORTS = 0,        
     parameter integer NUM_50G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_40G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_40G_MAC_PCS_WITH_FEC_PORTS = 0,         
     parameter integer NUM_25G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_10G_MAC_PCS_NOFEC_PORTS = 2,
     parameter integer NUM_10G_MAC_PCS_WITH_FEC_PORTS = 0, 
  parameter [3:0] CTL_FEC_MODE_0 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_1 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_2 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_3 = 4'h0,
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_3 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_01 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_23 = "FALSE",
  parameter CTL_RX_FEC_ERRIND_MODE = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_3 = "FALSE",
  parameter CTL_TX_FEC_FOUR_LANE_PMD = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_0 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_1 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_2 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_3 = "FALSE",

$ grep parameter ./dual_10G_fec/MRMAC_block_automation/MRMAC_block_automation.gen/sources_1/bd/MRMAC_block_automation/ip/MRMAC_block_automation_mrmac_0_0/mrmac_v3_2_0/MRMAC_block_automation_mrmac_0_0_wrapper.v|grep FEC
     parameter integer NUM_100G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_WITH_FEC_PORTS = 0,        
     parameter integer NUM_50G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_40G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_40G_MAC_PCS_WITH_FEC_PORTS = 0,         
     parameter integer NUM_25G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_10G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_10G_MAC_PCS_WITH_FEC_PORTS = 2,    
  parameter [3:0] CTL_FEC_MODE_0 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_1 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_2 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_3 = 4'h0,
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_3 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_01 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_23 = "FALSE",
  parameter CTL_RX_FEC_ERRIND_MODE = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_3 = "FALSE",
  parameter CTL_TX_FEC_FOUR_LANE_PMD = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_0 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_1 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_2 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_3 = "FALSE",

$ grep parameter ./quad_10G_fec/MRMAC_block_automation/MRMAC_block_automation.gen/sources_1/bd/MRMAC_block_automation/ip/MRMAC_block_automation_mrmac_0_0/mrmac_v3_2_0/MRMAC_block_automation_mrmac_0_0_wrapper.v|grep FEC
     parameter integer NUM_100G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_WITH_FEC_PORTS = 0,        
     parameter integer NUM_50G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_40G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_40G_MAC_PCS_WITH_FEC_PORTS = 0,         
     parameter integer NUM_25G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_10G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_10G_MAC_PCS_WITH_FEC_PORTS = 4,
  parameter [3:0] CTL_FEC_MODE_0 = 4'b1100,
  parameter [3:0] CTL_FEC_MODE_1 = 4'b1100,
  parameter [3:0] CTL_FEC_MODE_2 = 4'b1100,
  parameter [3:0] CTL_FEC_MODE_3 = 4'b1100,
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_3 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_01 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_23 = "FALSE",
  parameter CTL_RX_FEC_ERRIND_MODE = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_3 = "FALSE",
  parameter CTL_TX_FEC_FOUR_LANE_PMD = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_0 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_1 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_2 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_3 = "FALSE",

$ grep parameter ./dual_25G/MRMAC_block_automation/MRMAC_block_automation.gen/sources_1/bd/MRMAC_block_automation/ip/MRMAC_block_automation_mrmac_0_0/mrmac_v3_2_0/MRMAC_block_automation_mrmac_0_0_wrapper.v|grep FEC
     parameter integer NUM_100G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_WITH_FEC_PORTS = 0,        
     parameter integer NUM_50G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_40G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_40G_MAC_PCS_WITH_FEC_PORTS = 0,         
     parameter integer NUM_25G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_WITH_FEC_PORTS = 2,     
     parameter integer NUM_10G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_10G_MAC_PCS_WITH_FEC_PORTS = 0,        
  parameter [3:0] CTL_FEC_MODE_0 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_1 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_2 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_3 = 4'h0,
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_3 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_01 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_23 = "FALSE",
  parameter CTL_RX_FEC_ERRIND_MODE = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "TRUE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_1 = "TRUE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_3 = "FALSE",
  parameter CTL_TX_FEC_FOUR_LANE_PMD = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_0 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_1 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_2 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_3 = "FALSE",

$ grep parameter ./quad_25G/MRMAC_block_automation/MRMAC_block_automation.gen/sources_1/bd/MRMAC_block_automation/ip/MRMAC_block_automation_mrmac_0_0/mrmac_v3_2_0/MRMAC_block_automation_mrmac_0_0_wrapper.v|grep FEC
     parameter integer NUM_100G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_WITH_FEC_PORTS = 0,        
     parameter integer NUM_50G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_40G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_40G_MAC_PCS_WITH_FEC_PORTS = 0,         
     parameter integer NUM_25G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_WITH_FEC_PORTS = 4, 
     parameter integer NUM_10G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_10G_MAC_PCS_WITH_FEC_PORTS = 0,        
  parameter [3:0] CTL_FEC_MODE_0 = 4'b0011,
  parameter [3:0] CTL_FEC_MODE_1 = 4'b0011,
  parameter [3:0] CTL_FEC_MODE_2 = 4'b0011,
  parameter [3:0] CTL_FEC_MODE_3 = 4'b0011,
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_3 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_01 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_23 = "FALSE",
  parameter CTL_RX_FEC_ERRIND_MODE = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "TRUE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_1 = "TRUE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_2 = "TRUE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_3 = "TRUE",
  parameter CTL_TX_FEC_FOUR_LANE_PMD = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_0 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_1 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_2 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_3 = "FALSE",

$ grep parameter ./single_100G/MRMAC_block_automation/MRMAC_block_automation.gen/sources_1/bd/MRMAC_block_automation/ip/MRMAC_block_automation_mrmac_0_0/mrmac_v3_2_0/MRMAC_block_automation_mrmac_0_0_wrapper.v|grep FEC
     parameter integer NUM_100G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_100G_MAC_PCS_WITH_FEC_PORTS = 1,  
     parameter integer NUM_50G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_50G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_40G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_40G_MAC_PCS_WITH_FEC_PORTS = 0,         
     parameter integer NUM_25G_FEC_ONLY_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_25G_MAC_PCS_WITH_FEC_PORTS = 0,          
     parameter integer NUM_10G_MAC_PCS_NOFEC_PORTS = 0,
     parameter integer NUM_10G_MAC_PCS_WITH_FEC_PORTS = 0,        
  parameter [3:0] CTL_FEC_MODE_0 = 4'b1000,
  parameter [3:0] CTL_FEC_MODE_1 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_2 = 4'h0,
  parameter [3:0] CTL_FEC_MODE_3 = 4'h0,
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_ALIGNMENT_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_CORRECTION_3 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_0 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_1 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_2 = "FALSE",
  parameter CTL_RX_FEC_BYPASS_INDICATION_3 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_01 = "FALSE",
  parameter CTL_RX_FEC_CDC_BYPASS_23 = "FALSE",
  parameter CTL_RX_FEC_ERRIND_MODE = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_1 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_2 = "FALSE",
  parameter CTL_RX_FEC_TRANSCODE_CLAUSE49_3 = "FALSE",
  parameter CTL_TX_FEC_FOUR_LANE_PMD = "TRUE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_0 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_1 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_2 = "FALSE",
  parameter CTL_TX_FEC_TRANSCODE_BYPASS_3 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_0 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_1 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_2 = "FALSE",
  parameter CTL_TX_PTP_RSFEC_COMP_EN_3 = "FALSE",

Comparing versions with FEC:
a. dual_10G_fec has CTL_FEC_MODE_0..CTL_FEC_MODE_3 as 4'h0 which is "FEC disabled"
   It was expected that FEC was enabled on ports 0 and 1.
   NUM_10G_MAC_PCS_WITH_FEC_PORTS = 2, so some part of the configuration recognised that FEC was wanted.
b. quad_10G_fec has CTL_FEC_MODE_0..CTL_FEC_MODE_3 as 4'b1100 which is "IEEE 802.3 CL74 FEC".
   The is the expected FEC mode.

   Other FEC related settings are:
   - CTL_RX_FEC_TRANSCODE_CLAUSE49_0..CTL_RX_FEC_TRANSCODE_CLAUSE49_3 as FALSE, which is expected as per PG314
   - CTL_TX_FEC_FOUR_LANE_PMD = "FALSE", which is expected as per PG314

a. dual_25G has CTL_FEC_MODE_0..CTL_FEC_MODE_3 as 4'h0 which is "FEC disabled"
   It was expected that FEC was enabled on ports 0 and 1.
   NUM_25G_MAC_PCS_WITH_FEC_PORTS = 2, so some part of the configuration recognised that FEC was wanted.

   CTL_RX_FEC_TRANSCODE_CLAUSE49_0..CTL_RX_FEC_TRANSCODE_CLAUSE49_0 as TRUE, which is expected as per PG314
b. quad_25G has CTL_FEC_MODE_0..CTL_FEC_MODE_3 as 4'b0011 which is "IEEE 802.3 CL108 RS(528,514) FEC"
   The is the expected FEC mode.

   Other FEC related settings are:
   - CTL_RX_FEC_TRANSCODE_CLAUSE49_0..CTL_RX_FEC_TRANSCODE_CLAUSE49_3 as TRUE, which is expected as per PG314
   - CTL_TX_FEC_FOUR_LANE_PMD = "FALSE", which is expected as per PG314

The problem is that when only some ports are used as 10G or 25G, when try and enable FEC in the IP configuration the generated
output product MRMAC_block_automation_mrmac_0_0_wrapper.v doesn't have the CTL_FEC_MODE_x parameters with FEC enabled.
This means when the bitstream is loaded FEC is left disabled.

In dual_25G tried changing the MRMAC Data Rate to "4x25GE". I.e. enable all 4 ports. Since the existing Block Automation only connected
2 of the GT Quads with this attempt the block diagram validation failed with errors:
validate_bd_design
ERROR: [BD 41-1957] BUS Interface /mrmac_0/gt_tx_serdes_interface_2 if IP block /mrmac_0 is unconnected, this must be connected to Quad IP Block.
ERROR: [BD 41-1957] BUS Interface /mrmac_0/gt_tx_serdes_interface_3 if IP block /mrmac_0 is unconnected, this must be connected to Quad IP Block.
ERROR: [BD 41-1957] BUS Interface /mrmac_0/gt_rx_serdes_interface_2 if IP block /mrmac_0 is unconnected, this must be connected to Quad IP Block.
ERROR: [BD 41-1957] BUS Interface /mrmac_0/gt_rx_serdes_interface_3 if IP block /mrmac_0 is unconnected, this must be connected to Quad IP Block.

Therefore, enabling all ports in the MRMAC without rerunning the block automation and corresponding configuration changes is a
valid workaround.


Looking at the single_100G generated product:
- CTL_FEC_MODE_0 = 4'b1000, which is "IEEE 802.3 RS(528,514) FEC", which matches the configuration in the GUI.
- CTL_RX_FEC_TRANSCODE_CLAUSE49_0 = "FALSE", which is expected as per PG314
- CTL_TX_FEC_FOUR_LANE_PMD = "TRUE", which is expected as per PG314

