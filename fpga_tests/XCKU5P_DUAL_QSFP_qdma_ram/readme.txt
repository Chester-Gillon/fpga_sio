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

