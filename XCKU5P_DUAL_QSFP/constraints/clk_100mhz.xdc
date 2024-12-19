# 100 MHZ single ended input clock
set_property IOSTANDARD LVCMOS18 [get_ports CLK_100MHz]
set_property PACKAGE_PIN K22 [get_ports {CLK_100MHz}]
create_clock -name {CLK_100MHz} -period 10.00 [get_ports {CLK_100MHz}]

