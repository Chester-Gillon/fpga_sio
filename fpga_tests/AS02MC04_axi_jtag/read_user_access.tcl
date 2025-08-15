# Read the user access value over the hw_axi interface

set myread [llength [get_hw_axi_txns rd_txn_lite] ]
if { $myread == 0 } {
    create_hw_axi_txn rd_txn_lite [get_hw_axis hw_axi_1] -address 0001fff0 -type read
}

reset_hw_axi [get_hw_axis hw_axi_1]
set_property CMD.ADDR 0x00001000 [get_hw_axi_txns rd_txn_lite]
set_property CMD.LEN 1 [get_hw_axi_txns rd_txn_lite]
run_hw_axi -quiet [get_hw_axi_txns rd_txn_lite]
set tdata [get_property DATA [get_hw_axi_txns rd_txn_lite]]
puts "User access : 0x$tdata"