###############################################################################
# SPI
###############################################################################
set_property PACKAGE_PIN P22 [get_ports {spi_rtl_0_io0_io}]
set_property PACKAGE_PIN R22 [get_ports {spi_rtl_0_io1_io}]
set_property PACKAGE_PIN P21 [get_ports {spi_rtl_0_io2_io}]
set_property PACKAGE_PIN R21 [get_ports {spi_rtl_0_io3_io}]

set_property IOSTANDARD LVCMOS33 [get_ports {spi_rtl_0_io0_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_rtl_0_io1_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_rtl_0_io2_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_rtl_0_io3_io}]

set_property PACKAGE_PIN T19 [get_ports {spi_rtl_0_ss_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_rtl_0_ss_io}]

###############################################################################
# Additional design / project settings
###############################################################################

# Power down on overtemp
set_property BITSTREAM.CONFIG.OVERTEMPPOWERDOWN ENABLE [current_design]

# High-speed configuration so FPGA is up in time to negotiate with PCIe root complex
set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN Div-1 [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
set_property CONFIG_MODE SPIx4 [current_design]
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]

set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property CFGBVS VCCO [current_design]
