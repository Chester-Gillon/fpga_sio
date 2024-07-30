# A MT25QU256 with a 1.8V support is used as the configuration QuadSPI flash
#
# The STARTUP primitive has been enabled in the QAUD SPI block, and "Startup internal to IP" selected.
# As a result, no pins need to be defined here.

# Taken from example project for the board:
# PCIE_5P/PCIE/pcie4_uscale_plus_0_ex/imports/xilinx_pcie4_uscale_plus_x0y0.xdc
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 56.7 [current_design]
set_property BITSTREAM.CONFIG.SPI_32BIT_ADDR YES [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLUP [current_design]

# For tracking the version of the bitstream
set_property BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP [current_design]