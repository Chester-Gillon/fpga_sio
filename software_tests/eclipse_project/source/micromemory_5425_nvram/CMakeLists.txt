# Build test programs for a Micro Memory MM-5425CN NVRAM which use VFIO.
# This is an alternative to the test in https://github.com/Chester-Gillon/nvram_uio which used a Kernel module
# and user space driver

project (micromemory_5425_nvram C)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (test_nvram_csr_access "test_nvram_csr_access.c")
    target_link_libraries (test_nvram_csr_access vfio_access pci rt)
endif()