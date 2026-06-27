# Connected directly to the PCIe connector
set_property PACKAGE_PIN A9 [get_ports {PCI_PERSTN}]
set_property IOSTANDARD LVCMOS33 [get_ports {PCI_PERSTN}]

set_property PACKAGE_PIN D6 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
create_clock -name {CLK_PCIe_100MHz_clk_p[0]} -period 10.00 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
set_property PACKAGE_PIN B6 [get_ports {pcie_7x_mgt_rxp[0]}]