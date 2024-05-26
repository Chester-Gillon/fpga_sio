# Build test programs for a Micro Memory MM-5425CN NVRAM which use VFIO.
# This is an alternative to the test in https://github.com/Chester-Gillon/nvram_uio which used a Kernel module
# and user space driver

project (micromemory_5425_nvram C)

add_library (nvram_utils "nvram_utils.c")

add_executable (test_nvram_csr_access "test_nvram_csr_access.c")
target_link_libraries (test_nvram_csr_access nvram_utils vfio_access)

add_executable (time_nvram_access_vfio "time_nvram_access_vfio.c")
target_link_libraries (time_nvram_access_vfio nvram_utils vfio_access transfer_timing)

add_executable (time_nvram_access_libpciaccess "time_nvram_access_libpciaccess.c")
target_link_libraries (time_nvram_access_libpciaccess pciaccess transfer_timing nvram_utils vfio_access)
