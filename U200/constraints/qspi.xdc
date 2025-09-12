# Bitstream Generation for QSPI                              
set_property CONFIG_VOLTAGE 1.8                        [current_design]
set_property BITSTREAM.CONFIG.CONFIGFALLBACK Enable    [current_design]                  ;# Golden image is the fall back image if  new bitstream is corrupted.    
set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN disable [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 63.8          [current_design] 
#set_property BITSTREAM.CONFIG.CONFIGRATE 85.0          [current_design]                 ;# Customer can try but may not be reliable over all conditions.
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4           [current_design]  
set_property BITSTREAM.GENERAL.COMPRESS TRUE           [current_design]  
set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES        [current_design]
set_property BITSTREAM.CONFIG.SPI_32BIT_ADDR Yes       [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN Pullup         [current_design]

# For tracking the version of the bitstream
set_property BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP [current_design]