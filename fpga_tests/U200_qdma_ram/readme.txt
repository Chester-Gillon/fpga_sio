This was created to test the QDMA Subsystem in an UltraScale+ with memory mapped access to internal RAM.

It has one 8M URAM block memory, configured with a AXI interface at 250 MHz 512 bits wide,
to match the AXI interface from the QDMA which has a x16 8 GT/s PCIe interface.

To get the timing to pass:
a. The READ_LATENCY on the AXI BRAM controller increased from the default 1 to 3.
b. The "Vivado Implementation 2025" strategy set to Performance_ExtraTimingOpt


Failed attempts at larger memory with AXI interface matching QDMA
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In an attempt to maximise the amount of RAM tested tried using a total of 30 MiB split into:
a. Three 8 MiB URAM block memories. Intended to allow for the URAM split across the 3 SLRs in the U200 device.
b. Three 2 MiB BRAM block memories. Lower size as the U200 has less BRAM than URAM.

However, with the AXI block ram interface at 250 MHz 512 bits wide, to match the QDMA, the attempts to use larger
memory the timing failed to meet the setup timing on axi_bram_ctrl_0_BRAM_PORTA_CLK. E.g.:
---------------------------------------------------------------------------------------------------
From Clock:  axi_bram_ctrl_0_BRAM_PORTA_CLK
  To Clock:  axi_bram_ctrl_0_BRAM_PORTA_CLK

Setup :        47609  Failing Endpoints,  Worst Slack       -1.688ns,  Total Violation   -32201.417ns
Hold  :            0  Failing Endpoints,  Worst Slack        0.009ns,  Total Violation        0.000ns
PW    :            0  Failing Endpoints,  Worst Slack        1.300ns,  Total Violation        0.000ns
---------------------------------------------------------------------------------------------------

Trying other implementation strategies such as Congestion_SSI_SpreadLogic_high didn't help to get timing closure,
but didn't attempt to compare the timing results in detail using the different implementation strategies.
On a failed attempt the implementation could run for 2 hours before finally failed.

Trying a single 16 MiB URAM block memory, which requires the URAM's to be split across more than one SLR could be placed
but the timing failed.

Didn't investigate increasing the read latency above 3.


Option to use larger memory with BRAM AXI interface at 125 MHz 1024 bits wide
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Was able to meet timing using:
1. Total of 30 MiB split as:
   a. Three 8 MiB URAM block memories.
   b. Three 2 MiB BRAM block memories.
2. The AXI BRAM controllers were set to 1024 bits wide.
   I.e. compared to the QDMA AXI interface double the width.
3. Using a Clocking Wizard to create a 125 MHz clock for the block rams, from the 250 MHz axi_aclk output by the QDMA Subsystem.
4. Using AXI Smart Connect to convert from the 250 MHz 512 bit M_AXI interface from the QDMA Subsystem and the 125 MHz 1024 bit
   AXI interface to the AXI BRAM controllers.

Looking at schematic in synthesised design appears to show Asynchronous Clock Crossing Stages being used.

PG247 SmartConnect contains:
  "When the tools determine that the relationship between the clock domains is an integer ratio (faster or slower)
   within the range 1:16 to 16:1, and that the clocks are derived from the same clock source, the tools automatically
   configure the clock converter to perform synchronous conversion; otherwise, the clock converter is configured in asynchronous mode."

Not sure how to check if a synchronous conversion has been configured or not.

The following timing advisories are reported for the Clocking Wizard uses to create the 125 MHz clock from the 250 MHz clock:
  CLKC #1 Advisory The MMCME4 cell mmcme4_adv_inst driven by BUFG_GT cell bufg_gt_userclk uses CLKOUT* output(s)
  with a DIVIDE value of [1..8] which could be accomplished using a BUFG_GT divide capability. Consider using BUFG_GT(s)
  to accomplish the desired divide(s). 

  CLKC #1 Advisory The MMCME4_ADV cell mmcme4_adv_inst CLKIN1 or CLKIN2 pin is driven by global Clock buffer(s) bufg_gt_userclk
  and does not have a LOC constraint. It is recommended to LOC the MMCM and use the CLOCK_DEDICATED_ROUTE constraint on the net(s)
  driven by the global Clock buffer(s).

When temporarily changed the project to use the discontinued AXI Interconnect, instead of the AXI Smart Connect could see
the hierarchy inside:
a. The 250 MHz axi_aclk is used to converted the data width from 512 to 1024 bits.
b. The AXI Crossbar operates at 1024 bits using the 250 MHz axi_aclk.
c. The output couplers operate at 1024 bits using a AXI Clock Converter:
   - If the Clocking Wizard is used to generate the 125 MHz BRAM clock "Is ACLK Asynchronous" is Yes
   - Whereas if a BUFG GT Utility Buffer is used to divide the axi_aclk by two, "Is ACLK Asynchronous" is No.
     I.e. using a synchronous clock conversion.

https://zipcpu.com/blog/2021/03/20/xilinx-forums.html describes performance issues with crossing clock domains
or width conversions.

If wanted to check the performance of such an interface conversion, start by using AXI SmartConnect and trying to
use a BUFG_GT to halve the frequency to try and make AXI SmartConnect use a synchronous conversion. PG247 contains:
  "Clock converters always introduce latency. Asynchronous conversion incurs more latency and uses more logic
   resources than synchronous conversion."

