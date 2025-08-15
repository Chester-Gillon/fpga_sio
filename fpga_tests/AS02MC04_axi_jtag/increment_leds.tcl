# Perform a read-modify write of the GPIO ports connected to the LEDs on the edge of the board
# - Bit 0 active low for Green led
# - Bit 1 active low for Red led
# - If try and turn on both LEDs only Red led illuminates 

set myread [llength [get_hw_axi_txns rd_txn_lite] ]
if { $myread == 0 } {
    create_hw_axi_txn rd_txn_lite [get_hw_axis hw_axi_1] -address 0001fff0 -type read
}

set myread [llength [get_hw_axi_txns wr_txn_lite] ]
if { $myread == 0 } {
    create_hw_axi_txn wr_txn_lite [get_hw_axis hw_axi_1] -address 0001fff0 -type write
}

set led_output_addr 0x0
set led_readback_addr 0x8

reset_hw_axi [get_hw_axis hw_axi_1]
set_property CMD.ADDR $led_readback_addr [get_hw_axi_txns rd_txn_lite]
set_property CMD.LEN 1 [get_hw_axi_txns rd_txn_lite]
set_property CMD.ADDR $led_output_addr [get_hw_axi_txns wr_txn_lite] 
set_property CMD.LEN 1 [get_hw_axi_txns wr_txn_lite]

run_hw_axi -quiet [get_hw_axi_txns rd_txn_lite]
set initial_readback_data [get_property DATA [get_hw_axi_txns rd_txn_lite]]

set output_data [format "%08X" [expr ($initial_readback_data + 1) & 0x3]]
set_property DATA 0x$output_data [get_hw_axi_txns wr_txn_lite]
run_hw_axi [get_hw_axi_txns wr_txn_lite]

run_hw_axi -quiet [get_hw_axi_txns rd_txn_lite]
set final_readback_data [get_property DATA [get_hw_axi_txns rd_txn_lite]]

puts "LED : 0x$initial_readback_data -> 0x$final_readback_data"