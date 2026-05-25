The VD100_40G_ether design is design to provide one 40 GbE port using the 
Versal Devices Integrated 100G Multirate Ethernet MAC Subsystem (MRMAC).

XDMA with a C2H / H2C AXI stream is used to interface to the data ports of the MRMAC to transmit / receive Ethernet packets.

The VD100 board only exposes two of the lanes to SFP+ cages, so are unable to test actual communication with other devices.
By enabling loopback in the GTY transceivers will be able to test software for multi-lane MRMAC operation.

It has been created based upon VD100_10G_ether_dual with:
a. The MRMAC ports configured as 40G rather than 10G.
b. Corresponding changes to how the MRMAC AXI data ports are connected.

The AXI slave memory map, which is the same as VD100_10G_ether_dual:
a. 0x0 .. 0xffff is the MRMAC Configuration registers, Status registers, and Statistics counters.
b. 0x10000 .. 0x10fff dual GPIO to control the output pins for the SFP+ control signals
   Since the AXI GPIO doesn't allow output bits to be read back, configured the AXI GPIO as:
   - Dual channel
   - 1st channel is all inputs
   - 2nd channel is all outputs
   - Output channel bits are connected to input channel bits.

   Bits are:
   Bit 0 : SFP1_TX_EN
   Bit 1 : SFP2_TX_EN

   Write 1 to enable the transmitter and 0 to disable.
   The VD100 baseboard has a transistor which inverts the signal, which makes these GPIOs enables rather than disables.
c. 0x11000 ... 0x11fff
   I2C controller for SFP1.
   For some reason, the I2C signals on the VD100 are only connected to SFP1.

The Implementation Strategy has been set to "Performance_AggressiveExplore" in order to allow the timing to be met on the
PCIe gen4 interface.


For the MRMAC configuration and connectivity:
1. Create the mrmac_40G.bd block diagram, to hide the GT blocks and connections on the top-level diagram.

2. The MRMAC block configuration:
   a. For Configuration Type use "Static Configuration" since only intended to support one 40G port.
   b. Enable "Use Legacy GT Wizard in Example Design", since otherwise are unable to run block automation.
   c. Select MRMAC Configuration Preset "1x40GE XLAUI-4 Wide"
   d. Select MRMAC Mode "MAC+PCS+FEC" since will test using FEC.
   e. Select MRMAC Site "MRMAC X0Y0" (the only option on the the part)
   f. Select MRMAC Data Rate as "1x40GE".
   g. For the MAC port configuration:
      - Leave port0 Data Rate as 40GE (the only option)
      - Leave port1 and port3 Data Rate a N/A (the only option)
      - Leave port2 Data Rate as 10GE (the only option - not sure the GUI isn't using N/A)
      - Disable "TX Flow Control" as not needed, and would require additional user logic.
      - Disable "RX Flow Control" as not needed, and would require additional user logic.
      - Disable "Preemption" as not needed, and would require additional user logic.
      - Set port0 AXI Datapath Interface to "Independent 256b Not-Segmented"
        Clock domain crossing is required since:
        - The XMDA AXI streams are 256 bits at 250 MHz, which is fixed based upon the PCIe 16 GT/s x4 interface.
        - For 40GE the MRMAC data path options are either a 128 or 256 bit width.
        - For 40GE the MRMAC data path clocking options are either:
          - Low Latency using the tx_core_clk or rx_serdes_clk which are 644.531 MHz.
          - Independent with a AXI clock between 322.265 MHz and 644.531 MHz.
        - The device is xcve2302-sfva784 speed grade -1LP, for which :
          - Block RAM Fmax of 615 MHz for speed grade -1L
          - Ultra RAM Fmax of 500 MHz for speed grade -1L
       
          Which means with the speed grade neither the Block RAM nor Ultra RAM support running at the 644.531 MRMAC Tx core frequency.
          Therefore, need to select the Independent AXI Datapath Interface.

          Width conversion isn't necessary.
   h. Select GT Type as "GTYP" (only option).
   i. Select GT RefClk as "156.25" MHz, which is the fixed reference clock frequency on the VD100.
   j. Select Number of Pipeline Stages between MRMAC and GT core as "0".
      May need to increase to get timing closure.
   k. Set the FEC Operating Mode for slices 0 to "40G (IEEE 802.3 802.3 CL74) - Fire(2112 2080)".
      That is the only FEC mode supported.
      FEC slices 1 to 3 can only be N/A.

3. After configuring the MRMAC ran block automation, and added the mrmac_40G block to the top level VD100_40G_ether.
   The result of checking the connections made by block automation, excluding the data paths:
   a. Connect the s_axi_aclk, s_axi and s_axi_aresetn ports to the top level diagram.
      Map 64K to the XDMA M_AXI_LITE to access the MRMAC Configuration registers, Status registers, and Statistics counters.
   b. Delete the gtpowergood_in and gtpowergood ports.
      Connect the gtpowergood output from the gt_guad_base to MRMAC gtpowergood_in input.
   c. Delete the ch0_loopback and ch1_loopback ports.
      When the ports were deleted the ch0_loopback ... ch3_loopback inputs on the gt_quad_base disappeared.
      Was going to tie the ch0_loopback ... ch3_loopback on the gt_quad_base to 3'b000, which 
      AM002 Versal Adaptive SoC GTY and GTYP Transceivers Architecture Manual says is "Normal operation".
      No need to do that with the ch0_loopback and ch1_loopback inputs no longer on the gt_quad_base.
   d. Delete the ch0_txrate ... ch3_txrate, ch0_rxrate ... ch3_rxrate ports.
      When the ports were deleted the ch0_txrate ... ch3_txrate, ch0_rxrate ... ch3_rxrate inputs on the gt_quad_base disappeared. 
      AM002 says the CH*_RXRATE and CH*_TXRATE ports can be used to switch between a pre-configured set of line rates.
      The design uses a single fixed line rate, so no rate switching is required.
   e. Delete the APB3_INTF port, as are not going to access the GT registers.
   f. Connect the apb3clk_quad port to the pl0_ref_clk CIPs clock, which is also used for the xmda_0_support gtwiz_freerun_clk.
      From the PG331 Versal Adaptive SoC Transceivers Wizard not sure if apb3clk needs to be a free running clock to support initialisation.
   g. Delete the following status ports, since registers will be used to obtain status:
      - stat_tx_port0
      - stat_tx_port1
      - stat_tx_port2
      - stat_tx_port3
      - stat_rx_port0
      - stat_rx_port1
      - stat_rx_port2
      - stat_rx_port3
   h. Delete the pm_rdy port, since just sample the statistics counter registers.
   i. Delete the pm_tick port, since no user logic to request sampling statistics registers.
      Connect to constant zero.
   j. Delete the tx_preamblein port, since will not be using a custom preamble.
      CONFIGURATION_TX_REGx_0 registers show custom preamble is disabled by default.
   k. Delete the rx_preambleout port, since are not using custom preamble nor preemption.
   l. PG314 says for the s_axi_aclk signal:
        Note: The s_axi_aclk must be present for the MRMAC to function. If this clock is interrupted, the MRMAC enters an error state.

      Looking at a synthesised design for a two port design shows in the MRMAC:
      - An instance of the "Reset Controller Helper Block" for each of the the two GT ports.
      - The gtwiz_reset_clk_freerun_in connected to s_axi_aclk.
      - Therefore, s_axi_aclk needs to be running to perform a GT reset.

      Initially planned to assert the MRMAC reset ports just to provide a reset at device load, and then use register writes
      to force a reset later if required.

      To try and avoid an issue with the MRMAC entering an error state at device load, use axi_aresetn rather than the initial idea
      of phy_rdy_out from the xdma_0_support. axi_aresetn will assert when the VFIO driver is opened or closed,
      but should help to recover if the MRMAC does enter an error state.

      Configuration is:
      - Add a mrmac_resetn active low reset input port.
      - Delete the following input ports. Connect the corresponding MRMAC reset input to an inverted version of mrmac_resetn:
        - gt_reset_all_in
        - rx_core_reset
        - rx_flexif_reset
        - rx_serdes_reset
        - tx_core_reset
        - tx_serdes_reset
        - gt_reset_tx_datapath_in
        - gt_reset_rx_datapath_in
      - On the top level diagram connect mrmac_resetn to axi_aresetn from the xdma_0.
   m. Delete gt_tx_reset_done_out and gt_rx_reset_done_out ports as no need to monitor.
   n. For the the GTY clocks the frequencies (MHz) on the ports:
      - ch0_tx_usr_clk         : 644.531
      - ch0_tx_usr_clk2        : 322.2655
      - ch0_rx_usr_clk         : 644.531
      - ch1_rx_usr_clk         : 644.531
      - ch2_rx_usr_clk         : 644.531
      - ch3_rx_usr_clk         : 644.531
      - ch0_rx_usr_clk2        : 322.2655
      - ch1_rx_usr_clk2        : 322.2655
      - ch2_rx_usr_clk2        : 322.2655
      - ch3_rx_usr_clk2        : 322.2655
      - ch0_txusrclk           : 322.266
      - ch1_txusrclk           : 322.266
      - ch2_txusrclk           : 322.266
      - ch3_txusrclk           : 322.266
      - ch0_rxusrclk           : 322.266
      - ch1_rxusrclk           : 322.266
      - ch2_rxusrclk           : 322.266
      - ch3_rxusrclk           : 322.266
      - rx_alt_serdes_clk[3:0] : 322.266
      - rx_core_clk[3:0]       : 644.531
      - rx_serdes_clk[3:0]     : 644.531
      - tx_alt_serdes_clk[3:0] : 322.266
      - tx_core_clk[3:0]       : 644.531

      PG314 Table 32: Transceiver Signaling shows for the MRMAC "40GE Wide SerDes" configuration the transmit clocks used are:
      - tx_core_clk[0]
      - tx_alt_serdes_clk[0]
      - tx_alt_serdes_clk[1]
      - tx_alt_serdes_clk[2]
      - tx_alt_serdes_clk[3]

      Which explains why ch1_tx_usr_clk ... ch3_tx_usr_clk were not connected by the block automation.

      However, still need ch1_tx_usr_clk2 ... ch3_tx_usr_clk2 to provide a 322.266 MHz clock for tx_alt_serdes_clk[3:1]

      Add a "MBUF GT" for ch1_txoutclk ... ch3_txoutclk on the gt_guad_base, connecting the following
      inputs on each as per the mbufg_gt_0 which added by the block automation:
      - MBUFG_GT_I
      - MBUFG_GT_CE
      - MBUFG_GT_CLR
      - MBUFG_GT_CLRB_LEAF

      Remove the ports for the above. Make the clock connections:
      - ch0_tx_usr_clk (mbufg_gt_0 MBUFG_GT_O1)    -> tx_core_clk[0]
      - ch0_tx_usr_clk2 (mbufg_gt_0 MBUFG_GT_O2)   -> tx_alt_serdes_clk[0]
                                                   -> ch0_txusrclk
      - ch1_tx_usr_clk2 (mbufg_gt_0_1 MBUFG_GT_O2) -> tx_alt_serdes_clk[1]
                                                   -> ch1_txusrclk
      - ch2_tx_usr_clk2 (mbufg_gt_0_2 MBUFG_GT_O2) -> tx_alt_serdes_clk[2]
                                                   -> ch2_txusrclk
      - ch2_tx_usr_clk3 (mbufg_gt_0_3 MBUFG_GT_O2) -> tx_alt_serdes_clk[3]
                                                   -> ch3_txusrclk
      - ch0_rx_usr_clk (mbufg_gt_1 MBUFG_GT_O1)    -> rx_core_clk[0]
                                                   -> rx_serdes_clk[0]
      - ch1_rx_usr_clk (mbufg_gt_1_1 MBUFG_GT_O1)  -> rx_core_clk[1]
                                                   -> rx_serdes_clk[1]
      - ch2_rx_usr_clk (mbufg_gt_1_2 MBUFG_GT_O1)  -> rx_core_clk[2]
                                                   -> rx_serdes_clk[2]
      - ch3_rx_usr_clk (mbufg_gt_1_3 MBUFG_GT_O1)  -> rx_core_clk[3]
                                                   -> rx_serdes_clk[3]
      - ch0_rx_usr_clk2 (mbufg_gt_1 MBUFG_GT_O2)   -> rx_alt_serdes_clk[0]
                                                   -> ch0_rxusrclk
      - ch1_rx_usr_clk2 (mbufg_gt_1_1 MBUFG_GT_O2) -> rx_alt_serdes_clk[1]
                                                   -> ch1_rxusrclk
      - ch2_rx_usr_clk2 (mbufg_gt_1_2 MBUFG_GT_O2) -> rx_alt_serdes_clk[2]
                                                   -> ch2_rxusrclk
      - ch2_rx_usr_clk3 (mbufg_gt_1_3 MBUFG_GT_O2) -> rx_alt_serdes_clk[3]
                                                   -> ch3_rxusrclk
   o. For the following clocks on features not used, remove the ports and connect to s_axi_aclk:
      - rx_flexif_clk
      - tx_flexif_clk
      - rx_ts_clk
      - tx_ts_clk
   p. Ports which need to be connected to external pins, and add pin constraints:
      - Rename CLK_IN_D to SFP_REF
      - Rename GT_Serial to SFP_serial
   q. For the following remove the ports and connect to constant zero inputs since not intending to control with user logic:
      - ctl_tx_port0
      - ctl_tx_port1
      - ctl_tx_port2
      - ctl_tx_port3

      Use zero constants:
      - For send_idle, registers can be used to send idles if required.
      - For send_lfi, registers can be used to send Local Fault Indication if required.
      - For send_rfi, registers can be used to send Remote Fault Indication if required.
      - Don't override VLM BIP7 byte, since for multi-lane synchronisation and don't see the need to override.
        The registers can only read if override has been applied, and not modify the override.
   r. Delete the following ports, leaving the signals unconnected on the MRMAC:
      - rx_axis_tdata4
      - rx_axis_tkeep_user4
      - tx_axis_tdata4
      - tx_axis_tkeep_user4

      This is because as per Table 18: 40G Non-Segmented Signaling for 256 Bits shows these signals are not used.
      For some reason the MRMAC configuration GUI configured the port2 Data Rate as 10GE, which is probably why
      the block automation connected the above ports.
   s. With the AXI datapath interface as Independent:
      - Remove the tx_axi_clk and rx_axi_clk ports
      - Connect the MRMAC tx_axi_clk to tx_alt_serdes_clk (322.2655 MHz)
      - Connect the MRMAC rx_axi_clk to rx_alt_serdes_clk (322.2655 MHz)
      - Create the output port mrmac_axis_tx_aclk connected to tx_alt_serdes_clk[0]
      - Create the output port mrmac_axis_rx_aclk connected to rx_alt_serdes_clk[0]

4. For implementing MRMAC Tx data paths (H2C streams) created mrmac_h2c_stream block diagram with:
   a. axis_data_fifo_0 to convert from 250 MHz 256 bits to 322.2655 MHz 256 bits.
      This has a clock domain crossing from the XDMA stream to the MRMAC tx_alt_serdes_clk frequency 
      (with MRMAC set to "Independent 256b Not-Segmented").
      Settings:
      - FIFO depth: 1024
      - Memory type: Auto
      - Independent clocks: Yes
      - CDC sync stages: 3 (the default)
      - Enable packet mode: No (later data FIFO uses packet mode)
      - ACLKEN conversion mode: None
      - TDATA with (bytes): 32
      - Enable TKEEP: Yes
      - Enable TLAST: Yes
   b. axis_data_fifo_1 to collect a complete Ethernet frame for output by the MRMAC without causing a transmit underrun:
      - FIFO depth: 1024
      - Memory type: Auto:
      - Independent clocks: No
      - Enable packet mode: Yes
      - ACLKEN conversion mode: None
      - TDATA width (bytes): 32
      - Enable TKEEP: Yes
   c. proc_sys_reset_0 to generate the reset for axis_data_fifo_1 which operates on a different clock domain to the xdma_axis_aresetn reset output.
   d. Inline slices and concat are required to connect between axis_data_fifo_1 and the MRMAC (split into four data paths):
      - FIFO tdata[63:0]    -> port tx_axis_tdata0[63:0]
      - FIFO tdata[127:64]  -> port tx_axis_tdata1[63:0]
      - FIFO tdata[191:128] -> port tx_axis_tdata2[63:0]
      - FIFO tdata[255:192] -> port tx_axis_tdata3[63:0]
      - FIFO tkeep[7:0]     -> tx_axis_tkeep_user0[7:0]
      - FIFO tkeep[15:8]    -> tx_axis_tkeep_user1[7:0]
      - FIFO tkeep[23:16]   -> tx_axis_tkeep_user2[7:0]
      - FIFO tkeep[31:24]   -> tx_axis_tkeep_user3[7:0]
      - Constant 0 to port tx_axis_tkeep_user0[10:8], which are resume,preempt,err signals.
        Preemption isn't used.
        Errors aren't signalled.
      - Constant 0 to port tx_axis_tkeep_user1[10:8], tx_axis_tkeep_user2[10:8], tx_axis_tkeep_user3[10:8], which are unused.

5. For implementing MRMAC Rx data paths (C2H streams) created mrmac_c2h_stream block diagram with:
   a. axis_data_fifo_0  to receive the Ethernet packets from the MRMAC:
      - FIFO depth: 32768 (this is the maximum, which means can store 1 MB)
      - Memory type: Auto
      - Independent clocks: No
      - Enable packet mode: No
      - ACLKEN conversion mode: None
      - TDATA width (bytes): 32
      - Enable TKEEP: Yes
      - Enable TLAST: Yes

      A total of 32768 bytes so several maximum size frames.
   b. For connecting the MRMAC Rx data stream:
      - Connect port rx_axis_tdata0[63:0] to FIFO tdata[63:0]
      - Connect port rx_axis_tdata1[63:0] to FIFO tdata[127:64]
      - Connect port rx_axis_tdata2[63:0] to FIFO tdata[191:128]
      - Connect port rx_axis_tdata3[63:0] to FIFO tdata[255:192]
      - Connect port rx_axis_tkeep_user0[7:0] to FIFO tkeep[7:0]
      - Connect port rx_axis_tkeep_user1[7:0] to FIFO tkeep[15:8]
      - Connect port rx_axis_tkeep_user2[7:0] to FIFO tkeep[23:16]
      - Connect port rx_axis_tkeep_user3[7:0] to FIFO tkeep[31:24]

      There is no support to:
      - Store the err flag in rx_axis_tkeep_user0[8]
      - Record if the MRMAC attempt to output packet data when s_axis_tready is de-asserted, meaning receive data will be lost.
   c. axis_data_fifo_1 converts from 256 bits at mrmac_axis_rx_aclk (322.2655 MHz) to 256 bits at 250 MHz for the XDMA C2H:
      - FIFO depth: 1024. Means can store 32 KB.
      - Memory type: Auto:
      - Independent clocks: Yes
      - CDC sync stages: 3 (the default)
      - ACLKEN conversion mode: No
      - TDATA width (bytes): 32
      - Enable TKEEP: Yes
      - Enable TLAST: Yes
   d. proc_sys_reset_0 to generate the reset for axis_data_fifo_0 and axis_data_fifo_1 which operate
      on a different clock domain to the xdma_axis_aresetn reset output.

