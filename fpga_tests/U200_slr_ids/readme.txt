This was created to investigate for the SSI xcu200-fsgd2104-2-e device if identities could be read from all SLRs,
and if so they returned the same identification.

For USR_ACCESSE2:
1. Initially tried 4 instances, and expected to get an error about trying use too many instances since the U200 has 3 SLRs.
   Got an error during implementation that only 3 instances were available (omitted to save the exact error message).
2. Reduced the number of instances to 3. The implementation then reported the error:
     [Place 30-938] Cell U200_slr_ids_i/usr_accesse2_read_2/inst/USR_ACCESSE2_inst cannot be LOCed in slave SLR site.
     USR_ACCESSE2 can only use Master SLR sites.
3. Therefore, changed to have only 1 instance.


Was able to use 3 instances of DNA_PORTE2, which shows up in the U200_slr_ids_wrapper_utilization_placed.rpt as:
8. CONFIGURATION
----------------

+-------------+------+-------+------------+-----------+--------+
|  Site Type  | Used | Fixed | Prohibited | Available |  Util% |
+-------------+------+-------+------------+-----------+--------+
| BSCANE2     |    0 |     0 |          0 |        12 |   0.00 |
| DNA_PORTE2  |    3 |     0 |          0 |         3 | 100.00 |
| EFUSE_USR   |    0 |     0 |          0 |         3 |   0.00 |
| FRAME_ECCE4 |    0 |     0 |          0 |         3 |   0.00 |
| ICAPE3      |    0 |     0 |          0 |         6 |   0.00 |
| MASTER_JTAG |    0 |     0 |          0 |         3 |   0.00 |
| STARTUPE3   |    0 |     0 |          0 |         3 |   0.00 |
+-------------+------+-------+------------+-----------+--------+


A test shows the 3 DNA_PORTE2 instances return different values:
linux@DESKTOP-BVUMP11:~/fpga_sio/software_tests/eclipse_project/bin/release> identify_pcie_fpga_design/display_identified_pcie_fpga_designs 
Opening device 0000:31:00.0 (10ee:903f) with IOMMU group 22
Enabled bus master for 0000:31:00.0

Design U200_slr_ids:
  PCI device 0000:31:00.0 rev 00 IOMMU group 22  physical slot 2-2

  DMA bridge bar 1 memory base offset 0x0 size 0x1000
  Channel ID  addr_alignment  len_granularity  num_address_bits
       H2C 0               1                1                64
       C2H 0               1                1                64
  User access build timestamp : 9BB4B5A0 - 19/07/2026 11:22:32
  UltraScale DNA[0]: 40020000013A736625802305
  UltraScale DNA[1]: 4002000001299E251CD06345
  UltraScale DNA[2]: 40020000013A736625908245


There were location properties enforces for which SLR the DNA_PORTE2 were used on. After the implementation can see the SLRs
don't match the instances:
get_property SLR_INDEX [get_cells U200_slr_ids_i/AXI_DNA_v1_0_0/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
1
get_property SLR_INDEX [get_cells U200_slr_ids_i/AXI_DNA_v1_0_1/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
0
get_property SLR_INDEX [get_cells U200_slr_ids_i/AXI_DNA_v1_0_2/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
2

And the actual locations:
get_property LOC [get_cells U200_slr_ids_i/AXI_DNA_v1_0_0/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
CONFIG_SITE_X0Y1
get_property LOC [get_cells U200_slr_ids_i/AXI_DNA_v1_0_1/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
CONFIG_SITE_X0Y0
get_property LOC [get_cells U200_slr_ids_i/AXI_DNA_v1_0_2/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
CONFIG_SITE_X0Y2


The SLR index and location of the USR_ACCESSE2, which as above can only be placed on the "Master SLR Site"
get_property SLR_INDEX [get_cells U200_slr_ids_i/usr_accesse2_read_0/inst/USR_ACCESSE2_inst]
1
get_property LOC [get_cells U200_slr_ids_i/usr_accesse2_read_0/inst/USR_ACCESSE2_inst]
CONFIG_SITE_X0Y1


The SLR_INDEX property is read-only. Therefore, used constraints to set the LOC of the DNA_PORTE2 instances.
After setting the LOC properties, the SLR_INDEX read back as the same DNA_PORTE2 instance:
get_property SLR_INDEX [get_cells U200_slr_ids_i/AXI_DNA_v1_0_0/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
0
get_property SLR_INDEX [get_cells U200_slr_ids_i/AXI_DNA_v1_0_1/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
1
get_property SLR_INDEX [get_cells U200_slr_ids_i/AXI_DNA_v1_0_2/inst/AXI_DNA_v1_0_S00_AXI_inst/DNA_i/DNA_PORTE2_inst]
2


Following adding the constrains, the DNA for indices 0 and 1 have swapped:
linux@DESKTOP-BVUMP11:~/fpga_sio/software_tests/eclipse_project/bin/release> identify_pcie_fpga_design/display_identified_pcie_fpga_designs 
Opening device 0000:31:00.0 (10ee:903f) with IOMMU group 22
Enabled bus master for 0000:31:00.0

Design U200_slr_ids:
  PCI device 0000:31:00.0 rev 00 IOMMU group 22  physical slot 2-2

  DMA bridge bar 1 memory base offset 0x0 size 0x1000
  Channel ID  addr_alignment  len_granularity  num_address_bits
       H2C 0               1                1                64
       C2H 0               1                1                64
  User access build timestamp : 9BB4F3CD - 19/07/2026 15:15:13
  UltraScale DNA[0]: 4002000001299E251CD06345
  UltraScale DNA[1]: 40020000013A736625802305
  UltraScale DNA[2]: 40020000013A736625908245

