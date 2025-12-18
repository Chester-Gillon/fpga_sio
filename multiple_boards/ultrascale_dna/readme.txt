0. Introduction
===============

The source files in this directory are from the hdl directory of the xilinx.com_user_AXI_DNA_1.0.zip
downloaded from "71342 - Zynq UltraScale+ Device - PS DNA is not write protected and is a different value than the PL DNA"
https://adaptivesupport.amd.com/s/article/71342?language=en_US

They provide a AXI core to read the DNA.

Looking at the code, the first 96 S_AXI_ACLK clocks after S_AXI_ARESETN is released is used to shift out the DNA value
and store in a register.

If an AXI read was performed before the initial 96 S_AXI_ACLK clocks had expired, an invalid DNA value could be returned.
This is because the S_DONE state in the DNA_Module isn't used to interlock the S_AXI_RVALID output.

With software the DNA value is vert unlikely to get in quick enough after S_AXI_ARESETN is released to actually sample
an invalid DNA value which is in the process of being shifted.


1. Issues found during Vivado 2025.2 Synthesis
==============================================

1.1. Error about out-of-range
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Got the error:
[Synth 8-11323] assigned value '4' out of range ["DNA_read.vhd":154]

That was traced to a process trying to increment the COUNT signal out of its allowed range.

When fixing that error, also changed COUNT to be explicitly zeroed by reset.

1.2. Warning about inferred latch
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When the above warning was fixed got the warnings:
[Synth 8-327] inferring latch for variable 'FSM_onehot_NEXT_STATE_reg' ["DNA_read.vhd":93]
[Synth 8-327] inferring latch for variable 'FSM_sequential_NEXT_STATE_reg' ["DNA_read.vhd":93]

Looking at the schematic following synthesis shows FSM_sequential_NEXT_STATE_reg[0] and FSM_sequential_NEXT_STATE_reg[1]
are the LDCE "Transparent Data Latch with Asynchronous Clear and Gate Enable" primitive.

To fix this in the MAIN_PROC add when others so NEXT_STATE is always updated:
		when others=>
		   NEXT_STATE <= S_RESET;

Following this change the warning was gone, and the schematic shows the LDCE primitives are no longer present.
Albeit it there are no NEXT_STATE registers so assume has been optimised.

Did end with the following warning, which not sure how to restructure the code to remove:
[Synth 8-13157] FSM with state register 'COUNT_reg' is not having reset. This may cause incorrect behavior.
                User should consider using FSM_SAFE_STATE attribute on the register

Due to the states being reset think OK.

1.3. Failed to meet timing
~~~~~~~~~~~~~~~~~~~~~~~~~~

When the 250 MHz axi_aclk was used for the DNA, the timing wasn't meet.

Changed the clock used for the DNA to 100 MHz, and the timing was met.

1.4. Final changes to DNA_read.vhd
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

$ git diff ~/Downloads/xilinx.com_user_AXI_DNA_1.0/hdl/DNA_read.vhd ~/fpga_sio/multiple_boards/ultrascale_dna/DNA_read.vhd | cat
diff --git a/home/mr_halfword/Downloads/xilinx.com_user_AXI_DNA_1.0/hdl/DNA_read.vhd b/home/mr_halfword/fpga_sio/multiple_boards/ultrascale_dna/DNA_read.vhd
index e900714..783c02f 100644
--- a/home/mr_halfword/Downloads/xilinx.com_user_AXI_DNA_1.0/hdl/DNA_read.vhd
+++ b/home/mr_halfword/fpga_sio/multiple_boards/ultrascale_dna/DNA_read.vhd
@@ -114,6 +114,7 @@ begin
                        NEXT_STATE <= S_DONE;
 			
 		when others=>
+		   NEXT_STATE <= S_RESET;
 	end case;--NEXT_STATE
 end process MAIN_PROC;
 
@@ -128,6 +129,7 @@ begin
                     RD <= '0';
                     SFT <= '0';
                     RESET <= '0';           --de-assert reset (initially asserted)
+                    COUNT <= 0;
             when S_DNA =>
                 case COUNT is
                     when 0 =>
@@ -151,7 +153,6 @@ begin
                          RD <= '0';
                          SFT <= '0';
                          DONE_DNA <= '1';
-                         COUNT <= COUNT + 1;
                 
                     when others=>
                       COUNT <= COUNT + 1;
@@ -163,7 +164,7 @@ begin
                 	
             when others =>
                 RESET <= '1';	--re-assert reset
-            end case;		
+        end case;		
     end if;
 end process PROC;
 

