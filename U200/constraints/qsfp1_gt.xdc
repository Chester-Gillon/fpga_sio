#
# Input Clocks and Controls for QSFP28 Port 1
#
# MGT_SI570_CLOCK1   -> MGT Ref Clock 0 156.25MHz Default (User re-programmable)
# QSFP1_CLOCK        -> MGT Ref Clock 1 User selectable by QSFP0_FS
#
set_property PACKAGE_PIN T10 [get_ports MGT_SI570_CLOCK1_clk_n]; # Bank 230 Net "MGT_SI570_CLOCK1_C_N" - MGTREFCLK0N_230
set_property PACKAGE_PIN T11 [get_ports MGT_SI570_CLOCK1_clk_p]; # Bank 230 Net "MGT_SI570_CLOCK1_C_P" - MGTREFCLK0P_230

#
# MGT Connections
#
set_property PACKAGE_PIN U9  [get_ports {QSFP1_GT_gtx_p[0]}]; # Bank 230  - MGTYTXP0_230
set_property PACKAGE_PIN T7  [get_ports {QSFP1_GT_gtx_p[1]}]; # Bank 230  - MGTYTXP1_230
set_property PACKAGE_PIN R9  [get_ports {QSFP1_GT_gtx_p[2]}]; # Bank 230  - MGTYTXP2_230
set_property PACKAGE_PIN P7  [get_ports {QSFP1_GT_gtx_p[3]}]; # Bank 230  - MGTYTXP3_230

