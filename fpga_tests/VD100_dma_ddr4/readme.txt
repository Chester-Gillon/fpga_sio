This design is to test accessing the DDR4 4 GB of memory via the DMA/Bridge Subsystem for PCI Express.

The CIPS block has been configured as design flow "PL Subsystem".

The AXI NoC is configured as the DDR4 controller, with a slave AXI port connected to M_AXI port of the PCIe DMA/Bridge.
This AXI connection is specified by the DMA/Bridge PCIe configuration of 16 GT/s x4 which results in:
- 64 bit address
- 256 bit data
- 250 MHz clock
- Peak bandwidth of 8000 MB/s

The DDR4 memory is four Micron MT40A512M16LY-062E.
With a 3200 MHz data rate and 64 bits peak bandwidth of 25600 MB/s.

For the AXI NoC configuration:
1. On General tab:
   a. AXI Interfaces:
      - Number of AXI Slave Interfaces : 1 (from the DMA/Bridge)
      - Number of AXI Master Interfaces : 0
      - Number of AXI Clocks : 1 (from the DMA/Bridge)
   b. Inter-NOC Interfaces:
      - Number of Inter-NOC Slave Interfaces : 0
      - Number of Inter-NOC Master Interfaces : 0
   c. Memory Controllers - DDR4/LPDDR4:
      - Memory Controller : Single Memory Controller
      - Number of Memory Controller Ports : 1
      - Interleave Size in Bytes : Greyed out
      - DDR Address Region 0 : DDR LOW1 (32G starting from 0x8_0000_0000 as the lowest contiguous region which fits 4GB.
                                         DMA from the PCIe DMA/Bridge will have to start from this address).
      - DDR Address Region 1 : NONE
2. Inputs tab:
   a. AXI Inputs : One input from PL. Parity None since not used by the PCIe DMA/Bridge.
   b. Inter-NoC Inputs : None
3. Outputs tab:
   a. AXI Outputs : None
   b. Inter-NoC Outputs : None
4. Connectivity tab:
   a. S00_AXI pl connected to MC port 0
5. QoS tab:
   a. Set Read and Write Traffic Classes : ISOCHRONOUS 
       (since described as a mechanism to guarantee the maximum latency of DDR memory traffic)
   b. For MC Port 0 Bandwidth Read and Bandwidth Write:
      - Attempted to set both for 25600 MB/s which is the peak DDR4 memory bandwidth,
        but the GUI limited the values at 17280 MB/s.
      - Attempting to validate the block diagram with the bandwidth values as 17280 MB/s resulted in errors:
          [Ipconfig 75-886] Total Bandwidth requirement from instance axi_noc_0/inst/S00_AXI_nmu  is 21600 which is greater than max bandwidth 15360.
          The causes for exceeding the max bandwidth include packetization overhead, small or inefficient burst lengths, PL to NoC data width, 
          and unrealistic bandwidth requests. Please review NoC performance tuning information contained within
          PG406: Versal ACAP Programmable NoC and Integrated Memory Controller.
          Violating nets and total required bandwidth including packetization overhead for the axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc path are:
            axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc: 2160 MB/s; Isochronous; READ REQUEST
            axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc: 19440 MB/s; (Requested: 17280 MB/s); Isochronous; WRITE

         [Ipconfig 75-885] Total Bandwidth requirement to instance axi_noc_0/inst/S00_AXI_nmu  is 19440 which is greater than max bandwidth 15360.
         The causes for exceeding the max bandwidth include packetization overhead, small or inefficient burst lengths, PL to NoC data width, 
         and unrealistic bandwidth requests. Please review NoC performance tuning information contained within
         PG406: Versal ACAP Programmable NoC and Integrated Memory Controller.
         Violating nets and total required bandwidth including packetization overhead for the axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc path are:
           axi_noc_0/inst/MC0_ddrc to axi_noc_0/inst/S00_AXI_nmu: 17280 MB/s; (Requested: 17280 MB/s); Isochronous; READ
           axi_noc_0/inst/MC0_ddrc to axi_noc_0/inst/S00_AXI_nmu: 2160 MB/s; Isochronous; WRITE RESPONSE

         [Ipconfig 75-885] Total Bandwidth requirement to instance axi_noc_0/inst/MC0_ddrc  on PORT0 is 21600 which is greater than max bandwidth 15360.
         The causes for exceeding the max bandwidth include packetization overhead, small or inefficient burst lengths, PL to NoC data width, 
         and unrealistic bandwidth requests. Please review NoC performance tuning information contained within
         PG406: Versal ACAP Programmable NoC and Integrated Memory Controller.
         Violating nets and total required bandwidth including packetization overhead for the axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc path are:
           axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc: 2160 MB/s; Isochronous; READ REQUEST
           axi_noc_0/inst/S00_AXI_nmu to axi_noc_0/inst/MC0_ddrc: 19440 MB/s; (Requested: 17280 MB/s); Isochronous; WRITE

         [BD 41-1783] NOC Compiler failed. 
      - Changing both traffic classes to Best Effort didn't prevent the errors.
      - https://adaptivesupport.amd.com/s/article/000038333?language=en_US
        "000038333 - 2025.1 Vitis - Total Bandwidth requirement from instance is greater than max bandwidth" is about a similar error but on the "from"
        rather than "to" direction:
           ERROR: [VPL 75-886] Total Bandwidth requirement from instance NoC_C0/inst/MC0_ddrc on PORT0 is 16250 which is greater than max bandwidth 15344

        000038333 has a patch AR000038333_vivado_2025_1_preliminary_rev1.zip but not clear if the patch is needed / compatible with
        Vivado 2025.2
      - If reduce the Read Bandwidth and Write Bandwidth to 12250 MB/s then the errors no longer occur.
        Tried different values = 12500 MB/s still had errors.
      - 12250 MB/s is still more than the peak 8000 MB/s on PCIe DMA Bridge.
6. Address Remap tab:
   a. None
7. DDR Basic tab:
   a. Controller Selection:
      - Controller type : DDR4 SDRAM
   b. Clocking:
      - Clock Selection : System Clock 
        While the PG313 example shows using the internal HSM1 reference clock generated by the CIPS, with the CIPS design flow
        set to "PL Subsystem" the CIPS NoC -> DDRM3 HSM1 Clock Port option isn't available.
      - Input System Clock Period (ps) : 5000 
        Since the VD100 has a 200 MHz differential clock for the memory controller.
      - Memory Clock Period : 625 (1600.0MHz)
      - System Clock : Differential
      - Enable Internal Responder : Unticked (since only for simulation)
8. DDR Memory tab:
   a. DDR Memory Options:
      - Device Type : Components
      - Speed Bin (Monolithic/3DS) : DDR4-3200AA(22-22-22)
        The MT40A512M16 datasheet says the -062E speed grade is a "Data Rate (MT/s)" of 3200 with "Target CL-nRCD-nRP" of 22-22-22
      - Based Component Width : x16
        Since the MT40A512M16 is a "512 Meg x 16" configuration.
      - Row Address Width : 16
        As specified by the MT40A512M16 datasheet
      - Bank Group Width : 1 (greyed out)
      - Number of Channels : Single
        Since the DDR4 on the VD100 is connected as a single memory channel.
      - Bank Address Width : 2 (greyed out)
      - Data Width per Channel (including ECC bits if enabled) : 64
      - Ranks : 1
      - Stack Height : 1 (since not a 3DS device)
      - Slot : Single (greyed out since using components)
      - Number of Memory Clocks : 1 (greyed out)
      - ECC : unticked (greyed out)
      - Write DBI : DM NO DBI (since want byte masks to make the memory byte accessible)
      - Read DBI : Unticked
        While Read DBI can be enabled when byte masks are used, 
        https://www.xilinx.com/publications/events/designcon/2016/paper-optimalddr4systemwithdata-to.pdf suggests the use case is power reduction
        and improved signal integrity.
      - Channel Interleaving : Unticked (greyed out)
      - DRAM Command/Address Parity : Unticked
        The VD100 hasn't connected the PAR nor RESET_B of the MT40A512M16LY-062E devices to the Versal.
      - CA Mirror : Unticked (greyed out)
      - Clamshell : Unticked
   b. Future Expansion for PCB Designs
      - MC0 Pinout : Optimum
        Since the PCB design is fixed.
   c. Flipped pinout
      - MC0 Flipped pinout : Unticked
        Since the PCB design is fixed.
   d. Mode Register Settings:
      - CAS Latency (nCLK) : 22
        This is the default value set when the Speed Bin was selected, and matches the value in the datasheet.
      - CAS Write Latency (nCK) : 20
        The default value set when the Speed Bin was selected was 16, which is the datasheet value for 1tCK WRITE preamble.
        For component configurations "2T Timing" is fixed as enabled. Therefore set CAS Write Latency as 20 which is the
        datasheet value for 2tCK WRITE preamble.
   e. Timing Paramters:
      - The default values populated when the Speed Bin was selected match the datasheet with notes:
        - tRFC is for tRFC1 (as configured in DDR advanced)
        - tREFI is for -40°C ≤ TC ≤ 85°C temperature range
9. DDR Address Mappings Options tab:
   - Use the Pre defined address map "ROW BANK COLUMN BGO". The DRAM Address Mapping section of PG313 explains this is
     "Row-bank-column with bank group optimization" which is optimised Single-Thread Linear Read Address Mapping.
10. DDR Advanced tab:
    a. 2T Timing : Enabled (greyed out)
    b. ECC Options:
       - All unticked and greyed out
    c. Refresh Options:
       - Enable Refresh and Periodic Calibration Interface : Unticked
         No need to manually control from user logic
       - Fixed fine Granularity Refresh : 1x
         Leave at default. Not sure of the performance benefit of changing, for which tRFC and tREFI in the timing parameters
         would need to change.
   d. Power saving options:
      - Idle time to enter power down mode (nCK) : 0x00000AA
        Leave at default. Not sure of the benefit of changing.
   e. Migration Options:
      - Enable Migration : Unticked
        Not needed, as won't be migrating the design to a different SoC device.
   f. Startup Options:
      - DDRMC Calibration Status at Startup : ENABLE
        So that DONE remains de-asserted at startup if the calibration fails.
        The design isn't usable if the calibration fails.

