# Build a library for using the Xilinx DMA Bridge for PCIe in user space using VFIO

project (xilinx_dma_bridge_for_pci C)

add_library (xilinx_dma_bridge_transfers "xilinx_dma_bridge_transfers.c")

add_executable (test_dma_accessible_memory "test_dma_accessible_memory.c")
target_link_libraries (test_dma_accessible_memory xilinx_dma_bridge_transfers identify_pcie_fpga_design 
                                                  vfio_access transfer_timing pci rt)

add_executable (test_dma_descriptor_credits "test_dma_descriptor_credits.c")
target_link_libraries (test_dma_descriptor_credits identify_pcie_fpga_design vfio_access transfer_timing pci rt)