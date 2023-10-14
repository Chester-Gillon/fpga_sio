###############################################################################
# DDR
###############################################################################
# Note: Most of the pins are set in the constraints file created by MIG
set_property IOSTANDARD LVDS_25 [get_ports CLK_DDR3_200MHz_clk_p]
set_property IOSTANDARD LVDS_25 [get_ports CLK_DDR3_200MHz_clk_n]