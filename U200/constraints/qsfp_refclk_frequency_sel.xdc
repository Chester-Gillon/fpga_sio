#
# QSFP0 Clock Control Signals
#       FS[1:0] <-- Clock Select Pin FS[1:0] = 1X -> 161.132812 MHz 1.8V LVDS (default when FPGA pin Hi-Z due to 10K pullups)
#                                    FS[1:0] = 01 -> 156.250000 MHz 1.8V LVDS
#       RESET <-- Device Reset - Asserting this pin (driving high) is required to change FS1,FS0 pin setting. 
#
set_property -dict {PACKAGE_PIN AT20 IOSTANDARD LVCMOS12       } [get_ports QSFP0_FS[0]       ]; # Bank 64 VCCO - VCC1V2 Net "QSFP0_FS0"           - IO_L10P_T1U_N6_QBC_AD4P_64
set_property -dict {PACKAGE_PIN AU22 IOSTANDARD LVCMOS12       } [get_ports QSFP0_FS[1]       ]; # Bank 64 VCCO - VCC1V2 Net "QSFP0_FS1"           - IO_L9N_T1L_N5_AD12N_64
set_property -dict {PACKAGE_PIN AT22 IOSTANDARD LVCMOS12       } [get_ports QSFP0_REFCLK_RESET]; # Bank 64 VCCO - VCC1V2 Net "QSFP0_REFCLK_RESET"  - IO_L9P_T1L_N4_AD12P_64

#
# QSFP1 Clock Control Signals
#      - FS[1:0] <-- Clock Select Pin FS[1:0] = 1X -> 161.132812 MHz 1.8V LVDS (default when FPGA pin Hi-Z due to 10K pullups)
#                                     FS[1:0] = 01 -> 156.250000 MHz 1.8V LVDS
#      - RESET <-- Device Reset - Asserting this pin (driving high) is required to change FS1,FS0 pin setting. 
#                PINS: "QSFP1_RECLK_RESET"   - IO_L8N_T1L_N3_AD5N_64_AR21
#
set_property -dict {PACKAGE_PIN AR22 IOSTANDARD LVCMOS12       } [get_ports QSFP1_FS[0]       ]; # Bank 64 VCCO - VCC1V2 Net "QSFP1_FS0"           - IO_L8P_T1L_N2_AD5P_64
set_property -dict {PACKAGE_PIN AU20 IOSTANDARD LVCMOS12       } [get_ports QSFP1_FS[1]       ]; # Bank 64 VCCO - VCC1V2 Net "QSFP1_FS1"           - IO_L7N_T1L_N1_QBC_AD13N_64
set_property -dict {PACKAGE_PIN AR21 IOSTANDARD LVCMOS12       } [get_ports QSFP1_REFCLK_RESET]; # Bank 64 VCCO - VCC1V2 Net "QSFP1_REFCLK_RESET"  - IO_L8N_T1L_N3_AD5N_64
