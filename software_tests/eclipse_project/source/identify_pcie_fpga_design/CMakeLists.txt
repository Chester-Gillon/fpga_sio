# Build the library for identifying FPGA designs with a PCIe interface

project (identify_pcie_fpga_design C)

add_library (identify_pcie_fpga_design "identify_pcie_fpga_design.c")

add_executable (display_identified_pcie_fpga_designs "display_identified_pcie_fpga_designs.c")
target_link_libraries (display_identified_pcie_fpga_designs identify_pcie_fpga_design 
                       xilinx_dma_bridge_transfers transfer_timing xilinx_axi_stream_switch vfio_access)

add_executable (display_possible_fpga_designs "display_possible_fpga_designs.c")
target_link_libraries (display_possible_fpga_designs identify_pcie_fpga_design vfio_access)