# Defines the pins used for the SFP module management.
# I.e. the slow speed signals rather than the SERDES signals.

set_property PACKAGE_PIN D26 [get_ports {SFP1_TX_EN}]
set_property IOSTANDARD LVCMOS15 [get_ports {SFP1_TX_EN}] 

set_property PACKAGE_PIN D25 [get_ports {SFP2_TX_EN}]
set_property IOSTANDARD LVCMOS15 [get_ports {SFP2_TX_EN}]

set_property PACKAGE_PIN D21 [get_ports {SFP1_I2C_scl_io}]
set_property IOSTANDARD LVCMOS15 [get_ports {SFP1_I2C_scl_io}]

set_property PACKAGE_PIN D20 [get_ports {SFP1_I2C_sda_io}]
set_property IOSTANDARD LVCMOS15 [get_ports {SFP1_I2C_sda_io}]