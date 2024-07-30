# The IO standard and pull-up were taken from the following in an example project for the board:
# PCIE_5P/PCIE/pcie4_uscale_plus_0_ex/imports/xilinx_pcie4_uscale_plus_x0y0.xdc
set_false_path -from [get_ports PCI_PERSTN]
set_property PULLUP true [get_ports PCI_PERSTN]
set_property IOSTANDARD LVCMOS18 [get_ports PCI_PERSTN]
set_property PACKAGE_PIN T19 [get_ports {PCI_PERSTN}]

set_property PACKAGE_PIN V7 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
create_clock -name {CLK_PCIe_100MHz_clk_p[0]} -period 10.00 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
set_property PACKAGE_PIN P2 [get_ports {pcie_7x_mgt_rxp[0]}]
set_property PACKAGE_PIN T2 [get_ports {pcie_7x_mgt_rxp[1]}]
set_property PACKAGE_PIN V2 [get_ports {pcie_7x_mgt_rxp[2]}]
set_property PACKAGE_PIN Y2 [get_ports {pcie_7x_mgt_rxp[3]}]
set_property PACKAGE_PIN AB2 [get_ports {pcie_7x_mgt_rxp[4]}]
set_property PACKAGE_PIN AD2 [get_ports {pcie_7x_mgt_rxp[5]}]
set_property PACKAGE_PIN AE4 [get_ports {pcie_7x_mgt_rxp[6]}]
set_property PACKAGE_PIN AF2 [get_ports {pcie_7x_mgt_rxp[7]}]