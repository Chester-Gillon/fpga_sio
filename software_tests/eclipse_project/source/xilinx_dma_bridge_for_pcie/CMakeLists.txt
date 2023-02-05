# Build a library for using the Xilinx DMA Bridge for PCIe in use space using VFIO

project (xilinx_dma_bridge_for_pci C)

add_library (xilinx_dma_bridge_transfers "xilinx_dma_bridge_transfers.c")