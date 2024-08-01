# Build a library for using the Xilinx Analog-to-Digital Converter (XADC) or SYSMON in user space using VFIO

add_library (xilinx_xadc "xilinx_xadc.c")
add_library (xilinx_sysmon "xilinx_sysmon.c")

add_executable (display_sensor_values "display_sensor_values.c")
target_link_libraries (display_sensor_values identify_pcie_fpga_design xilinx_xadc xilinx_sysmon vfio_access)