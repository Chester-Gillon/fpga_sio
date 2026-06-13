# A MT25QU128 with a 1.8V support is used as the configuration QuadSPI flash
#
# The STARTUP primitive has been enabled in the QAUD SPI block, and "Startup internal to IP" selected.
# As a result, no pins need to be defined here.

# Set to match the settinhs for the XCKU5P_PCIe_DDR4_ETH example project, as reported by parse_bitstream_file.
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE  51.0 [current_design]
set_property BITSTREAM.CONFIG.SPI_32BIT_ADDR NO [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE NO [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLDOWN [current_design]

# For tracking the version of the bitstream
set_property BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP [current_design]