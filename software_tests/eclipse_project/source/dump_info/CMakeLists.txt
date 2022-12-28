# Build the dump_info program

project (dump_info C)

add_executable (dump_info_libpciaccess "dump_info_libpciaccess.c")
target_link_libraries (dump_info_libpciaccess pciaccess)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (dump_info_pciutils "dump_info_pciutils.c")
    target_link_libraries (dump_info_pciutils pci)
endif()

add_executable (display_physical_slots "display_physical_slots.c")
target_link_libraries (display_physical_slots pci)

add_executable (display_vfio_information "display_vfio_information.c")