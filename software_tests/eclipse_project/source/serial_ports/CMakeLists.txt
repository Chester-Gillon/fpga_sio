# Build programs for VFIO access to serial ports

project (serial_ports C)

add_executable (identify_uarts "identify_uarts.c")
target_link_libraries (identify_uarts vfio_access)