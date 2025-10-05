# Created from the Xilinx provided alveo-u250-xdc.xdc, since that had the MGT connections
# as commented out PACKAGE_PINs for some reasoon.

# Input Clocks for Gen3 x16
# PCIE_REFCLK -> PCIe 100Mhz Host clock
set_property PACKAGE_PIN AM10             [get_ports PCIE_REFCLK_clk_n]; # Bank 226 Net "PEX_REFCLK_C_N" - MGTREFCLK0N_226
set_property PACKAGE_PIN AM11             [get_ports PCIE_REFCLK_clk_p]; # Bank 226 Net "PEX_REFCLK_C_P" - MGTREFCLK0P_226
create_clock -name {PCIE_REFCLK_clk_p} -period 10.00 [get_ports {PCIE_REFCLK_clk_p}]

#
#  PCIe Connections   Bank 64
#    PCIE_PERSTN Active low input from PCIe Connector to Ultrascale+ Device to detect presence.
#
set_property -dict {PACKAGE_PIN BD21 IOSTANDARD LVCMOS12       } [get_ports PCIE_PERST        ]; # Bank 64 VCCO - VCC1V2 Net "PCIE_PERST_LS"       - IO_L23P_T3U_N8_64

# This is the upper 8 lanes of the PCIe connector, for investigating how to create a bifurcated design
set_property PACKAGE_PIN AF2 [get_ports {pcie_7x_mgt_rxp[0]} ]; # Bank 227  - MGTYRXP3_227
set_property PACKAGE_PIN AG4 [get_ports {pcie_7x_mgt_rxp[1]} ]; # Bank 227  - MGTYRXP2_227
set_property PACKAGE_PIN AH2 [get_ports {pcie_7x_mgt_rxp[2]} ]; # Bank 227  - MGTYRXP1_227
set_property PACKAGE_PIN AJ4 [get_ports {pcie_7x_mgt_rxp[3]} ]; # Bank 227  - MGTYRXP0_227
set_property PACKAGE_PIN AK2 [get_ports {pcie_7x_mgt_rxp[4]} ]; # Bank 226  - MGTYRXP3_226
set_property PACKAGE_PIN AL4 [get_ports {pcie_7x_mgt_rxp[5]} ]; # Bank 226  - MGTYRXP2_226
set_property PACKAGE_PIN AM2 [get_ports {pcie_7x_mgt_rxp[6]} ]; # Bank 226  - MGTYRXP1_226
set_property PACKAGE_PIN AN4 [get_ports {pcie_7x_mgt_rxp[7]} ]; # Bank 226  - MGTYRXP0_226
