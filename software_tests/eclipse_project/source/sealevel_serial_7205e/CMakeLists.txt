# Build test programs for a Sealevel COMM+2.LPCIe board (7205e) which use vfio
# This is an alternative to the tests in
# https://github.com/Chester-Gillon/plx_poll_mode_driver/tree/master/plx_poll_mode_driver which used a Plx
# kernel module and user space driver

project (sealevel_serial_7205e C)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (sealevel_serial_7205e_uart_tests "sealevel_serial_7205e_uart_tests.c" "pex8311.c")
    target_link_libraries (sealevel_serial_7205e_uart_tests vfio_access transfer_timing pci rt)
endif()

add_executable (time_pex8311_shared_memory_libpciaccess "time_pex8311_shared_memory_libpciaccess.c")
target_link_libraries (time_pex8311_shared_memory_libpciaccess vfio_access transfer_timing pciaccess pci rt)

add_executable (pex8311_enable_above_4GB_dma "pex8311_enable_above_4GB_dma.c")
target_link_libraries (pex8311_enable_above_4GB_dma pciaccess)