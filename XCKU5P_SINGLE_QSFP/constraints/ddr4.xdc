# The constrains for the DDR4 interface were:
# a. Copied from the example file XCKU5P-FFVB676/Demo/KU5P_DDR4_TEST/IO_KU5P.xdc,
#    and the comments translated from Simplified Chinese.
# b. Checked against the XCKU5P-FFVB676.pdf schematic.
#
# The following signals are connected from the DDR4 components to FPGA pins, but not driven by the FPGA:
# - DDR4_PAR on pin B26. Schematic shows pull-up to VTT.
# - DDR4_TEN on pin E23. Schematic shows pull-down to GND.
# - DDR4_ALERT on pin D23. Schematic shows pull-up to 1.2V. 

#clock ddr OK
set_property -dict {PACKAGE_PIN H23 IOSTANDARD DIFF_POD12_DCI} [get_ports c0_sys_clk_clk_p]
set_property -dict {PACKAGE_PIN H24 IOSTANDARD DIFF_POD12_DCI} [get_ports c0_sys_clk_clk_n]
#ddr4----------------------------------------------------------------------
#-----------------------------BG0-------------------------------
set_property PACKAGE_PIN L22 [get_ports {c0_ddr4_bg[0]}]
#-------------------------DDR4 Terminal resistor enable---------------
set_property PACKAGE_PIN E26 [get_ports {c0_ddr4_odt[0]}]
#-----------------------------address line----------------------------
set_property PACKAGE_PIN D25 [get_ports {c0_ddr4_adr[0]}]
set_property PACKAGE_PIN J23 [get_ports {c0_ddr4_adr[1]}]
set_property PACKAGE_PIN C26 [get_ports {c0_ddr4_adr[2]}]
set_property PACKAGE_PIN J26 [get_ports {c0_ddr4_adr[3]}]
set_property PACKAGE_PIN E25 [get_ports {c0_ddr4_adr[4]}]
set_property PACKAGE_PIN G26 [get_ports {c0_ddr4_adr[5]}]
set_property PACKAGE_PIN K23 [get_ports {c0_ddr4_adr[6]}]
set_property PACKAGE_PIN D24 [get_ports {c0_ddr4_adr[7]}]
set_property PACKAGE_PIN K26 [get_ports {c0_ddr4_adr[8]}]
set_property PACKAGE_PIN F24 [get_ports {c0_ddr4_adr[9]}]
set_property PACKAGE_PIN D26 [get_ports {c0_ddr4_adr[10]}]
set_property PACKAGE_PIN J24 [get_ports {c0_ddr4_adr[11]}]
set_property PACKAGE_PIN H26 [get_ports {c0_ddr4_adr[12]}]
set_property PACKAGE_PIN B25 [get_ports {c0_ddr4_adr[13]}]

#------------------------WE  CAS  RAS-------------------------
set_property PACKAGE_PIN M25 [get_ports {c0_ddr4_adr[14]}]
set_property PACKAGE_PIN F23 [get_ports {c0_ddr4_adr[15]}]
set_property PACKAGE_PIN H22 [get_ports {c0_ddr4_adr[16]}]
#-----------------------Pulse selection------------------------
set_property PACKAGE_PIN N23 [get_ports {c0_ddr4_dqs_t[3]}]
set_property PACKAGE_PIN P23 [get_ports {c0_ddr4_dqs_c[3]}]
set_property PACKAGE_PIN U26 [get_ports {c0_ddr4_dqs_t[2]}]
set_property PACKAGE_PIN V26 [get_ports {c0_ddr4_dqs_c[2]}]
set_property PACKAGE_PIN W25 [get_ports {c0_ddr4_dqs_t[1]}]
set_property PACKAGE_PIN W26 [get_ports {c0_ddr4_dqs_c[1]}]
set_property PACKAGE_PIN V21 [get_ports {c0_ddr4_dqs_t[0]}]
set_property PACKAGE_PIN V22 [get_ports {c0_ddr4_dqs_c[0]}]
#--------------------------data---------------------------
set_property PACKAGE_PIN W19 [get_ports {c0_ddr4_dq[0]}]
set_property PACKAGE_PIN T20 [get_ports {c0_ddr4_dq[1]}]
set_property PACKAGE_PIN U20 [get_ports {c0_ddr4_dq[2]}]
set_property PACKAGE_PIN U22 [get_ports {c0_ddr4_dq[3]}]
set_property PACKAGE_PIN W20 [get_ports {c0_ddr4_dq[4]}]
set_property PACKAGE_PIN T23 [get_ports {c0_ddr4_dq[5]}]
set_property PACKAGE_PIN U21 [get_ports {c0_ddr4_dq[6]}]
set_property PACKAGE_PIN T22 [get_ports {c0_ddr4_dq[7]}]
set_property PACKAGE_PIN Y25 [get_ports {c0_ddr4_dq[8]}]
set_property PACKAGE_PIN V24 [get_ports {c0_ddr4_dq[9]}]
set_property PACKAGE_PIN AA25 [get_ports {c0_ddr4_dq[10]}]
set_property PACKAGE_PIN W23 [get_ports {c0_ddr4_dq[11]}]
set_property PACKAGE_PIN AA24 [get_ports {c0_ddr4_dq[12]}]
set_property PACKAGE_PIN V23 [get_ports {c0_ddr4_dq[13]}]
set_property PACKAGE_PIN Y26 [get_ports {c0_ddr4_dq[14]}]
set_property PACKAGE_PIN W24 [get_ports {c0_ddr4_dq[15]}]
set_property PACKAGE_PIN T25 [get_ports {c0_ddr4_dq[16]}]
set_property PACKAGE_PIN N24 [get_ports {c0_ddr4_dq[17]}]
set_property PACKAGE_PIN P24 [get_ports {c0_ddr4_dq[18]}]
set_property PACKAGE_PIN P26 [get_ports {c0_ddr4_dq[19]}]
set_property PACKAGE_PIN U25 [get_ports {c0_ddr4_dq[20]}]
set_property PACKAGE_PIN P25 [get_ports {c0_ddr4_dq[21]}]
set_property PACKAGE_PIN R26 [get_ports {c0_ddr4_dq[22]}]
set_property PACKAGE_PIN R25 [get_ports {c0_ddr4_dq[23]}]
set_property PACKAGE_PIN P20 [get_ports {c0_ddr4_dq[24]}]
set_property PACKAGE_PIN N19 [get_ports {c0_ddr4_dq[25]}]
set_property PACKAGE_PIN P21 [get_ports {c0_ddr4_dq[26]}]
set_property PACKAGE_PIN N21 [get_ports {c0_ddr4_dq[27]}]
set_property PACKAGE_PIN R21 [get_ports {c0_ddr4_dq[28]}]
set_property PACKAGE_PIN P19 [get_ports {c0_ddr4_dq[29]}]
set_property PACKAGE_PIN R20 [get_ports {c0_ddr4_dq[30]}]
set_property PACKAGE_PIN N22 [get_ports {c0_ddr4_dq[31]}]


#------------------------CS---------------------------------------
set_property PACKAGE_PIN J25 [get_ports {c0_ddr4_cs_n[0]}]
# ---------------------output ddr4 Differential clock-------------
set_property PACKAGE_PIN G24 [get_ports {c0_ddr4_ck_t[0]}]
set_property PACKAGE_PIN G25 [get_ports {c0_ddr4_ck_c[0]}]
#----------------------------CKE----------------------------------
set_property PACKAGE_PIN K22 [get_ports {c0_ddr4_cke[0]}]
#---------------------------RESET---------------------------------
set_property PACKAGE_PIN K25 [get_ports c0_ddr4_reset_n]
#--------------------------ACT-------------------------------------
set_property PACKAGE_PIN F25 [get_ports c0_ddr4_act_n]
#-------------------------BA0  BA1---Bank address-----------------
set_property PACKAGE_PIN L23 [get_ports {c0_ddr4_ba[0]}]
set_property PACKAGE_PIN F22 [get_ports {c0_ddr4_ba[1]}]
#---------------------   DML   DMU---data mask_-------------------
set_property PACKAGE_PIN U19 [get_ports {c0_ddr4_dm_n[0]}]
set_property PACKAGE_PIN Y22 [get_ports {c0_ddr4_dm_n[1]}]
set_property PACKAGE_PIN T24 [get_ports {c0_ddr4_dm_n[2]}]
set_property PACKAGE_PIN R22 [get_ports {c0_ddr4_dm_n[3]}]

#--------------------------Set level standard---------------------

set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dm_n[0]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dm_n[1]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dm_n[2]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dm_n[3]}]

set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[31]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[30]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[29]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[28]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[27]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[26]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[25]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[24]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[23]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[22]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[21]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[20]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[19]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[18]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[17]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[16]}]

set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[15]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[14]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[13]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[12]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[11]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[10]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[9]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[8]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[7]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[6]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[5]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[4]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[3]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[2]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[1]}]
set_property IOSTANDARD POD12_DCI [get_ports {c0_ddr4_dq[0]}]
set_property IOSTANDARD SSTL12_DCI [get_ports {c0_ddr4_odt[0]}]
set_property INTERNAL_VREF 0.6 [get_iobanks 66]
set_property INTERNAL_VREF 0.6 [get_iobanks 65]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_t[0]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_c[0]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_t[1]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_c[1]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_t[2]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_c[2]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_t[3]}]
set_property IOSTANDARD DIFF_POD12_DCI [get_ports {c0_ddr4_dqs_c[3]}]
#-----------------------------------------------------------------------------
