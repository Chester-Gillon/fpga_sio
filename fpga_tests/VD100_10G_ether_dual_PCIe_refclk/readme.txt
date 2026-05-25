0. Introduction
~~~~~~~~~~~~~~~

This is a variant of the VD100_10G_ether_dual in which:
1. The reference clock for the MRMAC has been changed from the 156.25 MHz provided on a dedicated oscillator to the 
   100 MHz PCIe reference clock provided on the PCIe slot.

   The reason for doing that was to investigate how to configure a reference clock for the GT Quad which was one
   of the presets in the MRMAC.

   10 GbE Ethernet requires a reference clock which is +/-100 PPM. Whereas the PCIe reference clock:
   a. Is +/- 300 ppm when Spread Spectrum Clocking is disabled.
   b. Spread Spectrum Clocking adds an addition 5000 PPM of jitter.

   It isn't clear how to determine if a particular PC uses Spread Spectrum Clocking on the PCIe reference clock.

   As a result, expected that 10 GbE ports may not interoperate with other devices.

2. When the 10 GbE ports didn't interoperate with a switch, tried disabling Slot Clock in the PCIe endpoint in case that allowed
   to interoperate with a switch


1. Change to use PCIe reference clock for MRMAC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. On the top level diagram delete the SFP_REF clock input.

2. On the xmda_0_support diagram connect the IBUF_OUT from the Utility Buffer to a pcie_refclk_ibuf_out output.

3. For the mrmac_10G_dual diagram:
   a. Delete the SFP_REF input and associated Utility Buffer.
   b. Connect the GT_REFCLK0 on the gt_quad_base to a GTREFCLK0 input.
   c. Connect the GTREFCLK0 input to the pcie_refclk_ibuf_out added to xmda_0_support

   Which means the PCIe reference clock is now also used for the MRMAC.

4. For the gt_quad_base used for the MRMAC on the mrmac_10G_dual diagram, perform the following for both
   "Transceiver Configs Protocol 0" and "Transceiver Configs Protocol 1":
   a. Change the "Transceiver Configs Protocol" from AUTO to MANUAL.
   b. Open the "Transceiver Configs Protocol" and:
      - For both TX and RX change the "Requested Reference Clock (MHz)" from 156.25 to 100.
      - For both TX and RX change the Pll type from RPLL to LCPPL.
        That is required to get the "Actual Reference Clock (MHz)" to match the requested,
        and enables fractional feedback divider.
      - Put the following back to 644.531, which were changed to the lower 322.266 when changed the Pll type:
        - TXOUTCLK frequency (MHz)
        - RXOUTCLK frequency (MHz)
        - RXRECCLKOUT frequency (MHz)

      In the Advanced section leave the "PPM offset between receiver and transmitter" as 200,
      since are only changing one end of the Ethernet link.


2. Doesn't interoperate with switch when used in a HP Pavilion 590-p0053na desktop, and Slot Clock enabled
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The design with the PCIe reference clock for MRMAC was used in a HP Pavilion 590-p0053na desktop.

When connected to a T1700G-28TQ switch the link was always down at the switch end.

Sometimes the MRMAC RX realtime status was "0x00000180", meaning the link was down.
Othertimes the MRMAC RX realtime status was "0x00000043", meaning the MRMAC receive had achieved block lock
but was receiving a remote fault from the switch.

Example when MRMAC port 0 was connected to switch receive and MRMAC port 1 to switch transmit via split 10GBase-SR transceivers:
[mr_halfword@ryzen-alma release]$ identify_pcie_fpga_design/display_identified_pcie_fpga_designs 
Opening device 0000:10:00.0 (10ee:b044) with IOMMU group 10
Enabled bus master for 0000:10:00.0
Warning: Device device 0000:10:00.0 (10ee:b044) has reduced bandwidth
         Max width x4 speed 16 GT/s. Negotiated width x4 speed 8 GT/s

Design VD100_10G_ether_dual:
  PCI device 0000:10:00.0 rev 00 IOMMU group 10
  DMA bridge bar 2 AXI Stream
  Channel ID  addr_alignment  len_granularity  num_address_bits
       H2C 0               1                1                64
       H2C 1               1                1                64
       C2H 0               1                1                64
       C2H 1               1                1                64
  IIC registers at bar 0 offset 0x11000
  MRMAC configuration revision: 0x00000001
  Port 0:
    Data rate: 10GE
    GT Quad Operating mode: 10GE Wide, 10.3125 Gb/s, 32 bits, 322.2656 MHz, NRZ
    AXI4-Stream Mode: Independent, 32 bits, Non-Segmented
    Rx min packet len: 64
    Rx max packet len: 9600
    FEC Operating Mode: FEC Disabled
    fec_configuration_reg1: 0x00000000
      ctl_fec_mode=0
      ctl_rx_fec_bypass_indication=0 ctl_rx_fec_bypass_correction=0
      ctl_rx_fec_transcode_clause49=0
      ctl_rx_fec_alignment_bypass=0 ctl_tx_fec_transcode_bypass=0
      ctl_rx_fec_transcode_bypass=0 ctl_rx_fec_cdc_bypass=0
      ctl_rx_fec_errind_mode=0 ctl_tx_fec_four_lane_pmd=0
    TX realtime status: 0x00000000
      Port TX local fault            : 0
      Port TX axis underflow         : 0 
      Port TX axis error             : 0
      Port TX flexif error           : 0
      Port TX pcs bad code           : 0
      Port TX CL82 CL49 convert error: 0
      Port TX flex fifo overflow     : 0
      Port TX flex fifo underflow    : 0
    RX realtime status: 0x00000043
      Port RX Status: 1
      Port RX Block Lock: 1
      Port RX aligned: 0
      Port RX misaligned: 0
      Port RX aligned error: 0
      Port RX High BER: 0
      Port RX remote fault: 1
      Port RX local fault: 0
      Port RX internal local fault: 0
      Port RX received local fault: 0
      Port RX bad code: 0
      Port RX bad preamble: 0
      Port RX bad SFD: 0
      Port RX got signal ordered set: 0
      Port RX flex if error: 0
      Port RX Framing Error: 0
      Port RX Synced: 0
      Port RX Synced Error: 0
      Port RX BIP Error: 0
      Port RX CL49_82 convert error: 0
      Port RX pcs bad code: 0
      Port RX AXIS fifo overflow: 0
      Port RX AXIS error: 0
      Port RX invalid start: 0
      Port RX flex fifo overflow: 0
      Port RX flex fifo underflow: 0
  Port 1:
    Data rate: 10GE
    GT Quad Operating mode: 10GE Wide, 10.3125 Gb/s, 32 bits, 322.2656 MHz, NRZ
    AXI4-Stream Mode: Independent, 32 bits, Non-Segmented
    Rx min packet len: 64
    Rx max packet len: 9600
    FEC Operating Mode: FEC Disabled
    fec_configuration_reg1: 0x00000000
      ctl_fec_mode=0
      ctl_rx_fec_bypass_indication=0 ctl_rx_fec_bypass_correction=0
      ctl_rx_fec_transcode_clause49=0
      ctl_rx_fec_alignment_bypass=0 ctl_tx_fec_transcode_bypass=0
      ctl_rx_fec_transcode_bypass=0 ctl_rx_fec_cdc_bypass=0
      ctl_rx_fec_errind_mode=0 ctl_tx_fec_four_lane_pmd=0
    TX realtime status: 0x00000000
      Port TX local fault            : 0
      Port TX axis underflow         : 0 
      Port TX axis error             : 0
      Port TX flexif error           : 0
      Port TX pcs bad code           : 0
      Port TX CL82 CL49 convert error: 0
      Port TX flex fifo overflow     : 0
      Port TX flex fifo underflow    : 0
    RX realtime status: 0x00000180
      Port RX Status: 0
      Port RX Block Lock: 0
      Port RX aligned: 0
      Port RX misaligned: 0
      Port RX aligned error: 0
      Port RX High BER: 0
      Port RX remote fault: 0
      Port RX local fault: 1
      Port RX internal local fault: 1
      Port RX received local fault: 0
      Port RX bad code: 0
      Port RX bad preamble: 0
      Port RX bad SFD: 0
      Port RX got signal ordered set: 0
      Port RX flex if error: 0
      Port RX Framing Error: 0
      Port RX Synced: 0
      Port RX Synced Error: 0
      Port RX BIP Error: 0
      Port RX CL49_82 convert error: 0
      Port RX pcs bad code: 0
      Port RX AXIS fifo overflow: 0
      Port RX AXIS error: 0
      Port RX invalid start: 0
      Port RX flex fifo overflow: 0
      Port RX flex fifo underflow: 0
[mr_halfword@ryzen-alma release]$ mrmac_ethernet/mrmac_statistics 10
Opening device 0000:10:00.0 (10ee:b044) with IOMMU group 10
Enabled bus master for 0000:10:00.0
Warning: Device device 0000:10:00.0 (10ee:b044) has reduced bandwidth
         Max width x4 speed 16 GT/s. Negotiated width x4 speed 8 GT/s
Press Ctrl-C to stop the MRMAC port statistics collection


The MRMAC statistics show the receive port connected to the switch is getting framing errors, bad codes and some frames with bad FCS:
16:10:41.089 collection number 1
VD100_10G_ether_dual port 0 statistics (over 10.001 secs):
  TX_CYCLE_COUNT           :      6433378127
  RX_CYCLE_COUNT           :      6446486387
  RX_FRAMING_ERR_0         :        12668942
  RX_BAD_CODE              :        12233071
  RX_INVALID_START         :            2997
  RX_TOTAL_PACKETS         :              73
  RX_TOTAL_BYTES           :           18292
  RX_PACKET_128_255_BYTES  :              42
  RX_PACKET_256_511_BYTES  :              31
  RX_BAD_FCS               :              73
  RX_PACKET_BAD_FCS        :              73

VD100_10G_ether_dual port 1 statistics (over 10.001 secs):
  TX_CYCLE_COUNT           :      6433371393
  RX_CYCLE_COUNT           :      6433371393



16:10:51.089 collection number 2
VD100_10G_ether_dual port 0 statistics (over 10.000 secs):
  TX_CYCLE_COUNT           :      6433058933
  RX_CYCLE_COUNT           :      6446168370
  RX_FRAMING_ERR_0         :        12665668
  RX_BAD_CODE              :        12237170
  RX_INVALID_START         :            3020
  RX_TOTAL_PACKETS         :              79
  RX_TOTAL_BYTES           :           19732
  RX_PACKET_128_255_BYTES  :              47
  RX_PACKET_256_511_BYTES  :              32
  RX_BAD_FCS               :              79
  RX_PACKET_BAD_FCS        :              79

VD100_10G_ether_dual port 1 statistics (over 10.000 secs):
  TX_CYCLE_COUNT           :      6433058822
  RX_CYCLE_COUNT           :      6433058822

^C

16:10:51.621 collection number 3
VD100_10G_ether_dual port 0 statistics (over 0.532 secs):
  TX_CYCLE_COUNT           :       342113977
  RX_CYCLE_COUNT           :       342810999
  RX_FRAMING_ERR_0         :          674319
  RX_BAD_CODE              :          651403
  RX_INVALID_START         :             182
  RX_TOTAL_PACKETS         :               4
  RX_TOTAL_BYTES           :             904
  RX_PACKET_128_255_BYTES  :               3
  RX_PACKET_256_511_BYTES  :               1
  RX_BAD_FCS               :               4
  RX_PACKET_BAD_FCS        :               4

VD100_10G_ether_dual port 1 statistics (over 0.532 secs):
  TX_CYCLE_COUNT           :       342113758
  RX_CYCLE_COUNT           :       342113758


For this test Slot Clock was left enabled (the default) in the PCIe endpoint:
[mr_halfword@ryzen-alma release]$ dump_info/dump_pci_info_pciutils 
domain=0000 bus=10 dev=00 func=00 rev=00
  vendor_id=10ee (Xilinx Corporation) device_id=b044 (Device b044) subvendor_id=0002 subdevice_id=0026
  iommu_group=10
  driver=vfio-pci
  module=vfio_pci
  control: I/O- Mem+ BusMaster- ParErr- SERR- DisINTx-
  status: INTx- <ParErr- >TAbort- <TAbort- <MAbort- >SERR- DetParErr-
  bar[0] base_addr=f0400000 size=20000 is_IO=0 is_prefetchable=1 is_64=1
  bar[2] base_addr=f0420000 size=10000 is_IO=0 is_prefetchable=1 is_64=1
  Capabilities: [40] Power Management version 3
    Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0-,D1-,D2-,D3hot-,D3cold+)
    Status: D3 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
  Capabilities: [70] PCI Express v2 Express Endpoint, MSI 0
    Link capabilities: Max speed 16 GT/s Max width x4
    Negotiated link status: Current speed 8 GT/s Width x4
    Link capabilities2: Supported link speeds 2.5 GT/s 5.0 GT/s 8.0 GT/s 16.0 GT/s
    DevCap: MaxPayload 1024 bytes PhantFunc 0 Latency L0s Maximum of 64 ns L1 Maximum of 1 μs
            ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset- SlotPowerLimit 0.000W
    DevCtl: CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+
            RlxdOrd+ ExtTag+ PhantFunc- AuxPwr- NoSnoop+
    DevSta: CorrErr- NonFatalErr- FatalErr- UnsupReq- AuxPwr- TransPend-
    LnkCap: Port # 0 ASPM not supported
            L0s Exit Latency More than 4 μs
            L1 Exit Latency More than 64 μs
            ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp+
    LnkCtl: ASPM Disabled RCB 64 bytes Disabled- CommClk+
            ExtSynch- ClockPM- AutWidDis- BWInt- ABWMgmt-
    LnkSta: TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
  Capabilities: [100 v1] Advanced Error Reporting
  Capabilities: [1c0 v1] Secondary PCIe Capability
  Capabilities: [1f0 v1] Virtual Channel Capability
  Capabilities: [3a0 v1] Data Link Feature
  Capabilities: [3b0 v1] Physical Layer 16.0 GT/s
  Capabilities: [400 v1] Lane Margining at Receiver
  domain=0000 bus=00 dev=01 func=01 rev=00
    vendor_id=1022 (Advanced Micro Devices, Inc. [AMD]) device_id=15d3 (Raven/Raven2 PCIe GPP Bridge [6:0])
    iommu_group=2
    driver=pcieport
    control: I/O+ Mem+ BusMaster+ ParErr- SERR- DisINTx+
    status: INTx- <ParErr- >TAbort- <TAbort- <MAbort- >SERR- DetParErr-
    Capabilities: [50] Power Management version 3
      Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0+,D1-,D2-,D3hot+,D3cold+)
      Status: D0 NoSoftRst- PME-Enable- DSel=0 DScale=0 PME-
    Capabilities: [58] PCI Express v2 Root Port, MSI 0
      Link capabilities: Max speed 8 GT/s Max width x8
      Negotiated link status: Current speed 8 GT/s Width x4
      Link capabilities2: Supported link speeds 2.5 GT/s 5.0 GT/s 8.0 GT/s
      DevCap: MaxPayload 512 bytes PhantFunc 0 Latency L0s Maximum of 64 ns L1 Maximum of 1 μs
              ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset- SlotPowerLimit 0.000W
      DevCtl: CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+
              RlxdOrd+ ExtTag+ PhantFunc- AuxPwr- NoSnoop+
      DevSta: CorrErr- NonFatalErr- FatalErr- UnsupReq- AuxPwr- TransPend-
      LnkCap: Port # 0 ASPM L1
              L0s Exit Latency More than 4 μs
              L1 Exit Latency 32 μs to 64 μs
              ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp+
      LnkCtl: ASPM Disabled RCB 64 bytes Disabled- CommClk+
              ExtSynch- ClockPM- AutWidDis- BWInt+ ABWMgmt+
      LnkSta: TrErr- Train- SlotClk+ DLActive+ BWMgmt- ABWMgmt-
      SltCap: AttnBtn- PwrCtrl- MRL- AttnInd- PwrInd- HotPlug- Surprise-
              Slot #0 PowerLimit 0.000W Interlock- NoCompl+
    Capabilities: [a0] Message Signaled Interrupts
    Capabilities: [c0] Bridge subsystem vendor/device ID
    Capabilities: [c8] HyperTransport
    Capabilities: [100 v1] Vendor-Specific
    Capabilities: [150 v2] Advanced Error Reporting
    Capabilities: [270 v1] Secondary PCIe Capability
    Capabilities: [2a0 v1] Access Control Services
    Capabilities: [370 v1] L1 PM Substates

Which shows:
a. SlotClk+ in endpoint link status
b. CommClk+ in endpoint link control

