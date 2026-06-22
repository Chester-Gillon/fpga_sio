This is a modified version of U200_dma_ddr4 created by:
1. Copying the U200_dma_ddr4/create_project.tcl file and manually editing the project prefix to U200_dma_ddr4_ch2_3
2. Removing DDR4 channels 0 and 1. Regenerated the AXI Smart Connect blocks to allow for reduced ports.
3. Changing the PCIe revision to 0x02, to identify the different amount of memory.
4. Tying the DDR4 reset monitoring GPIO signals for the removed channels to a reset deasserted / calibration complete state.

The end result is 32GB of DDR4. Kept the same address offsets for DDR4 channels 2 and 3 as in U200_dma_ddr4.
I.e. the DDR4 memory base offset is 0x000800000000.

The implementation met timing.

