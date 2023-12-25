# Verify the bitstream in a FPGA. 
# Designed to be sourced with the Xilinx Vivado lab tools <install_root>/Vitis/<version>/bin/vivado_lab.
# Takes a pair of arguments which are the bitstream and mask filenames.

if { $argc != 2 } {
    puts [concat "Usage: " $argv0 " <bitstream_filename> <mask_filename>"]
    exit 1
}

set bitstream_filename [lindex $argv 0]
set mask_filename [lindex $argv 1]

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
    puts [concat "Verifying " $bitstream_filename " in " $DEVICE ]

    # Select the single device to verify
    create_hw_bitstream -hw_device $DEVICE -mask $mask_filename $bitstream_filename
    verify_hw_device $DEVICE
} else {
    puts "Can only operate on a single target as no way of specifying the which target to verify the bitstream in."
    puts [concat "Number of targets : " $NUM_TARGETS]
}

if {$disconnect_en == 1 } {
    disconnect_hw_server -quiet
}

