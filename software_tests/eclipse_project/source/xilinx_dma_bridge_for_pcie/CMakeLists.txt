# Build a library for using the Xilinx DMA Bridge for PCIe in user space using VFIO

project (xilinx_dma_bridge_for_pci C)

add_library (xilinx_dma_bridge_transfers "xilinx_dma_bridge_transfers.c")

add_executable (test_dma_bridge "test_dma_bridge.c")
target_link_libraries (test_dma_bridge xilinx_dma_bridge_transfers identify_pcie_fpga_design
                       xilinx_axi_stream_switch_configure xilinx_axi_stream_switch
                       transfer_timing vfio_access)

add_executable (test_dma_descriptor_credits "test_dma_descriptor_credits.c")
target_link_libraries (test_dma_descriptor_credits identify_pcie_fpga_design
                       xilinx_axi_stream_switch_configure xilinx_axi_stream_switch
                       vfio_access transfer_timing crcmodel)

add_executable (ddr3_reset_control "ddr3_reset_control.c")
target_link_libraries (ddr3_reset_control identify_pcie_fpga_design vfio_access transfer_timing)

add_executable (test_dma_bridge_parallel_streams "test_dma_bridge_parallel_streams.c")
target_link_libraries (test_dma_bridge_parallel_streams xilinx_dma_bridge_transfers transfer_timing
                       xilinx_axi_stream_switch_configure xilinx_axi_stream_switch
                       identify_pcie_fpga_design vfio_access pthread)

add_executable (test_dma_bridge_independent_streams "test_dma_bridge_independent_streams.c")
target_link_libraries (test_dma_bridge_independent_streams xilinx_dma_bridge_transfers transfer_timing
                       xilinx_axi_stream_switch_configure xilinx_axi_stream_switch
                       identify_pcie_fpga_design vfio_access pthread)