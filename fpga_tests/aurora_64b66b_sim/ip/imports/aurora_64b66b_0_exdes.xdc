 ##################################################################################
 ##
 ## Project:  Aurora 64B/66B
 ## Company:  Xilinx
 ##
 ##
 ##
 ## (c) Copyright 2008 - 2018 Xilinx, Inc. All rights reserved.
 ##
 ## This file contains confidential and proprietary information
 ## of Xilinx, Inc. and is protected under U.S. and
 ## international copyright and other intellectual property
 ## laws.
 ##
 ## DISCLAIMER
 ## This disclaimer is not a license and does not grant any
 ## rights to the materials distributed herewith. Except as
 ## otherwise provided in a valid license issued to you by
 ## Xilinx, and to the maximum extent permitted by applicable
 ## law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
 ## WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
 ## AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
 ## BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
 ## INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
 ## (2) Xilinx shall not be liable (whether in contract or tort,
 ## including negligence, or under any other theory of
 ## liability) for any loss or damage of any kind or nature
 ## related to, arising under or in connection with these
 ## materials, including for any direct, or any indirect,
 ## special, incidental, or consequential loss or damage
 ## (including loss of data, profits, goodwill, or any type of
 ## loss or damage suffered as a result of any action brought
 ## by a third party) even if such damage or loss was
 ## reasonably foreseeable or Xilinx had been advised of the
 ## possibility of the same.
 ##
 ## CRITICAL APPLICATIONS
 ## Xilinx products are not designed or intended to be fail-
 ## safe, or for use in any application requiring fail-safe
 ## performance, such as life-support or safety devices or
 ## systems, Class III medical devices, nuclear facilities,
 ## applications related to the deployment of airbags, or any
 ## other applications that could lead to death, personal
 ## injury, or severe property or environmental damage
 ## (individually and collectively, "Critical
 ## Applications"). Customer assumes the sole risk and
 ## liability of any use of Xilinx products in Critical
 ## Applications, subject only to applicable laws and
 ## regulations governing limitations on product liability.
 ##
 ## THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
 ## PART OF THIS FILE AT ALL TIMES.
 
 ##
 #################################################################################
 
 ##
 ##  aurora_64b66b_0_exdes 
 ##
 ##
 ##  Description: This is the user constraints file for a 1 lane Aurora
 ##               core. 
 ##               This is simplex example design xdc.
 ##  Note: User need to set proper IO standards for the LOC's mentioned below.
 ###################################################################################################

   # 50MHz board Clock Constraint   
   create_clock -period 20.000	 [get_ports INIT_CLK_P]

	# below constraint is needed for example design
	set_false_path -to [get_pins -hier *aurora_64b66b_0_cdc_to*/D]        


################################################################################


 
 # User Clock Contraint: the value is selected based on the line rate of the module
   #create_clock -period 15.06 [get_pins aurora_64b66b_0_block_i/clock_module_i/user_clk_net_i/I] 
 
 # SYNC Clock Constraint
   #create_clock -period 7.530	 [get_pins aurora_64b66b_0_block_i/clock_module_i/sync_clock_net_i/I] 
  
  

 
 ################################ IP LEVEL CONSTRAINTS START ##############################
  # Create clock constraint for TXOUTCLK from GT
  #create_clock -period 7.530	 [get_pins -filter {REF_PIN_NAME=~*TXOUTCLK} -of_objects [get_cells -hierarchical -filter {NAME =~ *aurora_64b66b_0_wrapper_i*aurora_64b66b_0_multi_gt_i*aurora_64b66b_0_gtx_inst/gtxe2_i*}]]
 

  # Create clock constraint for RXOUTCLK from GT
  # create_clock -period 7.530	 [get_pins -filter {REF_PIN_NAME=~*RXOUTCLK} -of_objects [get_cells -hierarchical -filter {NAME =~ *aurora_64b66b_0_wrapper_i*aurora_64b66b_0_multi_gt_i*aurora_64b66b_0_gtx_inst/gtxe2_i*}]]
 

 
 ################################ IP LEVEL CONSTRAINTS END ##############################
 
 # Reference clock contraint for GTX
   create_clock -period 9.412	 [get_ports GTXQ0_P] 
 
   ### DRP Clock Constraint
   create_clock -period 10.000	 [get_ports DRP_CLK_IN]
 

 
 ###### No cross clock domain analysis. Domains are not related ##############
 ## set_false_path -from [get_clocks init_clk] -to [get_clocks TS_user_clk_i]
 ## set_false_path -from [get_clocks TS_user_clk_i] -to [get_clocks init_clk]
 ## set_false_path -from [get_clocks init_clk] -to [get_clocks TS_sync_clk_i]
 ## set_false_path -from [get_clocks TS_sync_clk_i] -to [get_clocks init_clk]

## ## set_false_path -from init_clk -to [get_clocks -of_objects [get_pins aurora_64b66b_0_block_i/clock_module_i/mmcm_adv_inst/CLKOUT0]]
## 
## ## set_false_path -from [get_clocks -of_objects [get_pins aurora_64b66b_0_block_i/clock_module_i/mmcm_adv_inst/CLKOUT0]] -to init_clk
## 
## 
## ## set_false_path -from init_clk -to [get_clocks -of_objects [get_pins aurora_64b66b_0_block_i/clock_module_i/mmcm_adv_inst/CLKOUT1]]
## 
## ## set_false_path -from [get_clocks -of_objects [get_pins aurora_64b66b_0_block_i/clock_module_i/mmcm_adv_inst/CLKOUT1]] -to init_clk
##
 

 ################################ GT CLOCK Locations   ##############
 # Differential SMA Clock Connection
   set_property LOC H6 [get_ports GTXQ0_P]
   set_property LOC H5 [get_ports GTXQ0_N]
 
 


                                                                                                                                              
   set_property LOC GTXE2_CHANNEL_X0Y0 [get_cells  aurora_64b66b_0_block_i/aurora_64b66b_0_i/inst/aurora_64b66b_0_wrapper_i/aurora_64b66b_0_multi_gt_i/aurora_64b66b_0_gtx_inst/gtxe2_i]


 

  ##Note: User should add LOC based upon the board
  #       Below LOC's are place holders and need to be changed as per the device and board
             #set_property LOC D17 [get_ports INIT_CLK_P]
             #set_property LOC D18 [get_ports INIT_CLK_N]
    
##Note: The number of push button resets available is limited hence using DIP Switches
##Note: This is only for dataflow config being TX/RX_Simplex
             #set_property LOC G19 [get_ports TX_RESET]
             #set_property LOC A21 [get_ports RX_RESET]
             #set_property LOC K18 [get_ports PMA_INIT]
    
             #set_property LOC A20 [get_ports TX_CHANNEL_UP]
             #set_property LOC A16 [get_ports TX_LANE_UP]
             #set_property LOC A17 [get_ports RX_CHANNEL_UP]
             #set_property LOC B20 [get_ports RX_LANE_UP]

             # set_property LOC AG14 [get_ports RX_HARD_ERR]   
             # set_property LOC AH17 [get_ports RX_SOFT_ERR]   
             # set_property LOC AJ17 [get_ports DATA_ERR_COUNT[0]]   
             # set_property LOC AE16 [get_ports DATA_ERR_COUNT[1]]   
             # set_property LOC AF16 [get_ports DATA_ERR_COUNT[2]]   
             # set_property LOC AJ19 [get_ports DATA_ERR_COUNT[3]]   
             # set_property LOC AK19 [get_ports DATA_ERR_COUNT[4]]   
             # set_property LOC AG19 [get_ports DATA_ERR_COUNT[5]]   
             # set_property LOC AH19 [get_ports DATA_ERR_COUNT[6]]   
             # set_property LOC AJ18 [get_ports DATA_ERR_COUNT[7]]   
             # set_property LOC AE19 [get_ports TX_HARD_ERR]   
             # set_property LOC Y13 [get_ports TX_SOFT_ERR]   
    
             #set_property LOC AG29 [get_ports DRP_CLK_IN]
             #// DRP CLK needs a clock LOC
    
             #set_property LOC Y14 [get_ports CRC_PASS_FAIL_N] 
             #set_property LOC AK10 [get_ports CRC_VALID] 
  ##Note: User should add LOC based upon the board
  #       Below LOC's are place holders and need to be changed as per the device and board
	         #set_property IOSTANDARD LVDS_25 [get_ports INIT_CLK_P]
	         #set_property IOSTANDARD LVDS_25 [get_ports INIT_CLK_N]
    
##Note: The number of push button resets available is limited hence using DIP Switches
##Note: This is only for dataflow config being TX/RX_Simplex
	         #set_property IOSTANDARD LVCMOS18 [get_ports TX_RESET]
	         #set_property IOSTANDARD LVCMOS18 [get_ports RX_RESET]
	      #set_property IOSTANDARD LVCMOS18 [get_ports PMA_INIT]
    
	         #set_property IOSTANDARD LVCMOS18  [get_ports TX_CHANNEL_UP]
	         #set_property IOSTANDARD LVCMOS18  [get_ports TX_LANE_UP]
	         #set_property IOSTANDARD LVCMOS18  [get_ports RX_CHANNEL_UP]
	         #set_property IOSTANDARD LVCMOS18  [get_ports RX_LANE_UP]

              #set_property IOSTANDARD LVCMOS18 [get_ports RX_HARD_ERR]   
              #set_property IOSTANDARD LVCMOS18 [get_ports RX_SOFT_ERR]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[0]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[1]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[2]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[3]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[4]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[5]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[6]]   
              #set_property IOSTANDARD LVCMOS18 [get_ports DATA_ERR_COUNT[7]]   
             # set_property IOSTANDARD LVCMOS18 [get_ports TX_HARD_ERR]   
             # set_property IOSTANDARD LVCMOS18 [get_ports TX_SOFT_ERR]   
    
             #set_property IOSTANDARD LVCMOS18 [get_ports DRP_CLK_IN]
             #// DRP CLK needs a clock LOC
    
             #set_property IOSTANDARD LVCMOS18 [get_ports CRC_PASS_FAIL_N] 
             #set_property IOSTANDARD LVCMOS18 [get_ports CRC_VALID] 



