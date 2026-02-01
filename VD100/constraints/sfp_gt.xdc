# Defines pins for SFP GT signals

set_property PACKAGE_PIN H7 [get_ports {SFP_REF_clk_p}]
create_clock -name {SFP_REF_clk_p} -period 6.4 [get_ports {SFP_REF_clk_p}]

# For SFP1
set_property PACKAGE_PIN E5 [get_ports {SFP_serial_gtx_p[0]}]

# For SFP2
set_property PACKAGE_PIN D8 [get_ports {SFP_serial_gtx_p[1]}]

# Unconnected on the baseboard, but defined here as part of the GT quad 
set_property PACKAGE_PIN C5 [get_ports {SFP_serial_gtx_p[2]}]
set_property PACKAGE_PIN B8 [get_ports {SFP_serial_gtx_p[3]}]
