# Program a bitstream into a FPGA. 
# Designed to be sourced with the Xilinx Vivado lab tools <install_root>/Vitis/<version>/bin/vivado_lab.
# Takes a single argument which is the bitstream filename.
#
# Doesn't validate the bitstream matches the target device, since program_hw_devices does that.
# Example error attempting to program an incompatible target device:
#    ERROR: [Labtools 27-3303] Incorrect bitstream assigned to device.
#    Bitfile is incompatible for this device.
#    Bitstream was generated for device xc7k160t and target device is xc7a200t.

if { $argc != 1 } {
    puts [concat "Usage: " $argv0 " <bitstream_filename>"]
    exit 1
}

set bitstream_filename [lindex $argv 0]

# set default hw_server connection 
set HW_SERVER localhost:3121

open_hw_manager
if {[llength [get_hw_servers -quiet]] == 0 } then {
    connect_hw_server -quiet -url $HW_SERVER
    set disconnect_en 1
}

# update list of targets
refresh_hw_server -quiet

set NUM_TARGETS [llength [get_hw_targets -quiet]]
if {$NUM_TARGETS == 1 } then {
    set TARGET [get_hw_targets]
    open_hw_target -quiet $TARGET
    set DEVICE [get_hw_devices]
    puts [concat "Programming " $bitstream_filename " into " $DEVICE ]

    # Select the single device to program 
    current_hw_device $DEVICE
    set_property PROGRAM.FILE $bitstream_filename [current_hw_device]
    program_hw_devices $DEVICE
} else {
    puts "Can only operate on a single target as no way of specifying the which target to load the bitstream to."
    puts [concat "Number of targets : " $NUM_TARGETS]
}

if {$disconnect_en == 1 } {
    disconnect_hw_server -quiet
}
