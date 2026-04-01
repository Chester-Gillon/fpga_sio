# Build the programs for testing the Versal Multirate Ethernet MAC Subsystem (MRMAC)

project (mrmac_ethernet C)

add_library (mrmac_register_access "mrmac_register_access.c")

add_executable (mrmac_switch_test "mrmac_switch_test.c")
target_link_libraries (mrmac_switch_test identify_pcie_fpga_design xilinx_dma_bridge_transfers transfer_timing vfio_access m)

add_executable (mrmac_configuration "mrmac_configuration.c")
target_link_libraries (mrmac_configuration mrmac_register_access identify_pcie_fpga_design vfio_access)