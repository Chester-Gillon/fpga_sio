






# file: ibert_ultrascale_gty_0.xdc
####################################################################################

##**************************************************************************
## TX/RX out clock clock constraints
##





# GT X0Y44
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[0].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[0].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y45
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[1].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[1].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y46
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[2].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[2].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y47
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[3].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[3].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y48
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[0].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[0].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y49
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[1].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[1].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y50
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[2].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[2].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y51
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[3].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[3].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
