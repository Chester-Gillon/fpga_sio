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

    In the top-level example_ibert_ultrascale_gty_0.v module manually added the following outputs:
  // Ensure QSFP modules aren't in reset
  assign QSFP_RESET_A = 1'b1;
  assign QSFP_RESET_B = 1'b1;

    create_example_project.tcl re-creates this project.

    Manually moved files to the project top level and in the Vivado project removed the old files within the
    ibert_ultrascale_gty_0_ex/ directory and added the files in top level to change the files from local
    to remote to allow create_example_project.tcl to be run the files needing to be added to git control
    to be outside of the ignored project directory. 

3. Created a block diagram as the top-level, with the example_ibert_ultrascale_gty_0 created by the
   example as a module. Initially the block diagram just has ports to connect to all the signals
   on the example_ibert_ultrascale_gty_0 in preparation for adding a PCIe core to be able to access
   the QSFP port management.

   After making the example_ibert_ultrascale_gty_0 lower in the hierarchy had to edit the
   ibert_ultrascale_gty_ip_example.xdc get_pins TX/RX out clock clock constraints to be able to
   find the pins and avoid critical warnings.

Not sure that have got the ibert_ultrascale_gty_0.xci file shared between the XCKU5P_DUAL_QSFP_ibert_4.166
and ibert_ultrascale_gty_0_ex projects correctly, in the same way as the TOSING_160T_SFP_ibert design.

The bitstream detected:
- 8 links when fitted a 40G QSFP+ DAC cable to both ports
- 2 links when fitted a pair Finisar FTLF8524P2BNV of SDP+ modules in Mellanox QSFP+ to SFP+ adapters

