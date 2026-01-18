The VD100_10G_ether_dual design is design to provide dual 10 GbE ports using the 
Versal Devices Integrated 100G Multirate Ethernet MAC Subsystem (MRMAC).

XDMA with two C2H / H2C AXI streams is used to interface to the data ports of the MRMAC to transmit / receive Ethernet packets.

The AXI slave memory map:
a. 0x0 .. 0xffff is the MRAC Configuration registers, Status registers, and Statistics counters.
b. 0x10000 .. 0x10fff dual GPIO to control the output pins for the SFP+ control signals
   Since the AXI GPIO doesn't allow output bits to be read back, configured the AXI GPIO as:
   - Dual channel
   - 1st channel is all inputs
   - 2nd channel is all outputs
   - Output channel bits are connected to input channel bits.

   Bits are:
   Bit 0 : SFP1_TX_EN
   Bit 1 : SFP2_TX_EN

   Write 1 to enable the transmitter and 0 to disable.
   The VD100 baseboard has a transistor which inverts the signal, which makes these GPIOs enables rather than disables.
c. 0x11000 ... 0x11fff
   I2C controller for SFP1.
   For some reason, the I2C signals on the VD100 are only connected to SFP1.
