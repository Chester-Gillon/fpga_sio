# Build the dump_info programs

project (dump_info C)

add_library (pciutils_pci_access "pciutils_pci_access.c")
add_library (libpciaccess_pci_access "libpciaccess_pci_access.c")
add_library (vfio_pci_access "vfio_pci_access.c")

add_library (dump_pci_info OBJECT "dump_pci_info.c")

add_executable (dump_pci_info_pciutils $<TARGET_OBJECTS:dump_pci_info>)
target_link_libraries (dump_pci_info_pciutils pciutils_pci_access pci_sysfs_access pci)

add_executable (dump_pci_info_libpciaccess $<TARGET_OBJECTS:dump_pci_info>)
target_link_libraries (dump_pci_info_libpciaccess libpciaccess_pci_access pci_sysfs_access pciaccess)

add_executable (dump_pci_info_vfio $<TARGET_OBJECTS:dump_pci_info>)
target_link_libraries (dump_pci_info_vfio vfio_pci_access vfio_access)

add_executable (display_physical_slots "display_physical_slots.c")
target_link_libraries (display_physical_slots pci)

add_executable (display_vfio_information "display_vfio_information.c")