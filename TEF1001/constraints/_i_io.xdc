#----------
#USER LED Matrix
#

#USER LEDS CONNECTED TO A FMC_ADJ VCCO BANK (default config 1.8V)
set_property PACKAGE_PIN K25 [get_ports {USR_LED[0]}]
set_property PACKAGE_PIN K26 [get_ports {USR_LED[1]}]
set_property PACKAGE_PIN P26 [get_ports {USR_LED[2]}]
set_property PACKAGE_PIN R26 [get_ports {USR_LED[3]}]
set_property PACKAGE_PIN N16 [get_ports {USR_LED[4]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[0]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[1]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[2]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[3]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[4]}]

#USER LEDS CONNECTED TO A 1.8V VCCO BANK
set_property PACKAGE_PIN J26 [get_ports {USR_LED[5]}]
set_property PACKAGE_PIN H26 [get_ports {USR_LED[6]}]
set_property PACKAGE_PIN E26 [get_ports {USR_LED[7]}]
set_property PACKAGE_PIN A24 [get_ports {USR_LED[8]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[5]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[6]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[7]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[8]}]

#USER LED CONNECTED TO A FMC_ADJ VCCO BANK (default config 1.8V)
set_property PACKAGE_PIN F19 [get_ports {USR_LED[9]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_LED[9]}]


#----------
#USER LED over CPLD
# FEX11
set_property PACKAGE_PIN B21 [get_ports {USR_CPLD_LED[0]}]
set_property IOSTANDARD LVCMOS18 [get_ports {USR_CPLD_LED[0]}]
#----------
#CLK DDR3
#AC9 /AD9 for REV01
#AB11 / AC11 for REV02
##set_property PACKAGE_PIN AB11 [get_ports CLK_DDR3_200MHz_clk_p]
##set_property PACKAGE_PIN AC11 [get_ports CLK_DDR3_200MHz_clk_n]
##set_property IOSTANDARD DIFF_SSTL15 [get_ports CLK_DDR3_200MHz_clk_p]
##set_property IOSTANDARD DIFF_SSTL15 [get_ports CLK_DDR3_200MHz_clk_n]
#----------
#QSPI
set_property PACKAGE_PIN C23 [get_ports {spi_rtl_ss_io[0]}]
set_property IOSTANDARD LVCMOS18 [get_ports {spi_rtl_ss_io[0]}]
set_property PACKAGE_PIN B24 [get_ports spi_rtl_io0_io]
set_property PACKAGE_PIN A25 [get_ports spi_rtl_io1_io]
set_property PACKAGE_PIN B22 [get_ports spi_rtl_io2_io]
set_property PACKAGE_PIN A22 [get_ports spi_rtl_io3_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_io0_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_io1_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_io2_io]
set_property IOSTANDARD LVCMOS18 [get_ports spi_rtl_io3_io]
#----------
#IIC to CPLD
set_property PACKAGE_PIN G26 [get_ports SCF_cpld_1_scl]
set_property PACKAGE_PIN F25 [get_ports SCF_cpld_14_oe]
set_property PACKAGE_PIN G25 [get_ports SCF_cpld_16_sda]
set_property IOSTANDARD LVCMOS18 [get_ports SCF_cpld_1_scl]
set_property IOSTANDARD LVCMOS18 [get_ports SCF_cpld_14_oe]
set_property IOSTANDARD LVCMOS18 [get_ports SCF_cpld_16_sda]
#----------
#SI5338 CLKs
set_property PACKAGE_PIN H6 [get_ports {SI_MGT115_0_clk_p[0]}]

set_property PACKAGE_PIN G22 [get_ports {SI_FCLK_clk_p[1]}]
set_property PACKAGE_PIN D23 [get_ports {SI_FCLK_clk_p[2]}]
set_property PACKAGE_PIN G24 [get_ports {SI_FCLK_clk_p[0]}]
set_property IOSTANDARD LVDS_25 [get_ports {SI_FCLK_*}]