# 100 MHz differential clock
create_clock -name {diff_100mhz_clk_p} -period 10.00 [get_ports {diff_100mhz_clk_p}]
set_property IOSTANDARD LVDS [get_ports {diff_100mhz_clk_p}]
set_property PACKAGE_PIN E18 [get_ports {diff_100mhz_clk_p}]
set_property PACKAGE_PIN D18 [get_ports {diff_100mhz_clk_n}]
 