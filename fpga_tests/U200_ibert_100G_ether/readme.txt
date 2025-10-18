The U200_ibert_100G_ether project is designed to run the "IBERT for UltraScale GTY Transceivers v1.3"
https://docs.amd.com/v/u/en-US/pg196-ibert-ultrascale-gty on the two QSFP ports.

It was intended to test QSFP modules operating for 100G Ethernet.

The steps to create the project were:
1. Create the U200_ibert_100G_ether project.
   From the IP Catalog double-click "IBERT Ultrascale GTY" and customise the IP:
   a. Add protocol as "Custom 1" with:
      - 25.78125 Gbps line rate (same as 100G CAUI-4 protocol)
      - Data width 80
      - Quad Count 2
      - Refclk 161.1328125 MHz
   b. Configure the protocol selection with:
      - QUAD_224 to QUAD_227 as None
      - QUAD_230 and QUAD_231 as "Custom 1/ 25.78125 Gbps" with Refclk selection "MGTREFCLK1 231"
   c. Configure Clock Settings with:
      - System Clock as External LVDS pin AU19 frequency 156.250 MHz
      - Don't add any RXOUTCLK probes

   create_project.tcl re-creates this project.
   The ibert_ultrascale_gty_0.xci file was moved from it's initial location under the U200_ibert_100G_ether
   project directory to under ip/ to simplify the git configuration. Moved the file outside of Vivado, and then
   updated the location in the Vivado project editor.

2. Created the ibert_ultrascale_gty_0_ex project, by after configuring the IBERT IP in U200_ibert_100G_ether,
   right clicked the core and selected "Open IP Example Design".
   create_example_project.tcl re-creates this project.

   Manually moved files to the project top level and in the Vivado project removed the old files within the
   ibert_ultrascale_gty_0_ex/ directory and added the files in top level to change the files from local
   to remote to allow create_example_project.tcl to be run the files needing to be added to git control
   to be outside of the ignored project directory.
