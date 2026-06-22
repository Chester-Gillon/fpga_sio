This is a modified version of U200_dma_ddr4 created by:
1. Copying the U200_dma_ddr4/create_project.tcl file and manually editing the project prefix to U200_dma_ddr4_ch0_1
2. Removing DDR4 channels 2 and 3. Regenerated the AXI Smart Connect blocks to allow for reduced ports.
3. Changing the PCIe revision to 0x01, to identify the different amount of memory.
4. Tying the DDR4 reset monitoring GPIO signals for the removed channels to a reset deasserted / calibration complete state.
   On testing realised had missed a connection and the init_calib_complete GPIO input always reads back as zero.

The end result is 32GB of DDR4.

The implementation met timing.

