###############################################################################
# PCIe x4
###############################################################################

# Remove default placement first (otherwise the default LOC is applied)
#
# @todo This is necessary to get the correct PCIe pin placement when compiling under Linux, but worked without under Windows.
#       A side effect is 4 of the following Critical Warnings:
#         [Constraints 18-4427] You are overriding a physical property set by a constraint that originated in a read only source
#
#       Where the overriden physical properties are in NiteFury_dma_ddr3/NiteFury_dma_ddr3.gen/sources_1/bd/NiteFury_dma_ddr3/ip/NiteFury_dma_ddr3_xdma_0_0/ip_0/source/NiteFury_dma_ddr3_xdma_0_0_pcie2_ip-PCIE_X0Y0.xdc 
#       which is a generated IP constraint file.
set_property LOC "" [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
set_property LOC "" [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[1].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
set_property LOC "" [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[2].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
set_property LOC "" [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[3].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]

# PCIe lane 0
set_property LOC GTPE2_CHANNEL_X0Y7 [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[3].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]

# PCIe lane 1
set_property LOC GTPE2_CHANNEL_X0Y6 [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]

# PCIe lane 2
set_property LOC GTPE2_CHANNEL_X0Y5 [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[2].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]

# PCIe lane 3
set_property LOC GTPE2_CHANNEL_X0Y4 [get_cells {NiteFury_dma_ddr3_i/xdma_0/inst/NiteFury_dma_ddr3_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[1].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]

# PCIe refclock
set_property PACKAGE_PIN F6 [get_ports {CLK_PCIe_100MHz_clk_p[0]}]
set_property PACKAGE_PIN E6 [get_ports {CLK_PCIe_100MHz_clk_n[0]}]

# Other PCIe signals
set_property PACKAGE_PIN G1 [get_ports {pcie_clkreq_l}]
set_property IOSTANDARD LVCMOS33 [get_ports {pcie_clkreq_l}]
set_property PACKAGE_PIN J1 [get_ports PCI_PERSTN]
set_property IOSTANDARD LVCMOS33 [get_ports PCI_PERSTN]
