The TOSING_160T_SFP_ibert is designed to run the "Integrated Bit Error Ratio Tester 7 Series GTX Transceivers v3.0"
https://docs.xilinx.com/v/u/en-US/pg132-ibert-7series-gtx on SFP1 (quad 115 0) and SFP2 (quad 115 3) where the two
SFP+ ports can have finisar FTLF8524P2BNV fibre channel SFP transceivers fitted and looped externally.

The PCIe 100MHz reference clock is used as the reference for the IBERT design, which uses a 2.5Gbps rate.

The steps to create the project were:
1. Create the TOSING_160T_SFP_ibert project, and configure the IBERT to:
   a. Add protocol as "Custom 1" with a 2.5 Gbps line rate, data width 40 and Refclk 100MHz
   b. Configure the protocol selection with:
      - QUAD_115 as "Custom 1 / 2.5 Gbps", refclk MGTREFCLK1 116 (PCIe reference clock)
   c. Configure Clock Settings with:
      - System Clock as QUAD116 1 (i.e. the reference clock) to avoid the need for an additional clock)
      - Don't add any RXOUTCLK probes

   create_project.tcl re-creates this project.
   Initially used this project to generate the output products for the configured IBERT core, and add a constraint
   file to set the PACKAGE_PIN properties for the GTX transceivers but the issues with that were:
   a. Got errors during placement. Didn't record the errors but adding constraints for just the PACKAGE_PIN
      looked to be causing clock routing conflicts within the QUADs.
   b. The generated ibert_7series_gtx_0 module, which was being used as the top-level, brought out the following
      which are not needed:
      - RXOUTCLK_O (RXOUTCLK probes are disabled in the core configuration)
      - GTREFCLK0_I (QUAD 115 reference clock not used in the core configuration)
      - SYSCLK_I (external system clock not used in the core configuration)
 
 2. Created the ibert_7series_gtx_0_ex project, by aftering configuring the IBERT IP in TOSING_160T_SFP_ibert,
    right clicked the core and selected "Open IP Example Design".

    The bitstream detected links when used a DAC copper cable between SFP1 and SFP2, but not with the fibre channel
    SFP transceivers. That was because by default the transmit lasers in the SFP transceivers were disabled.

    In the top-level example_ibert_7series_gtx_0 module manually added the following outputs:
// The finisar FTLF8524P2BNV fibre channel SFP transceiver datasheet
// https://edt.com/wp-content/uploads/2018/06/FTLF8524P2BNV.pdf
// says the " Laser output disabled on high or open" for the Tdis pin
// The TOSING_160T schematic shows the TX_DIS signals has a 4.7K pull-up to 3.3V.
// Therefore, need to drive these signals low to enable the laser.
assign SFP1_TX_DIS = 0;
assign SFP2_TX_DIS = 0;

    And added sfp_control_signals.xdc containing the PACKAGE_PIN constraints.

    create_example_project.tcl re-creates this project.

    This example project has example_ibert_7series_gtx_0.xdc which doesn't specify PACKAGE_PIN,
    but does specify LOC properties for the GTX transceivers.

Both projects are set to share the same ip/sources_1/ip/ibert_7series_gtx_0_ex/ibert_7series_gtx_0.xci configuration file
for the IBERT IP.

The following relative directory within the .xci file lead to the directory depth for the .xci file location being
set the generation directory was the directory containing this file:
    "gen_directory": "../../../../TOSING_160T_SFP_ibert.gen/sources_1/ip/ibert_7series_gtx_0",

In the .gitinore file the leading slash was required on the following to ignore the directory containing the
geneated project, but not the same lower level directory containing the ibert_7series_gtx_0.xci configuration file:
/ibert_7series_gtx_0_ex/

