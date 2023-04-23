# Build the test programs which perform an I2C probe

project (i2c_probe C)

add_executable (i2c_probe "i2c_probe.c")
target_link_libraries (i2c_probe xilinx_axi_iic_transfers vfio_access pci rt)
