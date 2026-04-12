# Build the programs for testing the Versal Multirate Ethernet MAC Subsystem (MRMAC)

project (mrmac_ethernet C)

add_library (mrmac_register_access "mrmac_register_access.c")

add_executable (mrmac_switch_test "mrmac_switch_test.c")
target_link_libraries (mrmac_switch_test identify_pcie_fpga_design xilinx_dma_bridge_transfers mrmac_register_access
                                         transfer_timing vfio_access m pthread)

add_executable (mrmac_configuration "mrmac_configuration.c")
target_link_libraries (mrmac_configuration mrmac_register_access identify_pcie_fpga_design transfer_timing vfio_access)

add_executable (mrmac_statistics "mrmac_statistics.c")
target_link_libraries (mrmac_statistics mrmac_register_access identify_pcie_fpga_design transfer_timing vfio_access)

add_executable (mrmac_loopback_test "mrmac_loopback_test.c")
target_link_libraries (mrmac_loopback_test mrmac_register_access identify_pcie_fpga_design xilinx_dma_bridge_transfers
                                           transfer_timing vfio_access)