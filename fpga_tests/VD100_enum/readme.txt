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

Created the project by sourcing the gen2_x4/create_project.tcl.

After deleting the existing xdma_0 block the names of the IPs are:
get_ips
VD100_enum_versal_cips_0_0 VD100_enum_xdma_0_0

In the xdma_0 block increased the revision_id to 01.

Following AR000035682 allow all speeds:
set_property -dict [list CONFIG.all_speeds_all_sides {YES}] [get_ips VD100_enum_xdma_0_0]

In the xdma_0 block changed the maximum link speed to 8.0 GT/s

Attempted to "Run Block Automation" on the GUI for xmda_0, which has the description:
  "PCIe DMA Versal block automation configures the block "/xdma_0" and connects its clock and board interfaces"

This produced the following TCL console which failed with an error:
apply_bd_automation -rule xilinx.com:bd_rule:xdma -config { axi_strategy {max_data} link_speed {3} link_width {4} pl_pcie_cpm {PL-PCIE}}  [get_bd_cells xdma_0]
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_expansion_rom_type' from 'Expansion_ROM' to 'N/A' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_expansion_rom_type' from 'Expansion_ROM' to 'N/A' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_expansion_rom_type' from 'Expansion_ROM' to 'N/A' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_expansion_rom_type' from 'Expansion_ROM' to 'N/A' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_expansion_rom_size' from '1' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf3_expansion_rom_size' from '1' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf2_expansion_rom_size' from '1' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf1_expansion_rom_size' from '1' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_bar5_size' from '128' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_bar4_size' from '128' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_bar3_size' from '128' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'pf0_bar2_size' from '128' to '4' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'PF0_MSIX_CAP_TABLE_BIR' from 'BAR_0' to 'BAR_1' has been ignored for IP 'xdma_0_support/pcie'
WARNING: [IP_Flow 19-3374] An attempt to modify the value of disabled parameter 'PF0_MSIX_CAP_PBA_BIR' from 'BAR_0' to 'BAR_1' has been ignored for IP 'xdma_0_support/pcie'
ERROR: [IP_Flow 19-3461] Value '8.0_GT/s' is out of the range for parameter 'Max Link Speed(PL_LINK_CAP_MAX_LINK_SPEED)' for BD Cell 'xdma_0_support/pcie' . Valid values are - 2.5_GT/s, 5.0_GT/s
INFO: [IP_Flow 19-3438] Customization errors found on 'xdma_0_support/pcie'. Restoring to previous valid configuration.
ERROR: [Common 17-39] 'set_property' failed due to earlier errors.
ERROR: [BD 41-2168] Errors found in procedure apply_rule:
ERROR: [Common 17-39] 'set_property' failed due to earlier errors.

Can't determine the correct syntax to allow apply_bd_automation to change the CONFIG.all_speeds_all_sides on the block VD100_enum_pcie_0
which has not yet been created.

Therefore:
a. Re-created the project by sourcing the gen2_x4/create_project.tcl.
b. Following AR000035682 allow all speeds in both blocka:
     set_property -dict [list CONFIG.all_speeds_all_sides {YES}] [get_ips VD100_enum_pcie_0]
     set_property -dict [list CONFIG.all_speeds_all_sides {YES}] [get_ips VD100_enum_xdma_0_0]
c. Make the following changes:
   - In xdma set maximum link speed to 8 GT/s
   - In pcie set maximum link speed to 8 GT/s and maximum link width to X4
   - In xdma set revision_id to 01
   - In pcie set PF revsion_id to 01


In the HP Pavilion 590-p0053na desktop couldn't get the gen3_x4 design to enumerate.
Tried combinations of reloading the FPGA and rebooting the PC (to run the BIOS enumeration).

Most of the time after a reboot the PCI root port wasn't present

In one case the PCI root port was present, but apparently with a LnkSta of x16 width though the LnkCap was Width x8.
lsipci marked the width in LnkSta as "strange":
[mr_halfword@ryzen-alma ~]$ sudo lspci -nn -vvv -s 00:01.1
00:01.1 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] Raven/Raven2 PCIe GPP Bridge [6:0] [1022:15d3] (prog-if 00 [Normal decode])
	Control: I/O+ Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
	Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
	Latency: 0, Cache Line Size: 64 bytes
	Interrupt: pin ? routed to IRQ 26
	IOMMU group: 2
	Bus: primary=00, secondary=10, subordinate=10, sec-latency=0
	I/O behind bridge: 0000f000-00000fff [disabled]
	Memory behind bridge: fff00000-000fffff [disabled]
	Prefetchable memory behind bridge: 00000000fff00000-00000000000fffff [disabled]
	Secondary status: 66MHz- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- <SERR- <PERR-
	BridgeCtl: Parity- SERR+ NoISA- VGA- VGA16+ MAbort- >Reset- FastB2B-
		PriDiscTmr- SecDiscTmr- DiscTmrStat- DiscTmrSERREn-
	Capabilities: [50] Power Management version 3
		Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0+,D1-,D2-,D3hot+,D3cold+)
		Status: D3 NoSoftRst- PME-Enable+ DSel=0 DScale=0 PME-
	Capabilities: [58] Express (v2) Root Port (Slot+), MSI 00
		DevCap:	MaxPayload 512 bytes, PhantFunc 0
			ExtTag+ RBE+
		DevCtl:	CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+
			RlxdOrd+ ExtTag+ PhantFunc- AuxPwr- NoSnoop+
			MaxPayload 512 bytes, MaxReadReq 512 bytes
		DevSta:	CorrErr- NonFatalErr- FatalErr- UnsupReq- AuxPwr- TransPend-
		LnkCap:	Port #247, Speed 2.5GT/s, Width x8, ASPM L1, Exit Latency L1 <64us
			ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp+
		LnkCtl:	ASPM Disabled; RCB 64 bytes, Disabled- CommClk-
			ExtSynch- ClockPM- AutWidDis- BWInt- AutBWInt-
		LnkSta:	Speed 2.5GT/s (ok), Width x16 (strange)
			TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
		SltCap:	AttnBtn- PwrCtrl- MRL- AttnInd- PwrInd- HotPlug- Surprise-
			Slot #0, PowerLimit 0.000W; Interlock- NoCompl+
		SltCtl:	Enable: AttnBtn- PwrFlt- MRL- PresDet- CmdCplt- HPIrq- LinkChg-
			Control: AttnInd Unknown, PwrInd Unknown, Power- Interlock-
		SltSta:	Status: AttnBtn- PowerFlt- MRL- CmdCplt- PresDet- Interlock-
			Changed: MRL- PresDet- LinkState-
		RootCap: CRSVisible+
		RootCtl: ErrCorrectable- ErrNon-Fatal- ErrFatal- PMEIntEna+ CRSVisible+
		RootSta: PME ReqID 0000, PMEStatus- PMEPending-
		DevCap2: Completion Timeout: Range ABCD, TimeoutDis+ NROPrPrP- LTR+
			 10BitTagComp- 10BitTagReq- OBFF Not Supported, ExtFmt+ EETLPPrefix+, MaxEETLPPrefixes 1
			 EmergencyPowerReduction Not Supported, EmergencyPowerReductionInit-
			 FRS- LN System CLS Not Supported, TPHComp- ExtTPHComp- ARIFwd+
			 AtomicOpsCap: Routing- 32bit+ 64bit+ 128bitCAS-
		DevCtl2: Completion Timeout: 65ms to 210ms, TimeoutDis- LTR- OBFF Disabled, ARIFwd-
			 AtomicOpsCtl: ReqEn- EgressBlck-
		LnkCap2: Supported Link Speeds: 2.5-8GT/s, Crosslink- Retimer- 2Retimers- DRS-
		LnkCtl2: Target Link Speed: 2.5GT/s, EnterCompliance- SpeedDis+
			 Transmit Margin: Normal Operating Range, EnterModifiedCompliance- ComplianceSOS-
			 Compliance De-emphasis: -6dB
		LnkSta2: Current De-emphasis Level: -6dB, EqualizationComplete- EqualizationPhase1-
			 EqualizationPhase2- EqualizationPhase3- LinkEqualizationRequest-
			 Retimer- 2Retimers- CrosslinkRes: unsupported
	Capabilities: [a0] MSI: Enable+ Count=1/1 Maskable- 64bit+
		Address: 00000000fee00000  Data: 0000
	Capabilities: [c0] Subsystem: Hewlett-Packard Company Device [103c:8433]
	Capabilities: [c8] HyperTransport: MSI Mapping Enable+ Fixed+
	Capabilities: [100 v1] Vendor Specific Information: ID=0001 Rev=1 Len=010 <?>
	Capabilities: [150 v2] Advanced Error Reporting
		UESta:	DLP- SDES- TLP- FCP- CmpltTO- CmpltAbrt- UnxCmplt- RxOF- MalfTLP- ECRC- UnsupReq- ACSViol-
		UEMsk:	DLP- SDES- TLP- FCP- CmpltTO- CmpltAbrt- UnxCmplt- RxOF- MalfTLP- ECRC- UnsupReq- ACSViol-
		UESvrt:	DLP+ SDES+ TLP- FCP+ CmpltTO- CmpltAbrt- UnxCmplt- RxOF+ MalfTLP+ ECRC- UnsupReq- ACSViol-
		CESta:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr-
		CEMsk:	RxErr- BadTLP- BadDLLP- Rollover- Timeout- AdvNonFatalErr+
		AERCap:	First Error Pointer: 00, ECRCGenCap- ECRCGenEn- ECRCChkCap- ECRCChkEn-
			MultHdrRecCap- MultHdrRecEn- TLPPfxPres- HdrLogCap-
		HeaderLog: 00000000 00000000 00000000 00000000
		RootCmd: CERptEn+ NFERptEn+ FERptEn+
		RootSta: CERcvd- MultCERcvd- UERcvd- MultUERcvd-
			 FirstFatal- NonFatalMsg- FatalMsg- IntMsg 0
		ErrorSrc: ERR_COR: 0000 ERR_FATAL/NONFATAL: 0000
	Capabilities: [270 v1] Secondary PCI Express
		LnkCtl3: LnkEquIntrruptEn- PerformEqu-
		LaneErrStat: 0
	Capabilities: [2a0 v1] Access Control Services
		ACSCap:	SrcValid+ TransBlk+ ReqRedir+ CmpltRedir+ UpstreamFwd+ EgressCtrl- DirectTrans+
		ACSCtl:	SrcValid+ TransBlk- ReqRedir+ CmpltRedir+ UpstreamFwd+ EgressCtrl- DirectTrans-
	Capabilities: [370 v1] L1 PM Substates
		L1SubCap: PCI-PM_L1.2+ PCI-PM_L1.1+ ASPM_L1.2+ ASPM_L1.1+ L1_PM_Substates+
			  PortCommonModeRestoreTime=0us PortTPowerOnTime=10us
		L1SubCtl1: PCI-PM_L1.2- PCI-PM_L1.1- ASPM_L1.2- ASPM_L1.1-
			   T_CommonMode=0us LTR1.2_Threshold=0ns
		L1SubCtl2: T_PwrOn=10us
	Kernel driver in use: pcieport

In this state in the Vivado hardware manager:
a. The PCIe Debugger showed alternating between only the Detect and Polling states.
b. The IBERT Versal GTYP showed all 4 lanes at 2.5 Gbps.


PG344 Versal Adaptive SoC DMA and Bridge Subsystem for PCI Express v2.0 Product Guide contains:
  Note: If changes are made to the PCI Express core block, the supporting GT Wizard and PHY IPs must be deleted,
        and the block automation steps should be repeated.

For the above failed design, within the xdma_0_support diagram had only increased the maximum link speed in the pcie block.

The pcie_phy block also has a maximum link speed which was still 5.0 GT/s.
Changed the maximum link speed for the pcie_phy block to 8.0 GT/s

In the gtwiz_versal_0 block can't see any setting related to the maximum link speed.


With the above changes the design now enumerated in the HP Pavilion 590-p0053na desktop, following a reboot.
The 590-p0053na BIOS doesn't have an option to enable PCIe hot-plug in the slot so have to reboot to
run the BIOS enumeration.

display_identified_pcie_fpga_designs finds the expected design:
[mr_halfword@ryzen-alma ~]$ cd ~/fpga_sio/software_tests/eclipse_project/bin/release/
[mr_halfword@ryzen-alma release]$ identify_pcie_fpga_design/display_identified_pcie_fpga_designs 
Opening device 0000:10:00.0 (10ee:b034) with IOMMU group 10
Enabled bus master for 0000:10:00.0

Design VD100_enum:
  PCI device 0000:10:00.0 rev 01 IOMMU group 10
  DMA bridge bar 1 memory size 0x1000
  Channel ID  addr_alignment  len_granularity  num_address_bits
       H2C 0               1                1                64
       C2H 0               1                1                64

dump_pci_info_pciutils shows has negotiated the expected "Current speed 8 GT/s Width x4":
[mr_halfword@ryzen-alma release]$ dump_info/dump_pci_info_pciutils 
domain=0000 bus=10 dev=00 func=00 rev=01
  vendor_id=10ee (Xilinx Corporation) device_id=b034 (Device b034) subvendor_id=0002 subdevice_id=0020
  iommu_group=10
  driver=vfio-pci
  control: I/O- Mem+ BusMaster- ParErr- SERR- DisINTx-
  status: INTx- <ParErr- >TAbort- <TAbort- <MAbort- >SERR- DetParErr-
  bar[0] base_addr=fd010000 size=1000 is_IO=0 is_prefetchable=0 is_64=0
  bar[1] base_addr=fd000000 size=10000 is_IO=0 is_prefetchable=0 is_64=0
  Capabilities: [40] Power Management
  Capabilities: [70] PCI Express v2 Express Endpoint, MSI 0
    Link capabilities: Max speed 8 GT/s Max width x4
    Negotiated link status: Current speed 8 GT/s Width x4
    Link capabilities2: Supported link speeds 2.5 GT/s 5.0 GT/s 8.0 GT/s
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
  domain=0000 bus=00 dev=01 func=01 rev=00
    vendor_id=1022 (Advanced Micro Devices, Inc. [AMD]) device_id=15d3 (Raven/Raven2 PCIe GPP Bridge [6:0])
    iommu_group=2
    driver=pcieport
    control: I/O+ Mem+ BusMaster+ ParErr- SERR- DisINTx+
    status: INTx- <ParErr- >TAbort- <TAbort- <MAbort- >SERR- DetParErr-
    Capabilities: [50] Power Management
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


Output from the hardware manager for the PCIe debugger. There had been 4 hot resets following running two VFIO programs:
report_hw_pcie PCIe_0

-------------------------------------------
PCIe_0 Status
-------------------------------------------
First Quad     -
Link Info      -Gen3x4
Memory Size    -1024
Memory Type    -0
UUID           -3C87E4367BC552C89A958FDD0942F2AF


-------------------------------------------
LTSSM Data
-------------------------------------------
Configuration    -Visited
Detect           -Visited
Disabled         -Not Visited
E.Lock           -Visited
Hot Reset        -Visited
L0               -Visited
L0S              -Not Visited
L1               -Last Visited
Loopback         -Not Visited
Polling          -Visited
R.Cfg            -Visited
R.Eq             -Visited
R.Idle           -Visited
R.Speed          -Visited

Trace
---------
 LOOP (3) [DETECT [DETECT.QUIET (0X00)
  DETECT.ACTIVE (0X01)]
  POLLING [POLLING.ACTIVE (0X02)
  POLLING.COMPLIANCE (0X03)]]
 DETECT [DETECT.QUIET (0X00)
  DETECT.ACTIVE (0X01)]
 POLLING [POLLING.ACTIVE (0X02)
  POLLING.CONFIGURATION (0X04)]
 CONFIGURATION [CONFIGURATION.LINKWIDTH.START (0X05)
  CONFIGURATION.LINKWIDTH.ACCEPT (0X06)
  CONFIGURATION.LANENUM.WAIT (0X08)
  CONFIGURATION.LANENUM.ACCEPT (0X07)
  CONFIGURATION.COMPLETE (0X09)
  CONFIGURATION.IDLE (0X0A)]
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.SPEED (0X2C)
 R.LOCK (0X0B)
 R.EQ [R.EQ.PHASE0 (0X28)
  R.EQ.PHASE1 (0X29)
  R.EQ.PHASE2 (0X2A)
  R.EQ.PHASE3 (0X2B)]
 LOOP (4) [R.LOCK (0X0B)
  R.CFG (0X0D)
  R.IDLE (0X0E)
  L0 (0X10)]
 LOOP (68) [L1 [L1.ENTRY (0X17)
  L1.IDLE (0X18)]
  R.LOCK (0X0B)
  R.CFG (0X0D)
  R.IDLE (0X0E)]
 L1 [L1.ENTRY (0X17)
  L1.IDLE (0X18)]
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 HOTRESET (0X27)
 DETECT [DETECT.QUIET (0X00)
  DETECT.ACTIVE (0X01)]
 POLLING [POLLING.ACTIVE (0X02)
  POLLING.CONFIGURATION (0X04)]
 CONFIGURATION [CONFIGURATION.LINKWIDTH.START (0X05)
  CONFIGURATION.LINKWIDTH.ACCEPT (0X06)
  CONFIGURATION.LANENUM.WAIT (0X08)
  CONFIGURATION.LANENUM.ACCEPT (0X07)
  CONFIGURATION.COMPLETE (0X09)
  CONFIGURATION.IDLE (0X0A)]
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.SPEED (0X2C)
 R.LOCK (0X0B)
 R.EQ [R.EQ.PHASE0 (0X28)
  R.EQ.PHASE1 (0X29)
  R.EQ.PHASE2 (0X2A)
  R.EQ.PHASE3 (0X2B)]
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 HOTRESET (0X27)
 DETECT [DETECT.QUIET (0X00)
  DETECT.ACTIVE (0X01)]
 POLLING [POLLING.ACTIVE (0X02)
  POLLING.CONFIGURATION (0X04)]
 CONFIGURATION [CONFIGURATION.LINKWIDTH.START (0X05)
  CONFIGURATION.LINKWIDTH.ACCEPT (0X06)
  CONFIGURATION.LANENUM.WAIT (0X08)
  CONFIGURATION.LANENUM.ACCEPT (0X07)
  CONFIGURATION.COMPLETE (0X09)
  CONFIGURATION.IDLE (0X0A)]
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.SPEED (0X2C)
 R.LOCK (0X0B)
 R.EQ [R.EQ.PHASE0 (0X28)
  R.EQ.PHASE1 (0X29)
  R.EQ.PHASE2 (0X2A)
  R.EQ.PHASE3 (0X2B)]
 LOOP (5) [R.LOCK (0X0B)
  R.CFG (0X0D)
  R.IDLE (0X0E)
  L0 (0X10)
  L1 [L1.ENTRY (0X17)
  L1.IDLE (0X18)]]
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 HOTRESET (0X27)
 DETECT [DETECT.QUIET (0X00)
  DETECT.ACTIVE (0X01)]
 POLLING [POLLING.ACTIVE (0X02)
  POLLING.CONFIGURATION (0X04)]
 CONFIGURATION [CONFIGURATION.LINKWIDTH.START (0X05)
  CONFIGURATION.LINKWIDTH.ACCEPT (0X06)
  CONFIGURATION.LANENUM.WAIT (0X08)
  CONFIGURATION.LANENUM.ACCEPT (0X07)
  CONFIGURATION.COMPLETE (0X09)
  CONFIGURATION.IDLE (0X0A)]
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.SPEED (0X2C)
 R.LOCK (0X0B)
 R.EQ [R.EQ.PHASE0 (0X28)
  R.EQ.PHASE1 (0X29)
  R.EQ.PHASE2 (0X2A)
  R.EQ.PHASE3 (0X2B)]
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.IDLE (0X0E)
 HOTRESET (0X27)
 DETECT [DETECT.QUIET (0X00)
  DETECT.ACTIVE (0X01)]
 POLLING [POLLING.ACTIVE (0X02)
  POLLING.CONFIGURATION (0X04)]
 CONFIGURATION [CONFIGURATION.LINKWIDTH.START (0X05)
  CONFIGURATION.LINKWIDTH.ACCEPT (0X06)
  CONFIGURATION.LANENUM.WAIT (0X08)
  CONFIGURATION.LANENUM.ACCEPT (0X07)
  CONFIGURATION.COMPLETE (0X09)
  CONFIGURATION.IDLE (0X0A)]
 L0 (0X10)
 R.LOCK (0X0B)
 R.CFG (0X0D)
 R.SPEED (0X2C)
 R.LOCK (0X0B)
 R.EQ [R.EQ.PHASE0 (0X28)
  R.EQ.PHASE1 (0X29)
  R.EQ.PHASE2 (0X2A)
  R.EQ.PHASE3 (0X2B)]
 LOOP (24) [R.LOCK (0X0B)
  R.CFG (0X0D)
  R.IDLE (0X0E)
  L0 (0X10)
  L1 [L1.ENTRY (0X17)
  L1.IDLE (0X18)]]

