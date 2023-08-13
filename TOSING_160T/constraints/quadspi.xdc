# A MX25L128 with a 3.3V support is used as the configuration QuadSPI flash
# The clock isn't listed here as uses the dedicated CCLK_0 pin.
set_property PACKAGE_PIN C23 [get_ports {spi_rtl_ss_io[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_rtl_ss_io[0]}]
set_property PACKAGE_PIN B24 [get_ports spi_rtl_io0_io]
set_property PACKAGE_PIN A25 [get_ports spi_rtl_io1_io]
set_property PACKAGE_PIN B22 [get_ports spi_rtl_io2_io]
set_property PACKAGE_PIN A22 [get_ports spi_rtl_io3_io]
set_property IOSTANDARD LVCMOS33 [get_ports spi_rtl_io0_io]
set_property IOSTANDARD LVCMOS33 [get_ports spi_rtl_io1_io]
set_property IOSTANDARD LVCMOS33 [get_ports spi_rtl_io2_io]
set_property IOSTANDARD LVCMOS33 [get_ports spi_rtl_io3_io]
