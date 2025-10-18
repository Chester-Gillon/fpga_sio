






# file: ibert_ultrascale_gty_0.xdc
####################################################################################

##**************************************************************************
##
## Icon Constraints
##





create_clock -name D_CLK -period 6.4 [get_ports gty_sysclkp_i]
set_clock_groups -group [get_clocks D_CLK -include_generated_clocks] -asynchronous

set_property C_CLK_INPUT_FREQ_HZ 156250000 [get_debug_cores dbg_hub]
set_property C_ENABLE_CLK_DIVIDER true [get_debug_cores dbg_hub]
##
##gtrefclk lock constraints
##






 
set_property PACKAGE_PIN M11 [get_ports gty_refclk0p_i[0]]
set_property PACKAGE_PIN M10 [get_ports gty_refclk0n_i[0]]
set_property PACKAGE_PIN K11 [get_ports gty_refclk1p_i[0]]
set_property PACKAGE_PIN K10 [get_ports gty_refclk1n_i[0]]
##
## Refclk constraints
##





 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
create_clock -name gtrefclk0_12 -period 6.207 [get_ports gty_refclk0p_i[0]]
create_clock -name gtrefclk1_12 -period 6.207 [get_ports gty_refclk1p_i[0]]
set_clock_groups -group [get_clocks gtrefclk0_12 -include_generated_clocks] -asynchronous
set_clock_groups -group [get_clocks gtrefclk1_12 -include_generated_clocks] -asynchronous
##
## System clock pin locs and timing constraints
##
set_property PACKAGE_PIN AU19 [get_ports gty_sysclkp_i]
set_property IOSTANDARD LVDS [get_ports gty_sysclkp_i]

