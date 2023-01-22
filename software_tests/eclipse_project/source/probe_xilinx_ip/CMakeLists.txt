# Build the program which probes for Xilinx IP

project (probe_xilinx_ip C)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (probe_xilinx_ip "probe_xilinx_ip.c")
    target_link_libraries (probe_xilinx_ip pci)
endif()