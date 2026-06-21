This project is to try and test all 4 DDR4 channels on a U200. The 4 DDR4 SDRAM (MIG) blocks were configured as:
1. Created the block diagram U200_ddr4_channel to contain the IP, to allow to be configured in one place
   for use on multiple instances.
2. Basic -> Mode and Interface fixed (greyed out) as:
   - Controller/PHY Mode: Controller and physical layer
   - AXI Interface: Ticked

   PG150 says:
   "In IP integrator, only one controller instance can be created and only one kind of controller are available for instantiation"
3. Basic -> Clocking:
   a. Set Memory Interface Interface Speed (ps) to 833. This is the minimum allowed by the U200 User Guide,
      to give a data rate of 2400 MT/s
   b. PHY to controller clock frequency ratio 4:1 (the only option)
   c. Specify MMCM M and D on Advanced Clocking Page to calculate Ref Clk: Unticked
   d. Reference Input Clock Speed (ps) 3332 (300.12MHz)
      That is the closest to the actual reference of 300 MHz that the IP configuration is accepting.
      On looking at the Advanced Clocking can see has calculated integer values:
      - CLKFBOUT_MULT (M)   : 5
      - DIVCLK_DIVIDE (D)   : 1
      - CLKOUT0_DIVIDE (D0) : 5

      Which means the MMCM_CLKOUT (tCK/4) will be 300 MHz the same as the reference clock frequency;
      and the DDR4 memory clock will be 1200 MHz.

      The issue for the GUI is that the Memory Device Integer Speed can only be specified in integer pico-seconds.
      I.e. had to enter 833 ps rather than 833.3333333 ps which lead to the calculated nearest reference clock frequency of 300.12MHz.
4. Basic -> Controller options:
   a. Enable Custom Parts Data File: Unticked
   b. Configuration: RDIMMs
   c. Memory part: MTA18ASF2G72PZ-2G3
   d. Slot: Single
   e. IO Memory Voltage 1.2V (only option)
   f. Data Width: 72 (only option)
   g. ECC: Ticked (greyed out)
   h. Data Mask and DBI: NONE (greyed out)
      The RDIMM is built out of x4 components which don't have data mask / DBI pin.
      PG150 suggests the MIG should perform read-modify-write cycles to the DDR4 to allow byte writes from the AXI interface.
   i. Memory Address Map: ROW COLUMN BANK
      Leave at the default. Could impact efficiency of different traffic access patterns.
   j. Ordering: Normal
      Leave at the default. Could impact efficiency of different traffic access patterns.
   k. Force Read and Write commands to use AutoPrecharge when Column Address bit A3 is asserted high: Unticked
      Leave at the default. Could impact efficiency of different traffic access patterns.
5. Basic -> Advanced User Request Controller Options:
   a. Enable AutoPrecharge Input: Unticked
      No need for user logic to control AutoPrecharge.
   b. Enable User Refresh and ZQCS Input: Unticked
      No need for user logic to control refresh of ZQCS.
6. Basic -> Memory Options
   a. Burst Length: 8 (greyed out)
   b. CAS Latency: 18
      The lowest available value. The MTA18ASF2G72PZ-2G3 datasheet shows as a valid value for a DDR4-2400 datarate.
   c. CAS Write Latency: 12.
      The lowest available value. The MT40A2G4 component datasheet shows as a valid value for a DDR4-2400 datarate.
   d. Clamshell Topology: Unticked (greyed out)
      Only applicable to using components
7. AXI Options:
   a. Data Width: 512
      Matches the XDMA.
   b. Arbitration Scheme: RD PRI REG
      Use default, should only impact performance.
   c. ID Width: Auto
   d. Address Width: 34 (only option, which matches the 16GB total DDR4 size)
   e. AXI Narrow Burst: Ticked (manual)
      As suggested by https://adaptivesupport.amd.com/s/question/0D54U00007avGHnSAM/put-on-to-cecctest-register-in-ddr4-ip-mig-to-inject-ecc-fault.
      The implication is that without enabling AXI Narrow Burst support transfers which aren't a multiple of the 512 bit AXI data width
      could result in unexpected behaviour.
8. Advanced Clocking:
   a. Reference Input Clock Configuration: Differential
   b. Additional Clock Outputs: All None
9. Advanced Options:
   a. Debug Signals for controller: Enable
      PG150 says the XSDB debug interface is always included, but enabled debug to monitor pllLock output via GPIO.
   b. Microblaze MCS ECC: Unticked
      The Micoblaze is only used for calibration
   c. Enable Save-Restore: Unticked
      Save-restore would require user logic 
   d. Enable Migration: Unticked
      Not planning to migrate to a different FPGA.
10. Looked at enabling C_ECC_TEST to allow software to test ECC error injection.
    However, https://adaptivesupport.amd.com/s/question/0D54U00007avGHnSAM/put-on-to-cecctest-register-in-ddr4-ip-mig-to-inject-ecc-fault
    says you need to:
    a. Generate the IP output products as Global
    b. Manually edit the ddr4_0_ddr4.sv file outside of Vivado

    Therefore, decided to leave as generating the IP output products Out-of-Context and leaving C_ECC_TEST as OFF.

    On actually looking at the generated code with v2025.2, the C_ECC_TEST parameter is actually defaulting to ON.
11. On attempting to implement failed with the following error:
       [Mig 66-99] Memory Core Error - [U200_dma_ddr4_i/U200_ddr4_channel_3/ddr4_0]
       The memory interface port c3_ddr4_dq[66] has an invalid IOStandard SSTL12_DCI selected.
       Valid IOStandard for this port include: POD12_DCI. 

    The constraints had been copied Xilinx provided alveo-u250-xdc.xdc.
    For c3_ddr4_dq[66] changed the IOSTANDARD to POD12_DCI as suggested in the error message.

The AXI_LITE peripherals:
1. 0x0000 : DDR4 channel 0 ECC Control Registers
            Looking at the generated ip/U200_ddr4_channel_inst_0_ddr4_0_0/rtl/axi_ctrl/ddr4_v2_2_axi_ctrl_top.sv the number of address bits decoded
            is determined by the range of valid addresses. 0x400 of space is sufficient, with the highest ECC Control Register offset of 0x380.
2. 0x1000 : DDR4 channel 1 ECC Control Registers
3. 0x2000 : DDR4 channel 2 ECC Control Registers
4. 0x3000 : DDR4 channel 3 ECC Control Registers
5. 0x4000 : System Management Wizard
6. 0x6000 : QUAD SPI
7. 0x7000 : GPIO to read user access timestamp
8. 0x8000 : Device DNA
9. 0x9000 : A single port GPIO to manage the ddr4 reset. Configured for compatibility with the TOSING_160T_dma_ddr3 design
   and ddr3_reset_control.c:
   a. Port width is 9 bits. Bit 0 output. Bits 1-8 inputs.
   b. Bit 0 is active high reset for the ddr4 reset for all channels.
      Either this GPIO or PCIe reset can reset the ddr4.
      The idea is:
      - PCIe cold reset resets DDR4, but not hot reset. By the time VFIO opens the device DDR4 should be calibrated.
      - Software can force a reset of the DDR4 if required.
   c. Bit 1 input is pulled high. On TOSING_160T_dma_ddr3 this was:
      "Active high locked output from clocking wizard which generates the MIG clocks"
   d. Bit 2 input is dbg_pllGate (PLL Lock Indicator) ANDed from the MIGs for all channels.
   e. Bit 3 input is c0_init_calib_complete ANDed from the MIGs for all channels.
   f. Bit 4 input is c0_ddr4_ui_clk_sync_rst ORed from the MIGS for all channels.
   g. Bit 5 input is c0_ddr4_alert_n from the RDIMM
   h. Bit 5 input is c1_ddr4_alert_n from the RDIMM
   i. Bit 5 input is c2_ddr4_alert_n from the RDIMM
   i. Bit 5 input is c3_ddr4_alert_n from the RDIMM

   Just connected c?_ddr4_alert_n to the GPIO inputs so all the pins in the DDR4 constraints get used.
   alert_n is pulsed, so simply polling the GPIO input will probably miss errors.
   Consider latching these alerts, and then having some way of clearing them.

   To simplify the interface, apart from the alert_n signals decided to have a single reset control and status for all DDR4 channels.

   While did manage to add the c?_ddr4_alert_n signals as inputs on the U200_dma_ddr4 block diagram found that if closed and then
   reopened the project, on attempting open the block diagram got errors of the following form and the input ports didn't appear:
      [BD 41-2162] Unable to find pin <c0_ddr4_alert_n> in cell <> while setting up net connections during loading of BD file.

   The issue remained after recreated the project.

   By changing the name of the interface ports to c?_ddr4_alertn then got preserved in the block diagram.
   Perhaps the trailing "_n" in the interface ports got them interpreted as the negative half of a differential signal.
10. 0x40000 : Card Management System


For meeting timing:
a. Initial attempt to implement failed to meeting time on 533 endpoints for setup. Worst case negative slack -0.233 ns.
b. Changed the implementation strategy from "Vivado Implementation Defaults" to "Performance_Explore".
   The design still failed to meet timing. Has reduced to 75 endpoints for setup, with a worst case negative slack of -0.124 ns.
c. Changed the implementation strategy to "Performance_ExtraTimingOpt".
   The design still failed to meet timing. Has reduced to 22 endpoints for setup, with a worst case negative slack of -0.037 ns.
   Looking at the implemented design, the failing paths span SL0 and SL1.
d. Changed the implementation strategy to "Performance_BalanceSSLs"
   The design still failed to meet timing. Has increased to 60 endpoints for setup, with a worst case negative slack of -0.139 ns.
   Looking at the implemented design, the failing paths span SL0 and SL1.
e. Changed the implementation strategy to "Vivado Implementation Defaults" and tried selecting individual directive:
   - opt_design : Explore
   - place_design : Explore
   - phys_opt_design : Explore
   - route_design : Explore
   - phys_opt_design : Explore

   The design still failed to meet timing. Has increased to 75 endpoints for setup, with a worst case negative slack of -0.0124 ns
   Looking at the implemented design, the failing paths span SL0 and SL1.
f. Changed the implementation strategy back to "Performance_ExtraTimingOpt".
   The design failed to meet timing. Has 22 endpoints failing for setup, with a worst case negative slack of -0.037 ns.
   Looking at the implemented design, the failing paths span SL0 and SL1.
   The clock is mmcm_clkout0 which is 300 MHz, source from SYSCLK0_300. I.e. for DDR4 channel 0.

