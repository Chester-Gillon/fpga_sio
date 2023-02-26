# Build the test programs which perform an I2C probe

project (sealevel_serial_7205e C)

if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_executable (i2c_probe "i2c_probe.c")
    target_link_libraries (i2c_probe vfio_access pci rt)
endif()
