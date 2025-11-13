# Build the library for accessing the Xilinx Card Management Solution Subsystem (CMS Subsystem)

project (xilinx_cms_subsystem C)

add_library (xilinx_cms "xilinx_cms.c")

add_executable (xilinx_xms_time_averaging "xilinx_cms_time_averaging.c")
target_link_libraries (xilinx_xms_time_averaging identify_pcie_fpga_design xilinx_cms vfio_access)
