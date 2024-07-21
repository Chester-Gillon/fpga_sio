# Build libraries for performing device access using VFIO

project (vfio_access C)

if (HAVE_CMEM)
    # Build the library with support for using a physical memory allocator for DMA support for noiommu mode
    include_directories ("${CMEM_ROOT}/module")
    add_library (vfio_access "vfio_access.c"
                             "${CMEM_ROOT}/cmem_test/cmem_drv.c")
else()
    # Build the library without noiommu DMA support
    add_library (vfio_access "vfio_access.c")
endif()

add_library (transfer_timing "transfer_timing.c")

add_library (pci_sysfs_access "pci_sysfs_access.c")

# Set dependent libraries to reduce duplication in target_link_libraries() of the executables
target_link_libraries(vfio_access pci_sysfs_access pci rt)

add_executable (vfio_multi_process_manager "vfio_multi_process_manager.c")
target_link_libraries (vfio_multi_process_manager vfio_access)