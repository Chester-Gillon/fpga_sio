The XCKU5P_DUAL_QSFP_qdma_ram design was created to test the Queue DMA Subsystem for PCI Express use of multiple functions.

There is 2MB of on-chip memory connected to the DMA M_AXI (half BRAM and half URAM due to other use of memory).
The QDMA BARs are enabled on all physical functions, as the IP doesn't allow otherwise.

The M_AXI_LITE is connected to the following peripherals, and the BARs on each function are set via
their sizes and PCIe to AXI Translation to only be able to access one peripheral.

The functions and their accessible peripherals:
PF0 : Subsystem ID 000C
      Quad SPI
PF1 : Subsystem ID 000D 
      SYMON
PF2 : Subsystem ID 000E
      GPIO for version information
PF3 : Subsystem ID 000F
      16550 UART, which has internal loopback. 
      While the UART registers start at offset 0x1000, in the bridge PCIe to AXI Translation the offset was set so the
      UART registers start at offset zero in the BAR.

      Standard UARTs have byte registers one byte apart.
      Whereas the Xilinx 16550 IP has each register as 32-bits and only the least significant bits defined.


FLR issues
~~~~~~~~~~

Initially enabled Function Level Reset in the Queue DMA Subsystem.

Which was shown as a possible method:
$ tail /sys/bus/pci/devices/0000:10:00.?/reset_method
==> /sys/bus/pci/devices/0000:10:00.0/reset_method <==
flr bus

==> /sys/bus/pci/devices/0000:10:00.1/reset_method <==
flr

==> /sys/bus/pci/devices/0000:10:00.2/reset_method <==
flr

==> /sys/bus/pci/devices/0000:10:00.3/reset_method <==
flr

However, attempting to run dump_info/dump_pci_info_vfio caused a hang with dmesg reporting:
$ dmesg|grep FLR
[  332.633110] vfio-pci 0000:10:00.0: not ready 1023ms after FLR; waiting
[  333.721048] vfio-pci 0000:10:00.0: not ready 2047ms after FLR; waiting
[  335.833106] vfio-pci 0000:10:00.0: not ready 4095ms after FLR; waiting
[  339.993034] vfio-pci 0000:10:00.0: not ready 8191ms after FLR; waiting
[  348.697067] vfio-pci 0000:10:00.0: not ready 16383ms after FLR; waiting
[  365.592376] vfio-pci 0000:10:00.0: not ready 32767ms after FLR; waiting
[  402.456138] vfio-pci 0000:10:00.0: not ready 65535ms after FLR; giving up
[  450.839445] vfio-pci 0000:10:00.2: not ready 1023ms after FLR; waiting
[  451.927260] vfio-pci 0000:10:00.2: not ready 2047ms after FLR; waiting
[  454.039613] vfio-pci 0000:10:00.2: not ready 4095ms after FLR; waiting
[  458.263590] vfio-pci 0000:10:00.2: not ready 8191ms after FLR; waiting
[  466.967592] vfio-pci 0000:10:00.2: not ready 16383ms after FLR; waiting
[  483.863595] vfio-pci 0000:10:00.2: not ready 32767ms after FLR; waiting
[  517.143585] vfio-pci 0000:10:00.2: not ready 65535ms after FLR; giving up    

Disabling the Function Level Reset stopped the hanging. Not sure what the source of the issue is.

The above issues with Function Level Reset were when the card was fitted to a HP Pavilion 590-p0053na in which other
PCIe errors were seen even when a Function Level Reset was disabled
- see https://gist.github.com/Chester-Gillon/ba675c6ab4e5eb7271f43f8ce4aedb6c#5-pcie-errors-when-fittd-to-a-hp-pavilion-590-p0053na

Moved the card to a HP Z4 G4, which stopped the other PCIe errors when Function Level Reset were disabled.
Attempted to re-enable Function Level Reset. However, the HP Z4 G4 then hung when dump_pci_info_vfio opened the first function in the design.
Had to hold the power button to force a power off to recover.

What I failed to notice during the initial failed attempt to use FLR was:
1. Enabling FLR in the QDMA IP enables "FLR Ports". 
   The user logic is supposed to signal when FLR for a specific function has been completed.
   In the initial attempt nothing was connected to the FLR ports.
   The ports are such that the output signals can be looped-back to the input signals, without needing additional user logic.
2. PG302 describes a "FLR Control Status Register" which is intended to be used to initiate pre-FLR.
   This is specific to the QDMA IP, rather than part of the PCIe standard, so vfio-pci won't know about it.
   From PG302 it isn't immediately obvious if a PCIe FLR is received, but the FLR Control Status Register register wasn't used
   to initiate pre-FLR, if the FLR Port signals are used.
