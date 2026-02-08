The VD100_10G_ether_dual design is design to provide dual 10 GbE ports using the 
Versal Devices Integrated 100G Multirate Ethernet MAC Subsystem (MRMAC).

XDMA with two C2H / H2C AXI streams is used to interface to the data ports of the MRMAC to transmit / receive Ethernet packets.

The AXI slave memory map:
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
1. Create the mrmac_10G_dual.bd block diagram, to hide the GT blocks and connections on the top-level diagram.

   Initially attempted to use a hierarchy sub-block, but attempting to run the MRMAC block automation failed when
   the MRMAC was placed on hierarchy sub-block.
   See https://gist.github.com/Chester-Gillon/d87759ab5fb5dd1cb5c12ac3c7a8f292#32-block-automation-fails-in-a-hierarchy-sub-block

2. The MRMAC block configuration:
   a. For Configuration Type use "Static Configuration" since only intended to support dual 10G ports.
   b. Enable "Use Legacy GT Wizard in Example Design", since otherwise are unable to run block automation.
      See https://gist.github.com/Chester-Gillon/d87759ab5fb5dd1cb5c12ac3c7a8f292#31-block-automation-only-supported-with-legacy-gt-wizard
   c. Select MRMAC Configuration Preset "Start from Scratch" since are not using all ports.
   d. Select MRMAC Mode "MAC+PCS" since are not using FEC for 10 GbE.
      With a ConnectX-4 Lx connected to a T1700G-28TQ 3.0 switch with 10GBase-SR modules the ConnectX-4 reported FEC was none.
      The switch documentation doesn't mention support for FEC.
   e. Select MRMAC Site "MRMAC X0Y0" (the only option on the the part)
   f. Select MRMAC Data Rate as "Mixed Custom", since are not configuring all the ports.
   g. For MAC Port Configuration MAC Port0 and MAC Port1:
      - Set Data Rate to "10GE"
      - Disable "IEEE 1588v2" as not needed, and would require additional user logic.
      - Disable "TX Flow Control" as not needed, and would require additional user logic.
      - Disable "RX Flow Control" as not needed, and would require additional user logic.
      - Disable "Preemption" as not needed, and would require additional user logic.
      - Set AXI Datapath Interface to "Low Latency 32b Non-Segmented"
        Clock domain crossing and width conversion is required since:
        - The XMDA AXI streams are 256 bits at 250 MHz, which is fixed based upon the PCIe 16 GT/s x4 interface.
        - For 10GE the MRMAC data path options are all a 32 bit width.
        - For 10GE the MRMAC data path clocking options are either:
          - Low Latency using the tx_core_clk or rx_serdes_clk which are 644.531 MHz.
          - Independent with a AXI clock between 322.265 MHz and 644.531 MHz.
        - To avoid having two clock domain crossings, will try "Low Latency 32b Non-Segmented" with a clock domain crossing from
          the XMDA 250 MHz to the 644.531 MHz used by the MRMAC for the GTY interface.
          If have trouble getting timing closure with that, will have to try changing to "Independent 32b Non-Segmented"
   h. Select GT Type as "GTYP" (only option).
   i. Select GT RefClk as "156.25" MHz, which is the fixed reference clock frequency on the VD100.
   j. Select Number of Pipeline Stages between MRMAC and GT core as "0".
      May need to increase to get timing closure.
   h. For MAC Port Configuration MAC Port2 and MAC Port3:
      - Set Data Rate to N/U

3. After configuring the MRMAC ran block automation.
   The result of checking the connections made by block automation, excluding the data paths:
   a. Remove the Processor System Reset rst_xdma_0_250M added to the top level diagram. Only input had been connected to it.
   b. The s_axi_aclk, s_axi_aclk and s_axi_aresetn ports have been connected on the top level diagram. 
      64K has been mapped to the XDMA M_AXI_LITE to access the MRMAC Configuration registers, Status registers, and Statistics counters.
   c. Delete the gtpowergood_in and gtpowergood ports.
      Connect the gtpowergood output from the gt_guad_base to MRMAC gtpowergood_in input.
   d. Delete the ch0_loopback and ch1_loopback ports.
      When the ports were deleted the ch0_loopback and ch1_loopback inputs on the gt_quad_base disappeared.
      Was going to tie the ch0_loopback and ch1_loopback on the gt_quad_base to 3'b000, which 
      AM002 Versal Adaptive SoC GTY and GTYP Transceivers Architecture Manual says is "Normal operation".
      No need to do that with the ch0_loopback and ch1_loopback inputs no longer on the gt_quad_base.
   e. Delete the ch0_txrate, ch1_txrate, ch0_rxrate and ch1_rxrate ports.
      When the ports were deleted the ch0_txrate, ch1_txrate, ch0_rxrate and ch1_rxrate inputs on the gt_quad_base disappeared. 
      AM002 says the CH*_RXRATE and CH*_TXRATE ports can be used to switch between a pre-configured set of line rates.
      The design uses a single fixed line rate, so no rate switching is required.
   f. Delete the APB3_INTF port, as are not going to access the GT registers.
   g. Connect the apb3clk_quad port to the pl0_ref_clk CIPs clock, which is also used for the xmda_0_support gtwiz_freerun_clk.
      From the PG331 Versal Adaptive SoC Transceivers Wizard not sure if apb3clk needs to be a free running clock to support initialisation.
   h. Delete the following status ports, since registers will be used to obtain status:
      - stat_tx_port0
      - stat_tx_port1
      - stat_tx_port2
      - stat_tx_port3
      - stat_rx_port0
      - stat_rx_port1
      - stat_rx_port2
      - stat_rx_port3
   i. Delete the pm_rdy port, since just sample the statistics counter registers.
   j. Delete the pm_tick port, since no user logic to request sampling statistics registers.
   k. Delete the tx_preamblein port, since will not be using a custom preamble.
      CONFIGURATION_TX_REGx_0 registers show custom preamble is disabled by default.
   l. Delete the rx_preambleout port, since are not using custom preamble nor preemption.
   m. PG314 says for the s_axi_aclk signal:
        Note: The s_axi_aclk must be present for the MRMAC to function. If this clock is interrupted, the MRMAC enters an error state.

      Looking at a synthesised design shows in the MRMAC:
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
   n. Delete gt_tx_reset_done_out and gt_rx_reset_done_out ports as no need to monitor.
   o. For the the GTY clocks the frequencies (MHz) on the ports:
      - ch0_rx_usr_clk         : 644.531
      - ch0_rx_usr_clk2        : 322.2655
      - ch0_tx_usr_clk         : 644.531
      - ch0_tx_usr_clk2        : 322.2655
      - ch1_rx_usr_clk         : 644.531
      - ch1_rx_usr_clk2        : 322.2655
      - ch1_tx_usr_clk         : 644.531
      - ch1_tx_usr_clk2        : 322.2655
      - ch0_txusrclk           : 322.266
      - ch1_txusrclk           : 322.266
      - ch0_rxusrclk           : 322.266
      - ch1_rxusrclk           : 322.266
      - rx_alt_serdes_clk[3:0] : 322.266 
      - rx_core_clk[3:0]       : 644.531
      - rx_serdes_clk[3:0]     : 644.531
      - tx_alt_serdes_clk[3:0] : 322.266
      - tx_core_clk[3:0]       : 644.531

      Remove the ports for the above. Make the clock connections:
      - ch0_rx_usr_clk (mbufg_gt_1 MBUFG_GT_O1)    -> rx_core_clk[0]
                                                   -> rx_serdes_clk[0]
      - ch1_rx_usr_clk (mbufg_gt_1_1 MBUFG_GT_O1)  -> rx_core_clk[1]
                                                   -> rx_serdes_clk[1]
      - ch0_rx_usr_clk2 (mbufg_gt_1 MBUFG_GT_O2)   -> rx_alt_serdes_clk[0]
                                                   -> ch0_rxusrclk
      - ch1_rx_usr_clk2 (mbufg_gt_1_1 MBUFG_GT_O2) -> rx_alt_serdes_clk[1]
                                                   -> ch1_rxusrclk
      - ch0_tx_usr_clk (mbufg_gt_0 MBUFG_GT_O1)    -> tx_core_clk[0]
      - ch1_tx_usr_clk (mbufg_gt_0_1 MBUFG_GT_O1)  -> tx_core_clk[1]
      - ch0_tx_usr_clk2 (mbufg_gt_0 MBUFG_GT_O2)   -> tx_alt_serdes_clk[0]
                                                   -> ch0_txusrclk
      - ch1_tx_usr_clk2 (mbufg_gt_0_1 MBUFG_GT_O2) -> tx_alt_serdes_clk[1]
                                                   -> ch1_txusrclk
   p. For the following clocks on features not used, remove the ports and connect to s_axi_aclk:
      - rx_flexif_clk
      - tx_flexif_clk
      - rx_ts_clk
      - tx_ts_clk
   q. Since are using Low Latency mode the following aren't used for transfers, remove the ports and connect to s_axi_aclk:
      - rx_axi_clk
      - tx_axi_clk
   r. Ports which need to be connected to external pins, and add pin constraints:
      - Rename CLK_IN_D to SFP_REF
      - Rename GT_Serial to SFP_serial
   s. For the following remove the ports and connect to constant inputs since not intending to control with user logic:
      - ctl_tx_port0
      - ctl_tx_port1
      - ctl_tx_port2
      - ctl_tx_port3

      Use zero constants:
      - For send_idle, registers can be used to send idles if required.
      - For send_lfi, registers can be used to send Local Fault Indication if required.
      - For send_rfi, registers can be used to send Remote Fault Indication if required.
      - Don't override VLM BIP7 byte, since only used for multi-lane synchronisation and 10G Ethernet uses a single lane.
   t. Delete the following ports, leaving the signals unconnected on the MRMAC:
      - tx_axis_tlast_2
      - tx_axis_tvalid_2
      - tx_axis_tready_2
      - rx_axis_tlast_2
      - rx_axis_tvalid_2

      This is because as per PG314 Table 14: 10G Non-Segmented Signaling for 32 Bits shows the client2 signals should not be needed
      when client2 isn't used.
      See https://gist.github.com/Chester-Gillon/d87759ab5fb5dd1cb5c12ac3c7a8f292#5-unexpected-ports-when-enabled-only-client0-and-client1

4. For first attempt at implementing MRMAC Tx data paths (H2C streams) created mrmac_h2c_stream block diagram with:
   a. axis_data_fifo_0 to convert from 250 MHz 256 bits to 644.531 MHz 256 bits, FIFO depth 1024.
      This has a clock domain crossing from the XDMA stream to the MRMAC Tx core frequency 
      (with MRMAC set to "Low Latency 32b Non-Segmented").
      RAM type set to auto.
   b. axis_dwidth_converter_0 to convert from 644.531 MHz 256 bits to 644.531 MHz 32 bits
   c. axis_data_fifo_1 FIFO set to Packet Mode to collect a complete Ethernet frame for output by the MRMAC without causing
      a transmit underrun. FIFO depth 8192, for a total of 32768 bytes so several maximum size frames.
      RAM type set to auto.

   Timing failed on Intra-Clock Paths mmrac_10G_dual_0_mrmac_axis_tx_aclk_0 and mrmac_10G_dual_0_mrmac_axis_tx_aclk_1:
   a. For mrmac_10G_dual_0_mrmac_axis_tx_aclk_0 Setup Worst Slack -0.695 ns, failing endpoints 662
   b. For mrmac_10G_dual_0_mrmac_axis_tx_aclk_1 Setup Worst Slack -0.837 ns, failing endpoints 1281
   c. For both Pulse Width Worst Slack -0.553 ns, failing endpoints 19
      - For URAM Required 2.105 (475 MHz), Actual 1.552 (644 MHz)
      - For BRAM Required 1.626 (625 MHz), Actual 1.552 (644 MHz)

   The device is xcve2302-sfva784 speed grade -1LP

   DS958 Versal AI Edge Series Data Sheet: DC and AC Switching Characteristics gives:
   - Block RAM Fmax of 615 MHz for speed grade -1L
   - Ultra RAM Fmax of 500 MHz for speed grade -1L

   Which means with the speed grade neither the Block RAM nor Ultra RAM support running at the 644.531 MRMAC Tx core frequency.

5. Make the following changes for the MRMAC Tx data paths following the timing problems:
   a. In the MRMAC configuration change both MAC Port0 and Port1 from "Low Latency 32b Non-Segmented" to "Independent 32b Non-Segmented".
      Since this affects the AXI interface, no need to re-run the Block Automation which added the GT interface.
   b. Disconnect the MRMAC tx_ts_clk and rx_axi_clk ports from the s_axi_aclk
   c. Connect the MRMAC tx_axi_clk to tx_alt_serdes_clk (322.2655 MHz)
   d. Connect the MRMAC rx_axi_clk to rx_alt_serdes_clk (322.2655 MHz)
   e. PG314 Table 40: Clocks says "tx_axi_clk[0] and rx_axi_clk[0] are shared between Ports 0 and 1".
      Therefore, create the output port mrmac_axis_tx_aclk_0_1 connected to tx_alt_serdes_clk[0]
   f. On the top level diagram connect mrmac_axis_tx_aclk_0_1 to the mrmac_axis_tx_aclk input on
      both mrmac_h2c_stream_0 and mrmac_h2c_stream_1.
   g. On the mrmac_h2c_stream block diagram edit axis_data_fifo_0, axis_dwidth_converter_0 and axis_data_fifo_1 to enable
      tlast and tkeep, rather than auto.

      Otherwise, when sourced create_project.tcl:
      - tlast wasn't configured on axis_data_fifo_0
      - tlast wasn't configured on axis_dwidth_converter_0
      - tkeep wasn't configured on axis_data_fifo_1

   Following these changes, the timing was met on the MRMAC Tx data paths.

   When looked at the generated code for the axis_data_fifo_0 inside the mrmac_h2c_stream block, saw a xpm_cdc_handshake which
   thought could cause a performance throughput limit by requiring an Ack from the destination clock domain for every data
   transfer from the source clock domain.

   As a result modified mrmac_h2c_stream to use a "AXI4-Stream NOC" to perform the clock domain crossing, by connecting a
   NoC AXI4-Stream master and slave interface. Where the NoC master and slave interfaces take independent clocks and
   perform packetization and de-packetization.

   On looking at the schematic for the axis_data_fifo_0 the actual implementation uses a XPM_FIFO_AXIS where:
   - s_aclk is connected to the FIFO wr_clk
   - s_axis_tvalid is connected to the FIFO wr_en
   - s_axis_tready is connected to the FIFO full_n

   Therefore, looks OK in that the H2C stream can write to the FIFO until full, rather than as first thought every
   H2C transfer requiring an Ack'ed data transfer.
   A simulation or ILA could confirm this.

   For now, keep using the axis_data_fifo_0 for the clock domain crossing, rather than the NoC.

6. For first attempt at implementing MRMAC Tx data paths (H2C streams) created mrmac_c2h_stream block diagram with:
   a. On the mrmac_10G_dual block create output port mrmac_axis_rx_aclk_0_1 connected to rx_alt_serdes_clk[0]
      For the same reason as the mrmac_axis_tx_aclk_0_1.
   b. Add axis_data_fifo_0 to receive the Ethernet packets from the MRMAC. 32-bits clocked by mrmac_axis_rx_aclk_0_1.
      - Memory type set to Auto.
      - Depth set to 32768 which is the maximum. Means can store 128 KB.
      - Enable TKEEP set to Yes.
      - Enable TLAST set to Yes.

      Only uses the rx_axis_tkeep_user[3:0] from the MRMAC. There is no support to:
      a. Store the err flag in rx_axis_tkeep_user[8]
      b. Record if the MRMAC attempt to output packet data when s_axis_tready is de-asserted, meaning receive data will be lost.
   c. axis_dwidth_converter_0 converts from 32 to 256 bits.
   d. axis_data_fifo_1 converts from 256 bits at mrmac_axis_rx_aclk_0_1 (322.2655 MHz) to 256 bits at 250 MHz for the XDMA C2H.
      - Memory type set to Auto.
      - Depth set to 1024. Means can store 32 KB.
      - Enable TKEEP set to Yes.
      - Enable TLAST set to Yes.

   With the MRMAC Tx and Rx data path FIFOs utilisation is at:
   - 50% for Block RAM
   - 17% for Ultra RAM

   Therefore, space to increase on-board packet buffering if suffer from packet loss during testing.

