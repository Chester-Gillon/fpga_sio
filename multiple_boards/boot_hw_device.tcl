# Boot a FPGA from it's configuration memory. 
# Designed to be sourced with the Xilinx Vivado lab tools <install_root>/Vitis/<version>/bin/vivado_lab.
#
# https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/boot_hw_device says this:
#   Issue JTAG Program command to hw_device

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
    puts [concat "Booting " $DEVICE ]

    # Boot the device
    boot_hw_device $DEVICE
} else {
    puts "Can only operate on a single target as no way of specifying the which target to boot."
    puts [concat "Number of targets : " $NUM_TARGETS]
}

if {$disconnect_en == 1 } {
    disconnect_hw_server -quiet
}
