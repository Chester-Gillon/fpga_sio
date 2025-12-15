set_property PACKAGE_PIN P2 [get_ports {pcie_mgt_grx_p[0]}]
set_property PACKAGE_PIN M2 [get_ports {pcie_mgt_grx_p[1]}]
set_property PACKAGE_PIN K2 [get_ports {pcie_mgt_grx_p[2]}]
set_property PACKAGE_PIN H2 [get_ports {pcie_mgt_grx_p[3]}]

set_property PACKAGE_PIN B28 [get_ports PCIE_PERST]
set_property IOSTANDARD LVCMOS12 [get_ports PCIE_PERST]

set_property PACKAGE_PIN M7 [get_ports {pcie_refclk_clk_p[0]}]
create_clock -name {pcie_refclk_clk_p[0]} -period 10.00 [get_ports {pcie_refclk_clk_p[0]}]

