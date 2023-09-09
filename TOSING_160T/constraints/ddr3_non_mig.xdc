# Additional constaints for DDR3 interface over ddr3.ucf which was saved from the MIG project creation.

# This is single-ended 50M clock from an unspecified 50MHz oscillator with a 3.3V supply.
# The pin is a Single-region clock-capable I/O (SRCC), and fed to a Clock Wizard to generate
# the clocks for the Memory Interface Generator. 
set_property PACKAGE_PIN G17 [get_ports {CLK_50M}]
set_property IOSTANDARD LVCMOS33 [get_ports {CLK_50M}]

# Banks 32 and 33 are used for the DDR3 interface, but only bank32 has the VRN/VRP resistors
# so need to use DCI_CASCADE (which is also enabled in the MIG project file)
set_property DCI_CASCADE {33} [get_iobanks 32]