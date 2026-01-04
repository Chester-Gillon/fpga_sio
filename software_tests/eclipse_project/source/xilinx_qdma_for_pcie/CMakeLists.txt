# Build a library for using the Xilinx QMDA Subsystem for PCIe in user space using VFIO

project (xilinx_qdma_for_pci C)

add_library (qdma_transfers "qdma_transfers.c")