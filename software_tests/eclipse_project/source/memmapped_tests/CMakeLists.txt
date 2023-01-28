# Build the memory mapped test programs

project (memmapped_tests C)

add_executable (memmapped_persistence_libpciaccess "memmapped_persistence_libpciaccess.c")
target_link_libraries (memmapped_persistence_libpciaccess pciaccess rt)


if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (memmapped_persistence_vfio "memmapped_persistence_vfio.c")
    target_link_libraries (memmapped_persistence_vfio vfio_access pci)
endif()
