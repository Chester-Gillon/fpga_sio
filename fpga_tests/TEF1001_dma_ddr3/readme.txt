Some limitations of the DDR3 memory are:
1. While the DDR3 SODIMM fitted is a Micron 18KSF1G72HZ-1G6P1 which has ECC, have selected MT16KTF1G64HZ-1G6 as the
   SODIMM which deoesn't have ECC. This is because the MIG won't generate byte selects when ECC is used.
   TBD about:
   a. If the AXI interface can generate read-modify-write cycles which need the Data Masks.
   b. If can use the FGPA to connect the Data Masks pins, which the MIG doesn't use, to a zero or if need an external temination resistor.
      "42783 - MIG DDR2/DDR3 - Termination for Data Mask (DM) Signal when DM is disabled"
      https://support.xilinx.com/s/article/42783?language=en_US contains:
         However, if the DM signal is disabled, all DM signals should be pulled to ground at the memory with a resistor value set defined
         by the memory vendor.

2. Have had to reduce the memory speed grade from DRR3-1066 (max of the Kintex-7 device used) to DDR3-800.
   This is because when a dual rank memory is used, the MIG IP doesn't allow the highest speed grade supported by the FPGA to be used.
   "61572 - MIG 7 Series - DDR3 - Maximum PHY limits for TwinDie components" https://support.xilinx.com/s/article/61572?language=en_US
   contains:
      For 7 Series MIG DDR3 design, the Twin die (dual rank) component maximum speed supported is one memory speed grade lower than 
      dual rank DIMM.

   The above Xilinx support article describes how to modify the top level RTL parameters to remove this limitation, 
   but the IP then needs to be changed to be unmanaged.

