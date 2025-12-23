# Build the dump_info programs

project (dump_info C)

add_library (pciutils_pci_access "pciutils_pci_access.c")
add_library (libpciaccess_pci_access "libpciaccess_pci_access.c")
add_library (vfio_pci_access "vfio_pci_access.c")

add_library (dump_pci_info OBJECT "dump_pci_info.c")

add_executable (dump_pci_info_pciutils $<TARGET_OBJECTS:dump_pci_info>)
target_link_libraries (dump_pci_info_pciutils pciutils_pci_access pci_sysfs_access pci cap)

add_executable (dump_pci_info_libpciaccess $<TARGET_OBJECTS:dump_pci_info>)
target_link_libraries (dump_pci_info_libpciaccess libpciaccess_pci_access pci_sysfs_access pciaccess cap)

add_executable (dump_pci_info_vfio $<TARGET_OBJECTS:dump_pci_info>)
target_link_libraries (dump_pci_info_vfio vfio_pci_access vfio_access)

add_library (pcie_set_speed OBJECT "pcie_set_speed.c")

add_executable (pcie_set_speed_pciutils $<TARGET_OBJECTS:pcie_set_speed>)
target_link_libraries (pcie_set_speed_pciutils pciutils_pci_access pci_sysfs_access pci cap)

add_executable (pcie_set_speed_libpciaccess $<TARGET_OBJECTS:pcie_set_speed>)
target_link_libraries (pcie_set_speed_libpciaccess libpciaccess_pci_access pci_sysfs_access pciaccess cap)

add_executable (pcie_set_speed_vfio $<TARGET_OBJECTS:pcie_set_speed>)
target_link_libraries (pcie_set_speed_vfio vfio_pci_access vfio_access)

add_library (display_physical_slots OBJECT "display_physical_slots.c")

add_executable (display_physical_slots_pciutils $<TARGET_OBJECTS:display_physical_slots>)
target_link_libraries (display_physical_slots_pciutils pciutils_pci_access pci_sysfs_access pci cap)

add_executable (display_physical_slots_libpciaccess $<TARGET_OBJECTS:display_physical_slots>)
target_link_libraries (display_physical_slots_libpciaccess libpciaccess_pci_access pci_sysfs_access pciaccess cap)

add_executable (display_physical_slots_vfio $<TARGET_OBJECTS:display_physical_slots>)
target_link_libraries (display_physical_slots_vfio vfio_pci_access vfio_access)

add_executable (display_vfio_information "display_vfio_information.c")