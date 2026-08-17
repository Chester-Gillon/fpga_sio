1. Changes in PCIe Revision 01
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Added an AXI4 Stream Switch for the Ethernet packet streams, and moved to Vivado 2026.1.

Slave switch ports:
0: XDMA H2C_0
1: XDMA H2C_1
2: 100G port 0 receive
3: 100G port 1 receive

Master switch ports:
0: 100G port 0 transmit
1: 100G port 1 transmit
2: XDMA C2H_0
3: XDMA C2H_1

Switch control registers at offset 0x9000.

Noticed the following warnings were being generated about the AXI stream ports on the AXI switch:
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S00_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S00_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M00_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M00_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S01_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S01_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M01_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M01_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S02_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S02_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M02_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M02_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S03_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'S03_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M03_AXIS.TDATA_NUM_BYTES'. Logical port width '64' and physical port width '256' do not match.
WARNING: [IP_Flow 19-5378] Port width mismatch for bus parameter 'M03_AXIS.TUSER_WIDTH'. Logical port width '1' and physical port width '4' do not match.

On looking at the block diagram, the port widths on axi_switch_0 are 4 times wider than expected, where 4 happens to be the
number of slave and master ports.

On looking at a design in Vivado 2024.2 which had an AXI switch with 2 slave and 2 master ports, that had ports 2 times wider than expected.

https://adaptivesupport.amd.com/s/question/0D5KZ00000wp9fV0AQ/ipflow-195378-port-width-mismatch-for-bus-parameter-m05axistdestwidth-logical-port-width-4-and-physical-port-width-64-do-not-matchi-have-a-few-warnings-like-that-on-the-interconnect-ip-during-the-synthesis-someone-knows-how-to-tune-it?language=en_US
has:
   Yes, indeed, if we have a design or TCL that can reproduce these warnings, with that we can help further. Can this be shared, please?
   In general port width mismatch related warnings can be safely ignored as the width depends on master/slave it is connected to.


2. Changes in PCIe Revision 02
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Added a drp_bridge to access the DRP interface in the CMAC ports:
a. Offset 0xA000 is the CMAC port 0 DRP.
b. Offset 0xB000 is the CMAC port 1 DRP.

Added the DRP interfaces to be able to access the CTL_RX_MIN_PACKET_LEN and CTL_RX_MAX_PACKET_LEN attributes.

