# Build a library for using the Xilinx Analog-to-Digital Converter (XADC) in user space using VFIO

add_library (xilinx_xadc "xilinx_xadc.c")

add_executable (display_xadc_values "display_xadc_values.c")
target_link_libraries (display_xadc_values identify_pcie_fpga_design xilinx_xadc vfio_access pci rt)