Two critical warnings were reported for the implementation Methodology:

a. TIMING-6: No Common Primary Clock Between Related Clocks
   https://docs.amd.com/r/en-US/ug906-vivado-design-analysis/TIMING-6-No-Common-Primary-Clock-Between-Related-Clocks

   The clocks ch0_txoutclk and clk_pl_0 are related (timed together) but they have no common primary clock.
   The design could fail in hardware. To find a timing path between these clocks, run the following command:
     report_timing -from [get_clocks ch0_txoutclk] -to [get_clocks clk_pl_0]

b. TIMING-7: No Common Node Between Related Clocks
   https://docs.amd.com/r/en-US/ug906-vivado-design-analysis/TIMING-7-No-Common-Node-Between-Related-Clocks

   The clocks ch0_txoutclk and clk_pl_0 are related (timed together) but they have no common node.
   The design could fail in hardware. To find a timing path between these clocks, run the following command:
     report_timing -from [get_clocks ch0_txoutclk] -to [get_clocks clk_pl_0]

Running the suggested report:
report_timing -from [get_clocks ch0_txoutclk] -to [get_clocks clk_pl_0]
INFO: [Timing 38-91] UpdateTimingParams: Speed grade: -1LP, Temperature grade: E, Delay Type: max.
INFO: [Timing 38-191] Multithreading enabled for timing update using a maximum of 8 CPUs
INFO: [Timing 38-78] ReportTimingParams: -from_pins  -to_pins  -max_paths 1 -nworst 1 -delay_type max -sort_by slack.
WARNING: [Timing 38-164] This design has multiple clocks. Inter clock paths are considered valid unless explicitly excluded by timing constraints such as set_clock_groups or set_false_path.
Copyright 1986-2022 Xilinx, Inc. All Rights Reserved. Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
--------------------------------------------------------------------------------------------------------------------------------------------------
| Tool Version      : Vivado v.2025.2 (lin64) Build 6299465 Fri Nov 14 12:34:56 MST 2025
| Date              : Tue Dec 16 15:54:16 2025
| Host              : Haswell-Ubuntu running 64-bit Ubuntu 24.04.3 LTS
| Command           : report_timing -from [get_clocks ch0_txoutclk] -to [get_clocks clk_pl_0]
| Design            : VD100_dma_stream_crc64_wrapper
| Device            : xcve2302-sfva784
| Speed File        : -1LP  PRODUCTION 2.04 2025-08-19
| Design State      : Routed
| Temperature Grade : E
--------------------------------------------------------------------------------------------------------------------------------------------------

Timing Report

Slack (MET) :             0.272ns  (required time - arrival time)
  Source:                 VD100_dma_stream_crc64_i/xdma_0_support/pcie/inst/serial_pcie_top.pcie_5_0_pipe_inst/pcie_5_0_e5_inst/CORECLK
                            (rising edge-triggered cell PCIE50E5 clocked by ch0_txoutclk  {rise@0.000ns fall@1.000ns period=2.000ns})
  Destination:            VD100_dma_stream_crc64_wrapper/VD100_dma_stream_crc64/xdma_0_support_imp_MKABBP/VD100_dma_stream_crc64_pcie_0/VD100_dma_stream_crc64_pcie_0_core_top/VD100_dma_stream_crc64_pcie_0_dbg_intf/<hidden>
                            (rising edge-triggered cell FDRE clocked by clk_pl_0  {rise@0.000ns fall@5.000ns period=10.000ns})
  Path Group:             clk_pl_0
  Path Type:              Setup (Max at Slow Process Corner)
  Requirement:            4.000ns  (clk_pl_0 rise@10.000ns - ch0_txoutclk rise@6.000ns)
  Data Path Delay:        1.914ns  (logic 0.859ns (44.880%)  route 1.055ns (55.120%))
  Logic Levels:           1  (LUT5=1)
  Clock Path Skew:        -1.692ns (DCD - SCD + CPR)
    Destination Clock Delay (DCD):    1.985ns = ( 11.985 - 10.000 ) 
    Source Clock Delay      (SCD):    3.677ns = ( 11.677 - 8.000 ) 
    Clock Pessimism Removal (CPR):    0.000ns
  Clock Uncertainty:      0.119ns  (CJ)/2 + PE + PJ
    Clock Jitter             (CJ):    0.238ns
    Phase Error              (PE):    0.000ns
    Phase Jitter             (PJ):    0.000ns
  Clock Net Delay (Source):      2.902ns (routing 0.828ns, distribution 2.074ns)
  Clock Net Delay (Destination): 1.913ns (routing 0.238ns, distribution 1.675ns)
  Timing Exception:       MultiCycle Path   Setup -start 2
  Clock Domain Crossing:  Inter clock paths are considered valid unless explicitly excluded by timing constraints such as set_clock_groups or set_false_path.

    Location             Delay type                Incr(ns)  Path(ns)    Netlist Resource(s)
  -------------------------------------------------------------------    -------------------
                         (clock ch0_txoutclk rise edge)
                                                      6.000     6.000 r  
    GTYP_QUAD_X0Y0       GTYP_QUAD                    0.000     6.000 r  VD100_dma_stream_crc64_i/xdma_0_support/gtwiz_versal_0/inst/intf_quad_map_inst/quad_top_inst/gt_quad_base_0_inst/inst/quad_inst/CH0_TXOUTCLK
                         net (fo=4, routed)           0.465     6.465    VD100_dma_stream_crc64_i/xdma_0_support/pcie_phy/U0/diablo_gt_phy_wrapper/gt_top_i/diablo_gt_phy_wrapper/phy_clk_i/gt_txoutclk
    BUFG_GT_X0Y9         BUFG_GT (Prop_BUFG_GT_I_O)
                                                      0.310     6.775 r  VD100_dma_stream_crc64_i/xdma_0_support/pcie_phy/U0/diablo_gt_phy_wrapper/gt_top_i/diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_coreclk/O
    X1Y2 (CLOCK_ROOT)    net (fo=1314, routed)        2.902     9.677    VD100_dma_stream_crc64_i/xdma_0_support/pcie/inst/serial_pcie_top.pcie_5_0_pipe_inst/phy_coreclk
    PCIE50_X0Y0          PCIE50E5                                     r  VD100_dma_stream_crc64_i/xdma_0_support/pcie/inst/serial_pcie_top.pcie_5_0_pipe_inst/pcie_5_0_e5_inst/CORECLK
  -------------------------------------------------------------------    -------------------
    PCIE50_X0Y0          PCIE50E5 (Prop_PCIE50_CORECLK_CFGNEGOTIATEDWIDTH[2])
                                                      0.643    10.320 r  VD100_dma_stream_crc64_i/xdma_0_support/pcie/inst/serial_pcie_top.pcie_5_0_pipe_inst/pcie_5_0_e5_inst/CFGNEGOTIATEDWIDTH[2]
                         net (fo=1, routed)           0.995    11.315    VD100_dma_stream_crc64_i/xdma_0_support/pcie/inst/pcie_dbg_probes/cfg_negotiated_width[2]
    SLICE_X27Y110        LUT5 (Prop_C6LUT_SLICEM_I4_O)
                                                      0.216    11.531 r  VD100_dma_stream_crc64_i/xdma_0_support/pcie/inst/pcie_dbg_probes/pcie_dbg_intf_i_14/O
                         net (fo=1, routed)           0.060    11.591    VD100_dma_stream_crc64_wrapper/VD100_dma_stream_crc64/xdma_0_support_imp_MKABBP/VD100_dma_stream_crc64_pcie_0/VD100_dma_stream_crc64_pcie_0_core_top/VD100_dma_stream_crc64_pcie_0_dbg_intf/<hidden>
    SLICE_X27Y110        FDRE                                         r  VD100_dma_stream_crc64_wrapper/VD100_dma_stream_crc64/xdma_0_support_imp_MKABBP/VD100_dma_stream_crc64_pcie_0/VD100_dma_stream_crc64_pcie_0_core_top/VD100_dma_stream_crc64_pcie_0_dbg_intf/<hidden>
  -------------------------------------------------------------------    -------------------

                         (clock clk_pl_0 rise edge)
                                                     10.000    10.000 r  
    PS9_X0Y0             PS9                          0.000    10.000 r  VD100_dma_stream_crc64_i/versal_cips_0/U0/pspmc_0/U0/PS9_inst/PMCRCLKCLK[0]
                         net (fo=1, routed)           0.000    10.000    VD100_dma_stream_crc64_i/versal_cips_0/U0/pspmc_0/U0/pmc_pl_ref_clk[0]
    BUFG_PS_X0Y6         BUFG_PS (Prop_BUFG_PS_I_O)
                                                      0.072    10.072 r  VD100_dma_stream_crc64_i/versal_cips_0/U0/pspmc_0/U0/buffer_pl_clk_0.PL_CLK_0_BUFG/O
    X1Y1 (CLOCK_ROOT)    net (fo=2692, routed)        1.913    11.985    VD100_dma_stream_crc64_wrapper/VD100_dma_stream_crc64/xdma_0_support_imp_MKABBP/VD100_dma_stream_crc64_pcie_0/VD100_dma_stream_crc64_pcie_0_core_top/VD100_dma_stream_crc64_pcie_0_dbg_intf/<hidden>
    SLICE_X27Y110        FDRE                                         r  VD100_dma_stream_crc64_wrapper/VD100_dma_stream_crc64/xdma_0_support_imp_MKABBP/VD100_dma_stream_crc64_pcie_0/VD100_dma_stream_crc64_pcie_0_core_top/VD100_dma_stream_crc64_pcie_0_dbg_intf/<hidden>
                         clock pessimism              0.000    11.985    
                         clock uncertainty           -0.119    11.866    
    SLICE_X27Y110        FDRE (Setup_CFF2_SLICEM_C_D)
                                                     -0.003    11.863    VD100_dma_stream_crc64_wrapper/VD100_dma_stream_crc64/xdma_0_support_imp_MKABBP/VD100_dma_stream_crc64_pcie_0/VD100_dma_stream_crc64_pcie_0_core_top/VD100_dma_stream_crc64_pcie_0_dbg_intf/<hidden>
  -------------------------------------------------------------------
                         required time                         11.863    
                         arrival time                         -11.591    
  -------------------------------------------------------------------
                         slack                                  0.272    

The timing report shows 10 failing, which are all setup on "Intra-Clock Paths - ch0_txoutclk".
Worst case slack is -0.126 ns

Not obvious runtime failure seen yet.


==============================

Added a pcie_timing.xdc constaint file to mark the ch0_txoutclk and clk_pl_0 clocks as asynchronous,
which removed the Methodology critical warnings.

The timing report still shows 10 setup failures on "Intra-Clock Paths - ch0_txoutclk".
Worst case slack is now reduced -0.038 ns.

