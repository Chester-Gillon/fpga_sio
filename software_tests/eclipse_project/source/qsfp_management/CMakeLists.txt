# Build the programs for performing QSFP management, for designs which test transceivers

project (qsfp_management C)

add_executable (direct_iic_qsfp_management_menu "direct_iic_qsfp_management_menu.c")
target_link_libraries (direct_iic_qsfp_management_menu xilinx_axi_iic_transfers vfio_access)

add_executable (cms_qsfp_management_menu "cms_qsfp_management_menu.c")
target_link_libraries (cms_qsfp_management_menu identify_pcie_fpga_design xilinx_cms vfio_access)