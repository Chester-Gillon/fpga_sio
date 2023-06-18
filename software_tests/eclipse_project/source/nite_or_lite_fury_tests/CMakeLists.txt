# Build test programs for the NiteFury or LiteFury sample FPGA project from
# https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/tree/master/Sample-Projects/Project-0/FPGA
#
# Also builds time_dma_blkram as that has the same DMA controller as the NiteFury or LiteFury sample FPGA projects

project (nite_or_lite_fury_tests C)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_library (fury_utils "fury_utils.c")

    add_executable (test_general "test_general.c")
    target_link_libraries (test_general fury_utils vfio_access pci rt)

    add_executable (test_primary "test_primary.c")
    target_link_libraries (test_primary fury_utils vfio_access pci rt)

    add_executable (test_ddr "test_ddr.c")
    target_link_libraries (test_ddr fury_utils transfer_timing xilinx_dma_bridge_transfers vfio_access pci rt)

    add_executable (time_dma_blkram "time_dma_blkram.c")
    target_link_libraries (time_dma_blkram transfer_timing xilinx_dma_bridge_transfers vfio_access pci rt)

    add_executable (flash_m2_led "flash_m2_led.c")
    target_link_libraries (flash_m2_led fury_utils vfio_access pci rt)
endif()