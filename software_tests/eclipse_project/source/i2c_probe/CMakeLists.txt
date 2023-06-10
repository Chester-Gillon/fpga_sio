# Build the test programs which perform an I2C probe

project (i2c_probe C)

add_library (i2c_bit_banged "i2c_bit_banged.c")
add_library (pmbus_access "pmbus_access.c")
add_library (ltm4676a_access "ltm4676a_access.c")

add_executable (i2c_probe "i2c_probe.c")
if (${HAVE_XILINX_EMBEDDEDSW})
    target_link_libraries (i2c_probe xiic_l)
endif()
target_link_libraries (i2c_probe i2c_bit_banged xilinx_axi_iic_transfers vfio_access pci rt)

add_executable (i2c_dump_info "i2c_dump_info.c")
target_link_libraries (i2c_dump_info ltm4676a_access pmbus_access i2c_bit_banged vfio_access pci rt m)

add_executable (tef1001_fan_control "tef1001_fan_control.c")
target_link_libraries (tef1001_fan_control i2c_bit_banged vfio_access pci rt)