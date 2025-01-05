The XCKU5P_DUAL_QSFP_ibert_4.166 project is designed to run the "IBERT for UltraScale GTY Transceivers v1.3"
https://docs.amd.com/v/u/en-US/pg196-ibert-ultrascale-gty on the two QSFP ports.

It was intended to try and generate the maximum line rate FTLF8524P2BNV fibre channel SFP transceivers support.
While the FTLF8524P2BNV allows up to 4.25 Gbps, with the fixed reference clock of 156.25 MHz that line rate
isn't supported. Selected a line rate of 4.1666666666666 Gbps, since when that is entered into the
IBERT Ultrascale GTY configuration GUI a reference clock of 156.25 MHz is supported.

From the documentation it isn't clear how to calculate the supported line rates for a given reference clock,
and the GUI allows you to enter a line rate and then allows a selection of supported reference clocks.

The steps to create the project were:
1. Create the XCKU5P_DUAL_QSFP_ibert_4.166 project.
   From the IP Catalog double-click "IBERT Ultrascale GTY" and customise the IP:
   a. Add protocol as "Custom 1" with:
      - 4.1666666666666 Gbps line rate
      - Data width 80
      - Quad Count 2
      - Refclk 156.25 MHz
   b. Configure the protocol selection with:
      - QUAD_224 and QUAD_225 as None
      - QUAD_226 and QUAD_227 as "Custom 1 / 4.1666666666666 Gbps" with RefClk selection "MGTREFCLK1 227"
   c. Configure Clock Settings with:
      - System Clock as External LVCMOS18 pin K22 frequency 100 MHz
      - Don't add any RXOUTCLK probes

   create_project.tcl re-creates this project.
   The ibert_ultrascale_gty_0.xci file was moved from it's initial location under the XCKU5P_DUAL_QSFP_ibert_4.166/
   project directory to under ip/ to simplify the git configuration. Moved the file outside of Vivado, and then
   updated the location in the Vivado project editor.


2. Created the ibert_ultrascale_gty_0_ex project, by after configuring the IBERT IP in XCKU5P_DUAL_QSFP_ibert_4.166,
   right clicked the core and selected "Open IP Example Design".

    create_example_project.tcl re-creates this project.

    Manually moved files to the project top level and in the Vivado project removed the old files within the
    ibert_ultrascale_gty_0_ex/ directory and added the files in top level to change the files from local
    to remote to allow create_example_project.tcl to be run the files needing to be added to git control
    to be outside of the ignored project directory. 

3. Created a block diagram as the top-level, with the example_ibert_ultrascale_gty_0 created by the
   example as a module. The block diagram connects the ports to all the signals on the example_ibert_ultrascale_gty_0
   module. The reason for adding the block diagram was to allow a PCIe core and associated peripherals to allow
   access to the QSFP management signals.


   After making the example_ibert_ultrascale_gty_0 lower in the hierarchy had to edit the
   ibert_ultrascale_gty_ip_example.xdc get_pins TX/RX out clock clock constraints to be able to
   find the pins and avoid critical warnings.

4. After adding the DMA Bridge got 8 Critical Warnings of the following form:
   [Vivado 12-2285] Cannot set LOC property of instance 'XCKU5P_DUAL_QSFP_ibert_4_166_i/xdma_0/inst/pcie4_ip_i/inst/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[2].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[1].GTYE4_CHANNEL_PRIM_INST'... XCKU5P_DUAL_QSFP_ibert_4_166_i/xdma_0/inst/pcie4_ip_i/inst/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[2].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[1].GTYE4_CHANNEL_PRIM_INST. Instance XCKU5P_DUAL_QSFP_ibert_4_166_i/xdma_0/inst/pcie4_ip_i/inst/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[2].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[1].GTYE4_CHANNEL_PRIM_INST can not be placed in GTYE4_CHANNEL of site GTYE4_CHANNEL_X0Y9 because the bel is occupied by XCKU5P_DUAL_QSFP_ibert_4_166_i/example_ibert_ultras_0/inst/u_ibert_gty_core/inst/QUAD0.u_q/CH[1].u_ch/u_gtye4_channel. This could be caused by bel constraint conflict ["/home/mr_halfword/fpga_sio/fpga_tests/XCKU5P_DUAL_QSFP_ibert_4.166/ibert_ultrascale_gty_0_ex/ibert_ultrascale_gty_0_ex.gen/sources_1/bd/XCKU5P_DUAL_QSFP_ibert_4_166/ip/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0/ip_0/ip_0/synth/XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt.xdc":77]

   The XCKU5P_DUAL_QSFP_ibert_4_166_xdma_0_0_pcie4_ip_gt.xdc file is read only, and has been created by the IBERT IP which was configured
   to use GTY QUADS 226 and 227 to which the QSFP ports are connected to.

   The PCIe interface uses GTY QUADS 224 and 225, with the PCIe reference clock on MGTREFCLK0_225.

   The DMA/Bridge Subsystem for PCIe was set to Basic mode when created, and there was no control for GT Selection.
   Changed to Advance mode and the GT Quad was shown as "GTY Quad 227".
   Enabled GTY Qaud Selection and changed to "GTY Quad 225" (the quad used for the PCIe reference clock).
   This change to the GTY Qaud Selection removed the Critical Warnings.

4. The GPIO mapping for the QSFP control signals are:
   Bit 0 input  : MOD_PRSN
   Bit 1 input  : INTERRUPT
   Bit 2 output : RESET
   Bit 3 output : MOD_SEL
   Bit 4 output : LP_MODE
   Bit 5 output : LED

   Since the AXI GPIO doesn't allow output bits to be read back, configured the AXI GPIO as:
   - Dual channel
   - 1st channel is all inputs
   - 2nd channel is all outputs
   - Output channel bits are connected to input channel bits.

   I.e. the output channel bits can be read via the input channel.
   TBC: The delay before the output bits can be read back, and if there is any race condition.

Notes:
a. Not sure that have got the ibert_ultrascale_gty_0.xci file shared between the XCKU5P_DUAL_QSFP_ibert_4.166
   and ibert_ultrascale_gty_0_ex projects correctly, in the same way as the TOSING_160T_SFP_ibert design.

b. The bitstream detected:
   - 8 links when fitted a 40G QSFP+ DAC cable to both ports
   - 2 links when fitted a pair Finisar FTLF8524P2BNV of SDP+ modules in Mellanox QSFP+ to SFP+ adapters

