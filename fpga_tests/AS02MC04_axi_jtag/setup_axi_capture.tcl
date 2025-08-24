# Configure the ILA to capture AXI READ or WRITE transfers.
# The contents of this file saved from the TCL console after the GUI was used to configure the ILA.
delete_hw_probe -quiet [get_hw_probe READ]
delete_hw_probe -quiet [get_hw_probe WRITE]
create_hw_probe -map {probe12[0] probe12[1]} READ[1:0] [get_hw_ilas hw_ila_1]
create_hw_probe -map {probe13[0] probe13[1]} WRITE[1:0] [get_hw_ilas hw_ila_1]

# These probes include the READY and VALID signals, so a value of 'h3 mean both active and therefore a
# data transfer.
set_property CONTROL.CAPTURE_MODE BASIC [get_hw_ilas -of_objects [get_hw_devices xcku3p_0] -filter {CELL_NAME=~"u_ila_0"}]
set_property CAPTURE_COMPARE_VALUE eq2'h3 [get_hw_probes READ -of_objects [get_hw_ilas -of_objects [get_hw_devices xcku3p_0] -filter {CELL_NAME=~"u_ila_0"}]]
set_property CAPTURE_COMPARE_VALUE eq2'h3 [get_hw_probes WRITE -of_objects [get_hw_ilas -of_objects [get_hw_devices xcku3p_0] -filter {CELL_NAME=~"u_ila_0"}]]
set_property CONTROL.CAPTURE_CONDITION OR [get_hw_ilas -of_objects [get_hw_devices xcku3p_0] -filter {CELL_NAME=~"u_ila_0"}]

# Need to set the trigger position to zero to be able to upload the 1st data transfer.
# Setting CONTROL.TRIGGER_MODE was commented out following changing debug.xdc to configure
# the ILA with C_ADV_TRIGGER false. With C_ADV_TRIGGER false in the ILA, attempting to set CONTROL.TRIGGER_MODE
# results in the error:
#     ERROR: [Labtoolstcl 44-156] hw_ila property 'CONTROL.TRIGGER_MODE' is read-only.
# set_property CONTROL.TRIGGER_MODE BASIC_ONLY [get_hw_ilas -of_objects [get_hw_devices xcku3p_0] -filter {CELL_NAME=~"u_ila_0"}]
set_property CONTROL.TRIGGER_POSITION 0 [get_hw_ilas -of_objects [get_hw_devices xcku3p_0] -filter {CELL_NAME=~"u_ila_0"}]