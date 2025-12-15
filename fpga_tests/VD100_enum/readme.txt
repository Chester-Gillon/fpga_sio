This contains designs to test the PCIe x4 connector in the ALINX VD100 at different maximum speeds.


gen2_x4
-------

The maximum link speed was set to 5 GT/s which is the default maximum allowed by the IP when using
a XCVE2302-SFVA784-1LP-E-S part.

In a gen3 slot in a HP Pavilion 590-p0053na desktop this enumerated at the expected 5 GT/s speed and x4 width:
[mr_halfword@ryzen-alma release]$ dump_info/dump_pci_info_vfio 
Opening device 0000:10:00.0 (10ee:b024) with IOMMU group 10
domain=0000 bus=10 dev=00 func=00 rev=00
  vendor_id=10ee (Xilinx Corporation) device_id=b024 (Device b024) subvendor_id=0002 subdevice_id=0020
  iommu_group=10
  driver=vfio-pci
  control: I/O- Mem+ BusMaster- ParErr- SERR- DisINTx-
  status: INTx- <ParErr- >TAbort- <TAbort- <MAbort- >SERR- DetParErr-
  bar[0] base_addr=fd010000 size=1000 is_IO=0 is_prefetchable=0 is_64=0
  bar[1] base_addr=fd000000 size=10000 is_IO=0 is_prefetchable=0 is_64=0
  Capabilities: [40] Power Management
  Capabilities: [70] PCI Express v2 Express Endpoint, MSI 0
    Link capabilities: Max speed 5 GT/s Max width x4
    Negotiated link status: Current speed 5 GT/s Width x4
    Link capabilities2: Supported link speeds 2.5 GT/s 5.0 GT/s
    DevCap: MaxPayload 1024 bytes PhantFunc 0 Latency L0s Maximum of 64 ns L1 Maximum of 1 μs
            ExtTag- AttnBtn- AttnInd- PwrInd- RBE+ FLReset- SlotPowerLimit 0.000W
    DevCtl: CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+
            RlxdOrd+ ExtTag- PhantFunc- AuxPwr- NoSnoop+
    DevSta: CorrErr- NonFatalErr- FatalErr- UnsupReq- AuxPwr- TransPend-
    LnkCap: Port # 0 ASPM not supported
            L0s Exit Latency More than 4 μs
            L1 Exit Latency More than 64 μs
            ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp+
    LnkCtl: ASPM Disabled RCB 64 bytes Disabled- CommClk+
            ExtSynch- ClockPM- AutWidDis- BWInt- ABWMgmt-
    LnkSta: TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-


The first version committed didn't have the PCIe debugger enabled.
Realised that if you edit the settings in the xdma_0 block, the settings within the pcie block contained in the
block automation generated xdma_0_support diagram aren't updated to be consistent.

Therefore, when enabling the PCIe debugger:
a. Deleted the xdma_0_support diagram.
b. Enabled enable_pcie_debug in the xdma_0 block.
c. Reran the block automation from the GUI, which used the following TCL:
   apply_bd_automation -rule xilinx.com:bd_rule:xdma -config { axi_strategy {max_data} link_speed {2} link_width {4} pl_pcie_cpm {PL-PCIE}}  [get_bd_cells xdma_0]
d. After re-writing the create_project.tcl after the change some of the nets had there trailing numeric suffix changed.

@todo
a. When the xdma_0_support diagram is created by the block automation it has a BUFG_GT_CE[0:0] input.
   Not sure from the documentation what to connect this to so based upon
   https://gist.github.com/Chester-Gillon/b3b5b3a91d734807e4e739d3fc2b0fa8#22-unable-to-create-a-bifurcated-x8x8-pcie-design
   seen in an UltraScale+:
   - In the xdma_0_support diagram connected the gtpowergood output of the gtwiz_versal_0 to a output pin.
   - In the top-level diagram connected gtpowergood to BUFG_GT_CE[0:0].
b. The xdma_0_support diagram had a gtwiz_freerun_clk which from the documentation wasn't sure what should be connected to.
   Since a Versal needs the cips block, configured one of the PL output clocks to be 100 MHz (obtained frequency is 99.999001 MHz).
   Not sure of the start-up timing for the gtwiz_freerun_clk.


gen3_x4
-------

@todo this hasn't yet been committed as wasn't enumerating in the HP Pavilion 590-p0053na desktop.

By default the "DMA /Bridge Subsystem for PCI Express (4.2) IP" was only offering a maximum link speed of either:
- 2.5 GT/s
- 5.0 GT/s

And there is:
  Note: Please refer to AR000035682 for information on link configurations and placements that are access limited by default,
        even though they are listed as supported in PG344. 

The XCVE2302 only has a single PCIe block at location X0Y0, so can't change the placement of the block.
The placement of the PCIe lanes is fixed in QUAD 103 by the PCB layout.

I.e. no option to change placements.

The answer record is at https://adaptivesupport.amd.com/s/article/000035682?language=en_US

The names of the IP are:
get_ips
VD100_enum_bufg_gt_sysclk_0 VD100_enum_gtwiz_versal_0_0 VD100_enum_pcie_0 VD100_enum_pcie_phy_0 VD100_enum_refclk_ibuf_0 VD100_enum_versal_cips_0_0 VD100_enum_xdma_0_0

Following AR000035682 allow all speeds:
set_property -dict [list CONFIG.all_speeds_all_sides {YES}] [get_ips VD100_enum_pcie_0]
set_property -dict [list CONFIG.all_speeds_all_sides {YES}] [get_ips VD100_enum_xdma_0_0]

After running the above in the TCL console changed both the VD100_enum_pcie_0 and VD100_enum_xdma_0_0 IP to be
a maximum speed of 8.0 GT/s with a x4 width.

