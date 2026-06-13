# The IO standard was taken from the XCKU5P-FFVB676.pdf schematic which shows PCIe_RST
# from the edge connector is directly connected to a pin on bank 86 which is set to 3.3V.
# Haven't selected a pull-up, since https://adaptivesupport.amd.com/s/article/40279?language=en_US
# suggests the direction connection could cause a pull-up to violate the PCIe leakage current for this reset pin.
set_false_path -from [get_ports PCI_PERSTN]
set_property IOSTANDARD LVCMOS33 [get_ports PCI_PERSTN]
set_property PACKAGE_PIN K9 [get_ports {PCI_PERSTN}]

set_property PACKAGE_PIN P7 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
create_clock -name {CLK_PCIe_100MHz_clk_p[0]} -period 10.00 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
set_property PACKAGE_PIN F2 [get_ports {pcie_7x_mgt_rxp[0]}]; # MGT226_RX3_P 
set_property PACKAGE_PIN H2 [get_ports {pcie_7x_mgt_rxp[1]}]; # MGT226_RX2_P
set_property PACKAGE_PIN K2 [get_ports {pcie_7x_mgt_rxp[2]}]; # MGT226_RX1_P
set_property PACKAGE_PIN M2 [get_ports {pcie_7x_mgt_rxp[3]}]; # MGT226_RX0_P
set_property PACKAGE_PIN P2 [get_ports {pcie_7x_mgt_rxp[4]}]; # MGT225_RX3_P
set_property PACKAGE_PIN T2 [get_ports {pcie_7x_mgt_rxp[5]}]; # MGT225_RX2_P
set_property PACKAGE_PIN V2 [get_ports {pcie_7x_mgt_rxp[6]}]; # MGT225_RX1_P
set_property PACKAGE_PIN Y2 [get_ports {pcie_7x_mgt_rxp[7]}]; # MGT225_RX0_P