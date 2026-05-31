# The pads was taken from https://github.com/TiferKing/as02mc04_hack/blob/main/as02mc04/1.0/part0_pins.xml
# That file had the IOSTANDARD for the LEDs as LVCMOS18, but SYSMON showed BANK86 and BANK87 have a VCCO
# of 3.3V, so changed to LVCMOS33.
set_property PACKAGE_PIN B11 [get_ports {LED[0]}]
set_property PACKAGE_PIN C11 [get_ports {LED[1]}]
set_property PACKAGE_PIN A10 [get_ports {LED[2]}]
set_property PACKAGE_PIN B10 [get_ports {LED[3]}]
set_property PACKAGE_PIN A12 [get_ports {LED_G[0]}]
set_property PACKAGE_PIN A13 [get_ports {LED_R[0]}]
set_property PACKAGE_PIN B9 [get_ports LED_HEART]

set_property IOSTANDARD LVCMOS33 [get_ports {LED[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {LED[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {LED[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {LED[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {LED_G[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports LED_HEART]
set_property IOSTANDARD LVCMOS33 [get_ports {LED_R[0]}]


