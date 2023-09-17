# Build test programs for the NiteFury or LiteFury sample FPGA project from
# https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/tree/master/Sample-Projects/Project-0/FPGA

project (nite_or_lite_fury_tests C)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (test_primary "test_primary.c")
    target_link_libraries (test_primary identify_pcie_fpga_design vfio_access pci rt)

    add_executable (flash_m2_led "flash_m2_led.c")
    target_link_libraries (flash_m2_led identify_pcie_fpga_design vfio_access pci rt)
endif()