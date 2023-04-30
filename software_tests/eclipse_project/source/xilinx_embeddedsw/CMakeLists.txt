# Build a sub-set of the Xilinx embedded software from https://github.com/Xilinx/embeddedsw for use on a Linux x86_64 host

project (xilinx_embeddedsw C)

# Remove the -Wconversion option as generates multiple warnings in the Xilinx source code
string (REPLACE "-Wconversion" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# The low-level IIC driver is poll-mode only and doesn't have any dependencies on any library functions except usleep(),
# which is provided by Linux.
add_library (xiic_l "${XILINX_EMBEDDEDSW_ROOT}/XilinxProcessorIPLib/drivers/iic/src/xiic_l.c")
