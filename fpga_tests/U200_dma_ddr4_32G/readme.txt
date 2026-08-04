This is a modified version of U200_dma_ddr4 created by:
1. Copying the U200_dma_ddr4/create_project.tcl file and manually editing the project prefix to U200_dma_ddr4_32G
2. Initially attempted to create /U200/custom_memparts.csv which was a modified version of
   /opt/Xilinx/2025.2/data/ip/xilinx/mem_v1_4/csv/ddr4_sdram/memparts.csv
   Modifications were:
   a. Copy the MTA18ASF1G72PZ-2G1 row, which is the required density but PC4-2133.
   b. In the new row:
      - Change the part name to MTA18ASF1G72PZ-2G3
      - Copy the following columns from the MTA18ASF2G72PZ-2G3:
        - Memory speed grade
        - Min period
        - Max period
        - Model speed grade
   c. In theory, had created the new MTA18ASF1G72PZ-2G3 which has the density for a 8 GB RDIMM with x4 components, and a speed of PC4-2400.
   d. However, when attempted to import and use the custom_memparts.csv had errors:
      - When tried to import the custom_memparts.csv got numerous errors for the parts which hadn't modified. E.g.:
         [Mig 66-120] Custom Part (MT40A4G4FSE-083E) with parameter: Data mask having value: 1 is invalid. 
         The valid value for Data mask is 0 when Memory component width is 4.
      - Deleting all rows in the custom_memparts.csv except those for the MTA18ASF1G72PZ and MTA18ASF2G72PZ RDIMMs then allowed the file
        to be imported and the new MTA18ASF1G72PZ-2G3 selected in the ddr4_0 IP configuration.
      - However, when attempted to refresh the block containers got errors:
          ERROR: [IP_Flow 19-3461] Value '12' is out of the range for parameter 'C0.DDR4 CasWriteLatency(C0.DDR4_CasWriteLatency)' for BD Cell 'ddr4_0' . Valid values are - 14, 18
          ERROR: [BD 41-274] U200_ddr4_channel_inst_0_ddr4_0_0 (xilinx.com:ip:ddr4:2.2) could not be created.
          ERROR: [BD 41-2320] Could not instantiate U200_ddr4_channel into <U200_dma_ddr4_32G> because of the following reason:
          * 
          ERROR: [BD 41-6] ddr4_0 does not have a port called c0_ddr4_ui_clk
      - On inspection, the memparts.csv copied from the Vivado IP directory has fewer columns that the custom_parts_ddr4_2016_4_and_above.csv on
        "63462 - UltraScale/UltraScale+ Memory IP - Sample CSV data file for creating Custom Parts" 
        https://adaptivesupport.amd.com/s/article/63462?language=en_US
   d. Delete all rows in the custom_memparts.csv except those for the MTA18ASF1G72PZ and MTA18ASF2G72PZ RDIMMs
3. Created multiple_boards/custom_parts_ddr4_2016_4_and_above.csv based upon the example downloaded above. Added a new row with the columns set as:
    a. Part type: RDIMMs
    b. Part name: MTA18ASF1G72PZ-2G3
    c. Copy the following from MTA18ASF1G72PZ-2G1 in the Vivado IP memparts.csv:
       - Rank..CK width
       - Memory density..Data widths
    d. The following were copied from MTA18ASF2G72PZ-2G3 in the Vivado IP memparts.csv:
       - Memory speed grade: 83
       - Min period: 833
       - Max period: 1600
    e. tCKE: 5000 ps
       tFAW: 13328 ps
       tFAW_dlr: 16 tck
       tMRD: 8 tck
       tRAS: 33000 ps
       tRCD: 14160 ps
       tREFI: 7800000 ps (for -40°C to 85°C)
       tRFC: 350000 ps (datasheet tRFC1)
       tRFC_dlr: 110000 ps (datasheet tRFC4)
       tRP: 14160 ps
       tRRD_S: 3332 ps
       tRRD_L: 4900 ps
       tRRD_dlr: 0
       tRTP: 7500 ps
       tWR: 15000 ps
       tWTR_S: 2500 ps
       tWTR_L: 7500 ps
       tXPR: 360 ns
       tZQCS: 128 tck
       tZQINIT: 1024 tck
       cas latency: 17
       cas write latency: 12 (CWL - Primary Choice 1tCK Preamble)

       Values from MT40A1G4 component datasheet.
       For tRFC and tRFC_dlr based the selection against the values for other devices.
    f. tCCD_3ds: 0
       Since not a 3DS part
    g. burst length: 8
       The MIG is limited to this value.
4. In the ddr4_0 IP configuration under Basic -> Controller Options
   a. Tick Enable Custom Parts Data File
   b. For the Custom Parts Data File select the multiple_boards/custom_parts_ddr4_2016_4_and_above.csv created above.
      Successfully validated
   c. Change the memory part to MTA18ASF1G72PZ-2G3
5. On the U200_dma_ddr4_32G block diagram Address Editor edit the base address and range of all the DDR4 channels
   to be 8G per channel, and a total consecutive address range of 32G.
   Did that manually since if attempted to un-assign and then automatically assign the addresses:
   - All but the first channel remained at 8GB range.
   - The ECC register addresses got changed.
6. Changing the PCIe revision to 0x04, to identify the different amount of memory.
7. On the U200_DDR4_channel block diagram change the DDR4 address range from 16G to 8G.
   Not sure if that was the cause of the above issues trying to automatically assign the updated address range.
8. After using write_project_tcl noticed the generated create_project.tcl contains a write_ddr4_file_U200_ddr4_channel_ddr4_0_0 procedure
   which creates an IP file with the contents of custom_parts_ddr4_2016_4_and_above.csv which was loaded.

   The create_project.tcl does contain a reference to the source custom_parts_ddr4_2016_4_and_above.csv, so not sure what happens
   if the source file gets modified. E.g. does the project then use an outdated MIG configuration for the custom parts?

   After performing the implementation, which failed timing, ran write_project_tcl. That time generated create_project.tcl
   had $origin_dir/U200_dma_ddr4_32G/U200_dma_ddr4_32G.srcs/sources_1/bd/U200_ddr4_channel/ip/U200_ddr4_channel_ddr4_0_0/custom_parts_ddr4_2016_4_and_above.csv
   as a local file. Manually reverted that change.
9. While the MT40A1G4 component only uses A[15:0] for row addressing, the MIG is still generating the DDR4_adr[16:0] so will use all the
   address pins used in the constraints.

   The component pin is RAS_n/A16, so dual-use.
10. Timing failed for setup on 98 endpoints. Worst Negative Slack -0.233 ns
    Looking at the implemented design, the failing paths span SL0 and SL1.
    The clock is mmcm_clkout0 which is 300 MHz, sourced from SYSCLK0_300. I.e. for DDR4 channel 0
11. Trying a lower memory speed:
    a. In the ddr4_0 IP configuration:
       - Untick "Enable Custom Data Parts File"
       - Change the memory part to MTA18ASF1G72PZ-2G1
       - Change the Memory Device Interface Speed from 833 to 938 to meet the maximum data rate of the MTA18ASF1G72PZ-2G1 part.
         This is PC4-2133
       - Change the Reference Input Clock Speed (ps) to "3325 (299.84MHz)" to get back to the 300 MHz reference clock input.
         The Reference Input Clock Speed setting changed when the Memory Device Interface Speed was changed.
    b. The c0_ddr4_ui_clk output from the ddr4_0 block has been reduced from 300 MHz to 266.5 MHz as a result of the lower
       Memory Device Interface Speed, which could help with timing.
    c. Remove the following from the Design sources:
        /home/mr_halfword/fpga_sio/fpga_tests/U200_dma_ddr4_32G/U200_dma_ddr4_32G/U200_dma_ddr4_32G.srcs/sources_1/bd/U200_ddr4_channel/ip/U200_ddr4_channel_ddr4_0_0/custom_parts_ddr4_2016_4_and_above.csv
        /home/mr_halfword/fpga_sio/multiple_boards/custom_parts_ddr4_2016_4_and_above.csv

    The timing was met.
12. Went back to using the MTA18ASF1G72PZ-2G3 custom part with a Memory Device Interface Speed of 833. This is DDR4-2400.
    Added partitions.xdc with the following, as an initial attempt to avoid the timing issues with the SLR crossings:
      set_property USER_SLR_ASSIGNMENT SLR0 [get_cells U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_sc2axi]

    Timing wasn't met. Timing failed for setup on 23 endpoints. Worst Negative Slack -0.170 ns.
    The timing failures are reduced compared to the previous attempt without the additional constraints.

    Loaded the bitstream, which has a "User access build timestamp : CBB4BE09 - 25/07/2026 11:56:09".
    The MIG status in the debugger was reported as successful.
    All memory tests passed.

    The timing failures from U200_dma_ddr4_32G_wrapper_timing_summary_routed.rpt have been saved in DDR4-2400_timing_failures.txt,
    for future analysis.

    Since the memory tests passed, will commit the change, pending seeing if can get the timing to pass.

    The MIG_1 calibration results:
        Calibration Stage	Status
        1 - DQS Gate	PASS
        2 - DQS Gate Sanity Check	PASS
        3 - Write Leveling	PASS
        4 - Read Per-Bit Deskew	PASS
        5 - Read Per-Bit DBI Deskew	SKIP
        6 - Read DQS Centering (Simple)	PASS
        7 - Read Sanity Check	PASS
        8 - Write DQS to DQ Deskew	PASS
        9 - Write DQS to DM/DBI Deskew	SKIP
        10 - Write DQS to DQ (Simple)	PASS
        11 - Write DQS to DM/DBI (Simple)	SKIP
        12 - Read DQS Centering DBI (Simple)	SKIP
        13 - Write Latency Calibration	PASS
        14 - Write Read Sanity Check 0	PASS
        15 - Read DQS Centering (Complex)	PASS
        16 - Write Read Sanity Check 1	PASS
        17 - Read VREF Training	SKIP
        18 - Write Read Sanity Check 2	SKIP
        19 - Write DQS to DQ (Complex)	PASS
        20 - Write DQS to DM/DBI (Complex)	PASS
        21 - Write Read Sanity Check 3	PASS
        22 - Write VREF Training	SKIP
        23 - Write Read Sanity Check 4	SKIP
        24 - Read DQS Centering Multi Rank Adjustment	SKIP
        25 - Write Read Sanity Check 5	SKIP
        26 - Multi Rank Adjustment and Checks	SKIP
        27 - Write Read Sanity Check 6	SKIP
13. Looking at the Clocking Advanced Properties of axi_smc_1 which is used to connect the data for the DDR4 channels:
    - S00_Entry associated clock is aclk1: 250 MHz U200_dma_ddr4_32G_xdma_0_0_axi_aclk
    - SW0 associated clock is       aclk : 300 MHz U200_ddr4_channel_inst_0_ddr4_0_0_ddr4_ui_clk
    - M00_Exit associated clock is  aclk : 300 MHz U200_ddr4_channel_inst_0_ddr4_0_0_ddr4_ui_clk
    - M01_Exit associated clock is  aclk2: 300 MHz U200_ddr4_channel_inst_1_ddr4_0_0_ddr4_ui_clk
    - M02_Exit associated clock is  aclk3: 300 MHz U200_ddr4_channel_inst_2_ddr4_0_0_ddr4_ui_clk
    - M03_Exit associated clock is  aclk4: 300 MHz U200_ddr4_channel_inst_3_ddr4_0_0_ddr4_ui_clk

    I.e. the "switcher" SW0 is running at 300 MHz from DDR4 channel 0. That might explain the timing issues on signals between SLRs
    for DDR4 channel 0.

    On the block diagram, for axi_smc_1 swapped the connection to ack and ack1 so that
    - aclk is the 250 MHz clock from the XMDA
    - aclk1 is the 300 MHz clock from DDR channel 0

    After validating, the Clocking Advanced Properties of axi_smc_1 then showed the SW0 is now operating at 250 MHz.

    Timing wasn't met, and was significantly worse than on previous attempts.
    Is now failing on multiple clocks, for setup, hold and pulse width.
    Therefore, will not commit the changes.

    ------------------------------------------------------------------------------------------------
    | Design Timing Summary
    | ---------------------
    ------------------------------------------------------------------------------------------------

        WNS(ns)      TNS(ns)  TNS Failing Endpoints  TNS Total Endpoints      WHS(ns)      THS(ns)  THS Failing Endpoints  THS Total Endpoints     WPWS(ns)     TPWS(ns)  TPWS Failing Endpoints  TPWS Total Endpoints  
        -------      -------  ---------------------  -------------------      -------      -------  ---------------------  -------------------     --------     --------  ----------------------  --------------------  
         -4.355   -30259.623                  13261               547888       -3.381   -17942.541                   8444               543556       -0.219       -1.000                      11                220404  

14. Looking at the U200_dma_ddr4_32G_wrapper_io_placed.rpt to find the SLR region for different IOs:
    - DDR4 channel 0: SLR region 0
    - DDR4 channel 1: SLR region 1
    - DDR4 channel 2: SLR region 1
    - DDR4 channel 3: SLR region 2
    - PCIe          : SLR region 1

   On the axi_smc_1 reordered the clocks to be:
    - S00_Entry associated clock is ack1 : 250 MHz U200_dma_ddr4_32G_xdma_0_0_axi_aclk
    - SW0 associated clock is       aclk : 300 MHz U200_ddr4_channel_inst_1_ddr4_0_0_ddr4_ui_clk
    - M00_Exit associated clock is  aclk2: 300 MHz U200_ddr4_channel_inst_0_ddr4_0_0_ddr4_ui_clk
    - M01_Exit associated clock is  aclk : 300 MHz U200_ddr4_channel_inst_1_ddr4_0_0_ddr4_ui_clk
    - M02_Exit associated clock is  aclk3: 300 MHz U200_ddr4_channel_inst_2_ddr4_0_0_ddr4_ui_clk
    - M03_Exit associated clock is  aclk4: 300 MHz U200_ddr4_channel_inst_3_ddr4_0_0_ddr4_ui_clk

    This is to try running SW0 with a 300 MHz clock from DDR4 channel 1, which is in the same SLR region 1 as the PCIe.

    Timing wasn't met, and was significantly worse than attempts which didn't attempt to change the clocks used in axi_smc_1.
    Therefore, didn't commit the changes.

        ------------------------------------------------------------------------------------------------
        | Design Timing Summary
        | ---------------------
        ------------------------------------------------------------------------------------------------

            WNS(ns)      TNS(ns)  TNS Failing Endpoints  TNS Total Endpoints      WHS(ns)      THS(ns)  THS Failing Endpoints  THS Total Endpoints     WPWS(ns)     TPWS(ns)  TPWS Failing Endpoints  TPWS Total Endpoints  
            -------      -------  ---------------------  -------------------      -------      -------  ---------------------  -------------------     --------     --------  ----------------------  --------------------  
             -0.595     -502.610                   2463               547900        0.002        0.000                      0               543544       -0.146       -0.930                      13                220465  

    The timing failures are on:
    a. Intra-Clock paths:
       - mmcm_clkout0   (Setup and Pulse Width)
       - mmcm_clkout0_1 (Pulse Width)
       - mmcm_clkout0_2 (Pulse Width)
       - mmcm_clkout0_3 (Pulse Width)
    b. Inter-Clock paths:
       - mmcm_clkout0 to mmcm_clkout0_1 (Setup)
       - mmcm_clkout0_1 to mmcm_clkout0 (Setup)

    These are all 300 MHz clocks, and derived from reference clocks for each of the memory channels.

15. Tried replacing the "AXI Smart Connect" used for the DDR4 channels with the "AXI Interconnect (Discontinued)".
    For the configuration options:
    a. Top Level Settings -> Interconnect Optimization Strategy: Maximize Performance
    b. Advanced Options -> Interconnect Crossbar Options: Data Width of AXI Crossbar manual 512
       The automatic setting seemed to be 32-bits.

    Timing wasn't met:
        ------------------------------------------------------------------------------------------------
        | Design Timing Summary
        | ---------------------
        ------------------------------------------------------------------------------------------------

            WNS(ns)      TNS(ns)  TNS Failing Endpoints  TNS Total Endpoints      WHS(ns)      THS(ns)  THS Failing Endpoints  THS Total Endpoints     WPWS(ns)     TPWS(ns)  TPWS Failing Endpoints  TPWS Total Endpoints  
            -------      -------  ---------------------  -------------------      -------      -------  ---------------------  -------------------     --------     --------  ----------------------  --------------------  
             -0.114       -3.106                     77               520975        0.010        0.000                      0               516771       -0.505       -0.505                       1                214505  

        The timing failures are on:
    a. Intra-Clock paths:
       - mmcm_clkout0  : Setup
       - mmcm_clkout0_2: Pulse Width

    Tried using the design, but the memory tests failed with DMA timeouts.
    Possibly caused by not connecting the reset signals for the AXI Interconnect master ports?
    Therefore, didn't commit the design.

16. The PG247 (v1.0) SmartConnect v1.0 documentation mentions "Per-channel SLR Pipeline Control" options, to improve timing.

    Went back to the AXI Smart Connect for the DDR4 channels, and on the axi_smc_1 reordered the clocks to be:
    - S00_Entry associated clock is ack1 : 250 MHz U200_dma_ddr4_32G_xdma_0_0_axi_aclk
    - SW0 associated clock is       aclk : 300 MHz U200_ddr4_channel_inst_1_ddr4_0_0_ddr4_ui_clk
    - M00_Exit associated clock is  aclk2: 300 MHz U200_ddr4_channel_inst_0_ddr4_0_0_ddr4_ui_clk
    - M01_Exit associated clock is  aclk : 300 MHz U200_ddr4_channel_inst_1_ddr4_0_0_ddr4_ui_clk
    - M02_Exit associated clock is  aclk3: 300 MHz U200_ddr4_channel_inst_2_ddr4_0_0_ddr4_ui_clk
    - M03_Exit associated clock is  aclk4: 300 MHz U200_ddr4_channel_inst_3_ddr4_0_0_ddr4_ui_clk

    The following were set to 1 on the axi_smc_1 M00_Buffer and M03_Buffer, which should be on SLR crossings:
    - AR_SLR_PIPE
    - AW_SLR_PIPE
    - R_SLR_PIPE
    - W_SLR_PIPE
    - B_SLR_PIPE

    Timing wasn't met:
        ------------------------------------------------------------------------------------------------
        | Design Timing Summary
        | ---------------------
        ------------------------------------------------------------------------------------------------

            WNS(ns)      TNS(ns)  TNS Failing Endpoints  TNS Total Endpoints      WHS(ns)      THS(ns)  THS Failing Endpoints  THS Total Endpoints     WPWS(ns)     TPWS(ns)  TPWS Failing Endpoints  TPWS Total Endpoints  
            -------      -------  ---------------------  -------------------      -------      -------  ---------------------  -------------------     --------     --------  ----------------------  --------------------  
             -0.121       -2.519                     51               550745        0.006        0.000                      0               546388        0.000        0.000                       0                223163  

    The setup failures are all on Intra-Clock paths for mmcm_clkout0, which is for DDR4 channel 0.
    Looking at the Timing, for the 10 worst paths they are all between SLR0 and SLR1.

    Re-added the partitions.xdc constraint file to the project, and populated it with placement for the DDR4 channels 0 and 3 in the axi_smc_1.

    Timing wasn't met:
    WNS(ns)      TNS(ns)  TNS Failing Endpoints  TNS Total Endpoints      WHS(ns)      THS(ns)  THS Failing Endpoints  THS Total Endpoints     WPWS(ns)     TPWS(ns)  TPWS Failing Endpoints  TPWS Total Endpoints  
    -------      -------  ---------------------  -------------------      -------      -------  ---------------------  -------------------     --------     --------  ----------------------  --------------------  
     -0.200       -3.500                     58               550696        0.010        0.000                      0               546340        0.000        0.000                       0                223131  

    The setup failure are all on Intra-Clock paths for mmcm_clkout0, which is for DDR4 channel 0.
    Looking at the Timing, for the 10 worst paths:
    a. They are all between SLR0 and SLR1.
    b. They are actually all for paths on axi_smc, on the AXI control for the DDR4 channel 0 registers.

    axi_smc doesn't have the Advanced Properties enabled on the GUI, since it is for AXI4-Lite which is in low area mode.
    See https://adaptivesupport.amd.com/s/question/0D54U00008YzzeOSAR/axi-smartconnect-how-to-disable-low-area-mode-of-axi-smartconnet?language=en_US

    On the axi_smc swap the clock connections to be:
    - aclk  : U200_ddr4_channel_1_ddr4_ui_clk
    - aclk2 : U200_ddr4_channel_0_ddr4_ui_clk

    Timing wasn't met:
    WNS(ns)      TNS(ns)  TNS Failing Endpoints  TNS Total Endpoints      WHS(ns)      THS(ns)  THS Failing Endpoints  THS Total Endpoints     WPWS(ns)     TPWS(ns)  TPWS Failing Endpoints  TPWS Total Endpoints  
    -------      -------  ---------------------  -------------------      -------      -------  ---------------------  -------------------     --------     --------  ----------------------  --------------------  
     -2.321     -298.573                    991               550706        0.007        0.000                      0               546350       -0.141       -0.788                      10                223144  

    Used the following to disable the low area mode on axi_smc:
      set_property -dict [ list CONFIG.ADVANCED_PROPERTIES { __experimental_features__ {disable_low_area_mode 1 }} ] [get_bd_cells axi_smc]
      validate_bd_design

    The following were set to 1 on the axi_smc M00_Buffer and M03_Buffer, which should be on SLR crossings:
    - AR_SLR_PIPE
    - AW_SLR_PIPE
    - R_SLR_PIPE
    - W_SLR_PIPE
    - B_SLR_PIPE

    Updated partitions.xdc to populate it with the placement for axi_smc, as well as axi_smc_1.
    Used the same pblocks for both the axi_smc and axi_smc_1, since the placement is per SLR.

    Timing was then met.

17. To see if setting the PIPE adavanced properties were necessary manually edited the create_project.tcl file to remove the
    setting of CONFIG.ADVANCED_PROPERTIES for both axi_smc and axi_smc_1

    Built using Viado 2026.1. Timing was met and the memory tests passed.

    There were 14 Critical Warnings of the following form, on axi_smc nodes which didn't exist:
       [Vivado 12-1433] Expecting a non-empty list of cells to be added to the pblock.  Please verify the correctness of the <cells> argument. 
       ["/home/mr_halfword/fpga_sio_clean/fpga_tests/U200_dma_ddr4_32G/partitions.xdc":17]

    The nodes didn't exist since with the advanced properties remove from axi_smi, think that the SmartConnect was in Low Area mode.

    Disabled the partitions.xdc, and timing was met. That suggests Vivado 2026.1 is better at getting timing closure than 2025.1 was.

    In 2026.1 the axi_smc configuration GUI has a new option for "Area/Performance Tradeoff", which was initially set to Automatic.
    Changed from Automatic to High Performance. After validation, than enabled "Show Advanced Properties" in the configuration GUI.
    After Synthesis the Netlist showed axi_smc has the nodes targeted by partitions.xdc.

    Re-enabled partitions.xdc, and timing was met. The memory tests pass.

