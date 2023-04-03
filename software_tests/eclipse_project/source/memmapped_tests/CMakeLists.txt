# Build the memory mapped test programs

project (memmapped_tests C)

add_executable (memmapped_persistence_libpciaccess "memmapped_persistence_libpciaccess.c")
target_link_libraries (memmapped_persistence_libpciaccess pciaccess rt)

add_executable (time_memmapped_libpciaccess "time_memmapped_libpciaccess.c")
target_link_libraries (time_memmapped_libpciaccess vfio_access transfer_timing pciaccess pci rt)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (memmapped_persistence_vfio "memmapped_persistence_vfio.c")
    target_link_libraries (memmapped_persistence_vfio vfio_access pci rt)

    add_executable (time_dma_blkram "time_dma_blkram.c")
    target_link_libraries (time_dma_blkram transfer_timing xilinx_dma_bridge_transfers vfio_access pci rt)
endif()
