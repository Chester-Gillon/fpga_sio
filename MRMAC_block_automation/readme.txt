The sub projects were created to investigate how the MRMAC Block Automation generates the transceiver configuration.

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

single_100G
  Selected a single 100G port with FEC as "100G (IEEE 8.203) - RS(528 514)"


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

