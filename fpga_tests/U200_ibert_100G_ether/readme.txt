The U200_ibert_100G_ether project is designed to run the "IBERT for UltraScale GTY Transceivers v1.3"
https://docs.amd.com/v/u/en-US/pg196-ibert-ultrascale-gty on the two QSFP ports.

It was intended to test QSFP modules operating for 100G Ethernet.

The steps to create the project were:
1. Create the U200_ibert_100G_ether project.
   From the IP Catalog double-click "IBERT Ultrascale GTY" and customise the IP:
   a. Add protocol as "Custom 1" with:
      - 25.78125 Gbps line rate (same as 100G Ethernet CAUI-4 protocol)
      - Data width 80
      - Quad Count 2
      - Refclk 161.1328125 MHz. After selecting the 25.78125 Gbps line rate, a 156.2500 Mhz refclk frequency wasn't listed as an option.
        Whereas with the CMAC the 
   b. Configure the protocol selection with:
      - QUAD_224 to QUAD_227 as None
      - QUAD_230 as "Custom 1/ 25.78125 Gbps" with Refclk selection "MGTREFCLK1 230"
      - QUAD_231 as "Custom 1/ 25.78125 Gbps" with Refclk selection "MGTREFCLK1 231"

      The Refclk's are connected to the SI5335A-B06201-GM Selectable output Oscillator 156.2500Mhz/161.1328125Mhz.
      The U200 documentation suggests the SI5335A default output frequency is 161.1328125 Mhz when the frequency selection signals
      on the FPGA are Hi-Z.
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

3. Created a block diagram as the top-level, with the example_ibert_ultrascale_gty_0 created by the
   example as a module. The block diagram connects the ports to all the signals on the example_ibert_ultrascale_gty_0
   module. The reason for adding the block diagram was to allow a PCIe core and associated peripherals to allow
   access to the QSFP management signals.

   After making the example_ibert_ultrascale_gty_0 lower in the hierarchy had to edit the
   ibert_ultrascale_gty_ip_example.xdc get_pins TX/RX out clock clock constraints to be able to
   find the pins and avoid critical warnings.

4. The AXI slave memory map:
   a. 0x0 - 0x3_FFFF is the CMS subsystem. Of which:
      - 0x0-0x1_FFFF is the CMS firmware memory which doesn't need to be mapped, since writes could corrupt the firmware.
      - 0x020000 MicroBlaze reset register. Active-Low. Default 0x0 (reset active)
      - 0x022000-0x022FFF  Host Interrupt Controller
      - 0x028000-0x029FFF  Host/CMS shared memory map
   b. 0x4_0000 - 0x4_1FFF is the SYSMON which has two slaves enabled (since a SSI device)

   c. 0x4_2000 - 0x4_2FFF user access GPIO.

   d. 0x4_3000 - 0x4_3FFF dual GPIO to control the output pins to select the Refclk frequencies.
      Since the AXI GPIO doesn't allow output bits to be read back, configured the AXI GPIO as:
      - Dual channel
      - 1st channel is all inputs
      - 2nd channel is all outputs
      - Output channel bits are connected to input channel bits.

      The bits are:
      Bit 0 : QSFP0_FS[0]
      Bit 1 : QSFP0_FS[1]
      Bit 2 : QSFP0_REFCLK_RESET
      Bit 3 : QSFP1_FS[0]
      Bit 4 : QSFP1_FS[1]
      Bit 5 : QSFP1_REFCLK_RESET

   e. 0x4_4000 - 0x4_4FFF QUAD SPI to access the configuration memory.
