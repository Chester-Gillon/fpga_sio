# Build a library for using the Xilinx AXI4-Stream Switch using VFIO

project (xilinx_axi_stream_switch C)

add_library (xilinx_axi_stream_switch "xilinx_axi_stream_switch.c")

add_library (xilinx_axi_stream_switch_configure "xilinx_axi_stream_switch_configure.c")

add_executable (xilinx_axi_stream_switch_set_routing "xilinx_axi_stream_switch_set_routing.c")
target_link_libraries (xilinx_axi_stream_switch_set_routing identify_pcie_fpga_design
                       xilinx_axi_stream_switch_configure xilinx_axi_stream_switch vfio_access)