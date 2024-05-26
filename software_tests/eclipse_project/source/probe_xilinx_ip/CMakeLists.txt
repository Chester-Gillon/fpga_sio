# Build the program which probes for Xilinx IP

project (probe_xilinx_ip C)

add_executable (probe_xilinx_ip "probe_xilinx_ip.c")
target_link_libraries (probe_xilinx_ip vfio_access)