The configuration of the DDR4 SDRAM (MIG):
1. Basic -> Mode and Interface fixed (greyed out) as:
   - Controller/PHY Mode: Controller and physical layer
   - AXI Interface: Ticked

   PG150 says:
   "In IP integrator, only one controller instance can be created and only one kind of controller are available for instantiation"
2. Basic -> Clocking:
   a. Set Memory Interface Interface Speed (ps) to 750. This is the minimum allowed by the xcku5p-ffvb676-2-i running at VCCINT 0.85V
      using a single rank component, according to Table 27: Maximum Physical Interface (PHY) Rate for Memory Interfaces in DS922.
      This is is DDR4-2666 MT/s data rate.

      Checked from the schematic that the XCKU5P-1FFVB676 VCCINT is 0.85V

      The Micron part used MT40A512M16LY-062E-IT-E has -062E speed grade supports up to a DDR4-3200 datarate.
   b. PHY to controller clock frequency ratio 4:1 (the only option)
   c. Specify MMCM M and D on Advanced Clocking Page to calculate Ref Clk: Unticked
   d. Reference Input Clock Speed (ps) 10000 (100MHz)
3. Basic -> Controller options:
   a. Enable Custom Parts Data File: Unticked
   b. Configuration: Components
   c. Memory Part: MT40A512M16TB-062E
      This only differs from the actual component in the package:
      - TB is 96-ball 7.5mm x 13.0mm FBGA
      - LY is 96-ball 7.5mm x 13.5mm FBGA
   d. Slot: Single (only option)
   e. IO Memory Voltage 1.2V (only option)
      Banks 65 and 66 are used for the DDR4 interface, and the schematic shows they have VCCO of 1.2V.
   f. Data Width: 32
      Since have two x16 components
   g. ECC: Untickerd (greyed out)
   h. Data Mask and DBI: DM NO DBI
      Need data masks since using the AXI interface.
      Disable read DBI (Data Bus Inversion) since looks to lower performance by requiring a larger CL:
      - With DM NO DBI allowed CAS latency is 19 or 20
      - With DM BDI RD allowed CAS latency is 22 or 23.

      The MT40A512M16TB-062E datasheet shows enabling read DBI changes the allowed CL.
   i. Memory Address Map: ROW COLUMN BANK
      Leave at the default. Could impact efficiency of different traffic access patterns.
   j. Ordering: Normal
      Leave at the default. Could impact efficiency of different traffic access patterns.
   k. Force Read and Write commands to use AutoPrecharge when Column Address bit A3 is asserted high: Unticked
      Leave at the default. Could impact efficiency of different traffic access patterns.
4. Basic -> Advanced User Request Controller Options:
   a. Enable AutoPrecharge Input: Unticked
      No need for user logic to control AutoPrecharge.
   b. Enable User Refresh and ZQCS Input: Unticked
      No need for user logic to control refresh of ZQCS.
5. Basic -> Memory Options
   a. Burst Length: 8 (greyed out)
   b. CAS Latency: 19
      The lowest available value. The MT40A512M16TB-062E datasheet shows as a valid value for a DDR4-2666 datarate.
   c. CAS Write Latency: 14.
      The lowest available value. The MT40A512M16TB-062E datasheet shows as a valid value for a DDR4-2666 datarate.
   d. Clamshell Topology: Unticked
      The schematic shows a single DDR4_CS connected to both memory components.
6. AXI Options:
   a. Data Width: 256
      Matches the XDMA.
   b. Arbitration Scheme: RD PRI REG
      Use default, should only impact performance.
   c. ID Width: Auto
   d. Address Width: 31 (only option, which matches the 2GB total DDR4 size)
   e. AXI Narrow Burst: Unticked (auto)
7. Advanced Clocking:
   a. Reference Input Clock Configuration: Differential
      Initially attempted to use " No Buffer", so could use an external buffer to share the reference clock.
      However, resulted in:

        [Place 30-716] Sub-optimal placement for a global clock-capable IO pin-BUFGCE-MMCM pair. 
        If this sub optimal condition is acceptable for this design, you may use the CLOCK_DEDICATED_ROUTE constraint in the .xdc file to demote this message
        to a WARNING. However, the use of this override is highly discouraged. These examples can be used directly in the .xdc file to override this clock rule.
	< set_property CLOCK_DEDICATED_ROUTE BACKBONE [get_nets XCKU5P_SINGLE_QSFP_dma_ddr4_i/util_ds_buf_0/U0/xlnx_opt_] >

	Clock Rule: rule_bufg_mmcm_1load
	Status: FAILED
	Rule Description: A BUFGCE with I/O driver driving a single MMCM must both be in the same clock region if CLOCK_DEDICATED_ROUTE=BACKBONE is NOT set

	XCKU5P_SINGLE_QSFP_dma_ddr4_i/util_ds_buf_0/U0/IBUF_OUT[0]_BUFGCE_collapsed_inst (BUFGCE.O) is provisionally placed by clockplacer on BUFGCE_X0Y48
	XCKU5P_SINGLE_QSFP_dma_ddr4_i/ddr4_0/inst/u_ddr4_infrastructure/gen_mmcme4.u_mmcme_adv_inst (MMCME4_ADV.CLKIN1) is locked to MMCM_X0Y1

	The above error could possibly be related to other connected instances. Following is a list of 
	all the related clock rules and their respective instances.

   b. Additional Clock Outputs: All None
8. Advanced Options:
   a. Debug Signals for controller: Enable
      PG150 says the XSDB debug interface is always included, but enabled debug to monitor pllLock output via GPIO.
   b. Microblaze MCS ECC: Unticked
      The Micoblaze is only used for calibration
   c. Enable Save-Restore: Unticked
      Save-restore would require user logic 
   d. Enable Migration: Unticked
      Not planning to migrate to a different FPGA.


The AXI_LITE peripherals:
1. 0x0000 : QUAD SPI
2. 0x1000 : System Management Wizard
3. 0x2000 : GPIO to read user access timestamp
4. 0x3000 : Device DNA
5. 0x4000 : A single port GPIO to manage the ddr4 reset. Configured for compatibility with the TOSING_160T_dma_ddr3 design
   and ddr3_reset_control.c:
   a. Port width is 5 bits. Bit 0 output. Bits 1-4 inputs.
   b. Bit 0 is active high reset for the ddr4 reset.
      Either this GPIO or PCIe reset can reset the ddr4.
      The idea is:
      - PCIe cold reset resets DDR4, but not hot reset. By the time VFIO opens the device DDR4 should be calibrated.
      - Software can force a reset of the DDR4 if required.
   c. Bit 1 input is pulled high. On TOSING_160T_dma_ddr3 this was:
      "Active high locked output from clocking wizard which generates the MIG clocks"
   d. Bit 2 input is dbg_pllGate (PLL Lock Indicator) from the MIG.
   e. Bit 3 input is c0_init_calib_complete from the MIG.
   f. Bit 4 input is c0_ddr4_ui_clk_sync_rst from the MIG.

