# Build a library for using the Xilinbx "AXI IIC Bus Interface" in user space using VFIO

project (xilinx_axi_iic C)

add_library (xilinx_axi_iic_transfers "xilinx_axi_iic_transfers.c")