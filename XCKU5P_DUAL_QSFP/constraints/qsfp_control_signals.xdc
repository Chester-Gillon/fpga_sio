# Defines the pins used for the QSFP module management.
# I.e. the slow speed signals rather than the SERDES signals.
#
# fpga_tests/XCKU5P_DUAL_QSFP_input_monitor/readme.txt found that the following
# have external pull-ups when no QSFP modules are fitted:
# Inputs        : INTERRUPT, MOD_PRSN
# Outputs       : RESET, LP_MODE, MOD_SEL
# Bi-directional: SCL, SDA
#
# Therefore, no need to enable an internal pull-up on the above signals.
#
# Placement on QSFP modules looking from outside the PC:
#
#                            port B LED 
# PCIe       QSFP    QSFP    unused LED
# connector  port B  port A  port A LED
# ---------------------------------------


# QSFP port A control signals.

# Output set low to enable the I2C interface on the QSFP module.
# Since each QSFP module has a dedicated I2C bus, there is no need to multiplex
# the module selection when accessing the I2C interface at run time in terms of
# avoiding conflicts.
#
# Leaving disabled when not in use may offer protection against "noise" making
# unexpected modifications.
set_property PACKAGE_PIN AF14 [get_ports {QSFP_MOD_SEL_A}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_MOD_SEL_A}]

# Active low output to reset the module.
set_property PACKAGE_PIN AE15 [get_ports {QSFP_RESET_A}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_RESET_A}]

set_property PACKAGE_PIN B14 [get_ports {QSFP_I2C_A_scl_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_I2C_A_scl_io}] 

set_property PACKAGE_PIN A14 [get_ports {QSFP_I2C_A_sda_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_I2C_A_sda_io}] 

# Input pulled low to indicate a module present.
set_property PACKAGE_PIN C14 [get_ports {QSFP_MOD_PRSN_A}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_MOD_PRSN_A}]

# Input pull low to indicate an interrupt.
set_property PACKAGE_PIN AD15 [get_ports {QSFP_INTERRUPT_A}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_INTERRUPT_A}]

# Active high output to force the module into low-power state.
# May be changed by I2C command to be transmit disable.
# SFF-8679 suggests this should default to high (i.e. low power), to avoid exceeding
# the host system power cap acity and thermal management.
set_property PACKAGE_PIN AF15 [get_ports {QSFP_LP_MODE_A}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_LP_MODE_A}]


# QSFP port B control signals.
set_property PACKAGE_PIN B9 [get_ports {QSFP_MOD_SEL_B}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_MOD_SEL_B}]

set_property PACKAGE_PIN A10 [get_ports {QSFP_RESET_B}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_RESET_B}]

# The XCKU5P_pcie_qsfp_or_sfp_IO.xls delivered as documentation for the board indicated
# QSFP B SCL is on pin B10. However, using that pin resulted in no I2C communication with
# a QSFP module.
# fpga_tests/XCKU5P_DUAL_QSFP_input_monitor/readme.txt indicated B11 has an external pull-up
# so changed to pin B11.
set_property PACKAGE_PIN B11 [get_ports {QSFP_I2C_B_scl_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_I2C_B_scl_io}] 

set_property PACKAGE_PIN B12 [get_ports {QSFP_I2C_B_sda_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_I2C_B_sda_io}] 

set_property PACKAGE_PIN A13 [get_ports {QSFP_MOD_PRSN_B}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_MOD_PRSN_B}]

set_property PACKAGE_PIN A12 [get_ports {QSFP_INTERRUPT_B}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_INTERRUPT_B}]

set_property PACKAGE_PIN A9 [get_ports {QSFP_LP_MODE_B}]
set_property IOSTANDARD LVCMOS33 [get_ports {QSFP_LP_MODE_B}]


# LEDs on the board which are visible externally between the pair of QSFP ports.
# Assigned as one indicator per QSFP port.
#
# LEDs are active low.
set_property PACKAGE_PIN D25 [get_ports {QSFP_LED_A}]
set_property IOSTANDARD LVCMOS18 [get_ports {QSFP_LED_A}]

set_property PACKAGE_PIN E26 [get_ports {QSFP_LED_B}]
set_property IOSTANDARD LVCMOS18 [get_ports {QSFP_LED_B}]
