# Connected directly to the PCIe connector. Has a pull-up to 3.3V
set_property PACKAGE_PIN M17 [get_ports {PCI_PERSTN}]
set_property IOSTANDARD LVCMOS33 [get_ports {PCI_PERSTN}]

# Connected to the PCIe connector via AC coupling 0.1uF capacitors.
# The PCIe clock is shared with an on-board 150 MHz oscillator. The on-board dip switch
# must be OFF to allow the PCIe clock to be used (if the dip switch is ON the clocks would
# fight each other).
set_property PACKAGE_PIN F6 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
create_clock -name {CLK_PCIe_100MHz_clk_p[0]} -period 10.00 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
set_property PACKAGE_PIN B6 [get_ports {pcie_7x_mgt_rxp[0]}]
set_property PACKAGE_PIN C4 [get_ports {pcie_7x_mgt_rxp[1]}]
set_property PACKAGE_PIN E4 [get_ports {pcie_7x_mgt_rxp[2]}]
set_property PACKAGE_PIN G4 [get_ports {pcie_7x_mgt_rxp[3]}]
