






# file: ibert_ultrascale_gty_0.xdc
####################################################################################

##**************************************************************************
## TX/RX out clock clock constraints
##





# GT X0Y8
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[0].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[0].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y9
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[1].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[1].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y10
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[2].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[2].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y11
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[3].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD0.u_q/CH[3].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y12
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[0].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[0].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y13
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[1].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[1].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y14
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[2].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[2].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
# GT X0Y15
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[3].u_ch/u_gtye4_channel/RXOUTCLK}] -include_generated_clocks]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins {*/*/*/u_ibert_gty_core/inst/QUAD1.u_q/CH[3].u_ch/u_gtye4_channel/TXOUTCLK}] -include_generated_clocks]
