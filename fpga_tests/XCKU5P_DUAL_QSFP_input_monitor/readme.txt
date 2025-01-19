The FPGAs in this directory were created to try and investigate why with the
XCKU5P_DUAL_QSFP_ibert_4.166 design, the I2C interface for QSFP port B wasn't able
to detect any module on the I2C bus.

https://gist.github.com/Chester-Gillon/ba675c6ab4e5eb7271f43f8ce4aedb6c#9-bank-voltages
found that banks 84, 86, and 87 have a VCC of 3.3V, which is the signal level used
for the QSFP I2C.

The FPGA designs in this directory set the bank 84, 86, and 87 pins to inputs,
with the inputs connected to a ILA with the maximum memory depth of 2^17.
The ILA clock is set to 100 MHz, so a 1.31 ms sample duration.

The difference between the designs is either:
a. No pull-up or pull-down enabled on the inputs, so unconnected inputs may float.
b. Pull-ups enabled on all inputs.
c. Pull-downs enanled on all inputs.

Measurements were taken with no QSFP modules connected.
The ILA indicates the inputs were constant during the sample period.
Performed two runs of each type of FPGA to check for consistency:
- Both pull_up and pull_down reported consistent results across both runs.
- Whereas no_pull had some bits with different values across both runs.

1. XCKU5P_DUAL_QSFP_input_monitor_no_pull_i/bank_84 ffffbf
   XCKU5P_DUAL_QSFP_input_monitor_no_pull_i/bank_86 5ffdff
   XCKU5P_DUAL_QSFP_input_monitor_no_pull_i/bank_87 7bffff

2. XCKU5P_DUAL_QSFP_input_monitor_pull_up_i/bank_84 ffffbf
   XCKU5P_DUAL_QSFP_input_monitor_pull_up_i/bank_86 dfffff
   XCKU5P_DUAL_QSFP_input_monitor_pull_up_i/bank_87 ffffff

3. XCKU5P_DUAL_QSFP_input_monitor_pull_down_i/bank_84 ffc000
   XCKU5P_DUAL_QSFP_input_monitor_pull_down_i/bank_86 000035
   XCKU5P_DUAL_QSFP_input_monitor_pull_down_i/bank_87 00009f

4. XCKU5P_DUAL_QSFP_input_monitor_no_pull_i/bank_84 ffe53a
   XCKU5P_DUAL_QSFP_input_monitor_no_pull_i/bank_86 482435
   XCKU5P_DUAL_QSFP_input_monitor_no_pull_i/bank_87 7083df

5. XCKU5P_DUAL_QSFP_input_monitor_pull_down_i/bank_84 ffc000
   XCKU5P_DUAL_QSFP_input_monitor_pull_down_i/bank_86 000035
   XCKU5P_DUAL_QSFP_input_monitor_pull_down_i/bank_87 00009f

6. XCKU5P_DUAL_QSFP_input_monitor_pull_up_i/bank_84 ffffbf
   XCKU5P_DUAL_QSFP_input_monitor_pull_up_i/bank_86 dfffff
   XCKU5P_DUAL_QSFP_input_monitor_pull_up_i/bank_87 ffffff

Analysis of inputs which are always in the same state for all FPGA images,
and therefore assumed to external driven.
Where possible, map to known signals in the XCKU5P_pcie_qsfp_or_sfp_IO.xls
documentation for the board

Bank 84 bits always set : FFC000, which is:
   AC14 bank_84[14] AT24C04 SCK
   AC13 bank_84[15] unknown
   AD14 bank_84[16] AT24C04 SDA
   AD13 bank_84[17] unknown
   AE15 bank_84[18] QSFP A RESET
   AD15 bank_84[19] QSFP A INTERRUPT
   AF13 bank_84[20] unknown
   AE13 bank_84[21] unknown
   AF15 bank_84[22] QSFP A LP_MODE
   AF14 bank_84[23] QSFP A MOD_SEL

Bank 86 bits always set : 000035, which is:
   B11  bank_86[0] unknown
   A10  bank_86[2] QSFP B RESET
   A9   bank_86[4] QSFP B LP_MODE
   B9   bank_86[5] QSFP B MOD_SEL

Bank 87 bits always set : 00009F, which is:
   A14  bank_87[0] QSFP A SDA
   B14  bank_87[1] QSFP A SCL
   A12  bank_87[2] QSFP B INTERRUPT
   A13  bank_87[3] QSFP B MOD_PRSN
   B12  bank_87[4] QSFP B SDA
   C14  bank_87[7] QSFP A MOD_PRSN

Bank 84 bits always clear : 000040, which is:
   Y16  bank_84[6] unknown

Bank 86 bits always clear : 200000, which is:
   J11  bank_86[21] unknown

Bank 87 bits always clear : 000000. I.e. none

Expected signals not identified are:
- QSFP B SCL

Which could cause the QSFP B I2C communication to not work.

XCKU5P_pcie_qsfp_or_sfp_IO.xls has QSFP B SCL as B10.
There is an unknown signal on B11 driven high externally, which could be the SCL.

