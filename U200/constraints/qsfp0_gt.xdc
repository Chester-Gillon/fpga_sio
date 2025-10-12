#
# Input Clocks and Controls for QSFP28 Port 0
#
# MGT_SI570_CLOCK0   -> MGT Ref Clock 0 156.25MHz Default (User re-programmable)
# QSFP0_CLOCK        -> MGT Ref Clock 1 User selectable by QSFP0_FS
#
set_property PACKAGE_PIN M10 [get_ports MGT_SI570_CLOCK0_clk_n]; # Bank 231 Net "MGT_SI570_CLOCK0_C_N" - MGTREFCLK0N_231
set_property PACKAGE_PIN M11 [get_ports MGT_SI570_CLOCK0_clk_p]; # Bank 231 Net "MGT_SI570_CLOCK0_C_P" - MGTREFCLK0P_231

#
# MGT Connections
#
set_property PACKAGE_PIN N9  [get_ports {QSFP0_GT_gtx_p[0]}]; # Bank 231  - MGTYTXP0_231
set_property PACKAGE_PIN M7  [get_ports {QSFP0_GT_gtx_p[1]}]; # Bank 231  - MGTYTXP1_231
set_property PACKAGE_PIN L9  [get_ports {QSFP0_GT_gtx_p[2]}]; # Bank 231  - MGTYTXP2_231
set_property PACKAGE_PIN K7  [get_ports {QSFP0_GT_gtx_p[3]}]; # Bank 231  - MGTYTXP3_231
