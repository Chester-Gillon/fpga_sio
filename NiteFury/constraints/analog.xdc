###############################################################################
# Analog inputs
###############################################################################

# The pin placement for the external Vaux14 input for TMON core, but need to set
# a compatible IO standand for pins in the same bank 35 to prevent errors when
# placing the design even though the IOSTANDARD does not affect the input programming.
# Vaux14 is set to unipolar with:
# a. The TMON_CORE signal connected to AD14P
# b. GND connected to AD14N
#
# The 7 Series FPGAs and Zynq-7000 SoC XADC Dual 12-Bit 1 MSPS Analog-to-Digital Converter User Guide
# https://www.xilinx.com/content/dam/xilinx/support/documents/user_guides/ug480_7Series_XADC.pdf
# shows that in unipolar mode Csample is between the Vp and Vn inputs, so both are set here.
set_property PACKAGE_PIN H2 [get_ports {TMON_CORE_v_p}]
set_property IOSTANDARD LVCMOS33 [get_ports {TMON_CORE_v_p}]
set_property PACKAGE_PIN G2 [get_ports {TMON_CORE_v_n}]
set_property IOSTANDARD LVCMOS33 [get_ports {TMON_CORE_v_n}]