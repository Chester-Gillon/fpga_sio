# Build the programs for performing SFP management, for designs which test transceivers

project (sfp_management C)

add_executable (sfp_management_menu "sfp_management_menu.c")
target_link_libraries (sfp_management_menu xilinx_axi_iic_transfers vfio_access)