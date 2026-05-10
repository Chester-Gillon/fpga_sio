# The IO standard and pad was taken from https://github.com/TiferKing/as02mc04_hack/blob/main/as02mc04/1.0/part0_pins.xml
# Enabled a pull-up since done for a different design.
set_false_path -from [get_ports PCI_PERSTN]
set_property PULLTYPE PULLUP [get_ports PCI_PERSTN]
set_property IOSTANDARD LVCMOS18 [get_ports PCI_PERSTN]
set_property PACKAGE_PIN A9 [get_ports PCI_PERSTN]

# Pins taken from https://github.com/TiferKing/as02mc04_hack/blob/main/as02mc04/1.0/part0_pins.xml
# and only use x4 width on the quad224 with a reversed lane order.
set_property PACKAGE_PIN T7 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
create_clock -period 10.000 -name {CLK_PCIe_100MHz_clk_p[0]} [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
set_property LOC GTYE4_CHANNEL_X0Y3 [get_cells {AS02MC04_enum_i/xdma_0/inst/pcie4_ip_i/inst/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.AS02MC04_enum_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[3].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[0].GTYE4_CHANNEL_PRIM_INST}]
set_property PACKAGE_PIN AB2 [get_ports {pcie_7x_mgt_rxp[3]}]
set_property LOC GTYE4_CHANNEL_X0Y2 [get_cells {AS02MC04_enum_i/xdma_0/inst/pcie4_ip_i/inst/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.AS02MC04_enum_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[3].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[1].GTYE4_CHANNEL_PRIM_INST}]
set_property PACKAGE_PIN AD2 [get_ports {pcie_7x_mgt_rxp[2]}]
set_property LOC GTYE4_CHANNEL_X0Y1 [get_cells {AS02MC04_enum_i/xdma_0/inst/pcie4_ip_i/inst/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.AS02MC04_enum_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[3].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[2].GTYE4_CHANNEL_PRIM_INST}]
set_property PACKAGE_PIN AE4 [get_ports {pcie_7x_mgt_rxp[1]}]
set_property LOC GTYE4_CHANNEL_X0Y0 [get_cells {AS02MC04_enum_i/xdma_0/inst/pcie4_ip_i/inst/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/gt_wizard.gtwizard_top_i/AS02MC04_enum_xdma_0_0_pcie4_ip_gt_i/inst/gen_gtwizard_gtye4_top.AS02MC04_enum_xdma_0_0_pcie4_ip_gt_gtwizard_gtye4_inst/gen_gtwizard_gtye4.gen_channel_container[3].gen_enabled_channel.gtye4_channel_wrapper_inst/channel_inst/gtye4_channel_gen.gen_gtye4_channel_inst[3].GTYE4_CHANNEL_PRIM_INST}]
set_property PACKAGE_PIN AF2 [get_ports {pcie_7x_mgt_rxp[0]}]

