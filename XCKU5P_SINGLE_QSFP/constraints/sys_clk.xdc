# 100 MHZ differential input clock.
# Source is a SG7050VAN-100.000000M-KEGA3 which has LVDS output.
# The oscillator output is AC coupled, and biased at 0.6V.
# On bank 66 which is HP I/O, with a VCCO of 1.2V.
# The IO standard DIFF_POD12_DCI was taken from the XCKU5P_PCIe_DDR4_ETH example project.
# UG861 indicates DIFF_POD12_DCI is valid for a VCCO of 1.2V, whereas LVDS requires VCCO of 1.8V. 
set_property IOSTANDARD DIFF_POD12_DCI [get_ports SYS_CLK_clk_p]
set_property PACKAGE_PIN H23 [get_ports {SYS_CLK_clk_p}]
create_clock -name {SYS_CLK_clk_p} -period 10.00 [get_ports {SYS_CLK_clk_p}]