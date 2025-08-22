# Created from Synthesis -> Set Up Debug

create_debug_core u_ila_0 ila
set_property ALL_PROBE_SAME_MU true [get_debug_cores u_ila_0]
set_property ALL_PROBE_SAME_MU_CNT 4 [get_debug_cores u_ila_0]
set_property C_ADV_TRIGGER true [get_debug_cores u_ila_0]
set_property C_DATA_DEPTH 65536 [get_debug_cores u_ila_0]
set_property C_EN_STRG_QUAL true [get_debug_cores u_ila_0]
set_property C_INPUT_PIPE_STAGES 0 [get_debug_cores u_ila_0]
set_property C_TRIGIN_EN false [get_debug_cores u_ila_0]
set_property C_TRIGOUT_EN false [get_debug_cores u_ila_0]
set_property port_width 1 [get_debug_ports u_ila_0/clk]
connect_debug_port u_ila_0/clk [get_nets [list AS02MC04_axi_jtag_i/clk_wiz/inst/clk_out1]]
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe0]
set_property port_width 8 [get_debug_ports u_ila_0/probe0]
connect_debug_port u_ila_0/probe0 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[3]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[4]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[5]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[6]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWLEN[7]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe1]
set_property port_width 2 [get_debug_ports u_ila_0/probe1]
connect_debug_port u_ila_0/probe1 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWBURST[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWBURST[1]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe2]
set_property port_width 3 [get_debug_ports u_ila_0/probe2]
connect_debug_port u_ila_0/probe2 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWSIZE[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWSIZE[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWSIZE[2]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe3]
set_property port_width 32 [get_debug_ports u_ila_0/probe3]
connect_debug_port u_ila_0/probe3 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[3]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[4]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[5]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[6]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[7]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[8]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[9]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[10]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[11]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[12]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[13]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[14]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[15]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[16]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[17]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[18]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[19]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[20]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[21]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[22]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[23]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[24]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[25]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[26]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[27]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[28]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[29]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[30]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RDATA[31]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe4]
set_property port_width 2 [get_debug_ports u_ila_0/probe4]
connect_debug_port u_ila_0/probe4 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARBURST[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARBURST[1]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe5]
set_property port_width 32 [get_debug_ports u_ila_0/probe5]
connect_debug_port u_ila_0/probe5 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[3]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[4]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[5]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[6]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[7]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[8]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[9]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[10]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[11]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[12]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[13]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[14]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[15]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[16]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[17]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[18]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[19]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[20]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[21]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[22]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[23]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[24]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[25]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[26]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[27]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[28]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[29]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[30]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARADDR[31]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe6]
set_property port_width 2 [get_debug_ports u_ila_0/probe6]
connect_debug_port u_ila_0/probe6 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RRESP[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RRESP[1]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe7]
set_property port_width 1 [get_debug_ports u_ila_0/probe7]
connect_debug_port u_ila_0/probe7 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WSTRB[0]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe8]
set_property port_width 32 [get_debug_ports u_ila_0/probe8]
connect_debug_port u_ila_0/probe8 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[3]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[4]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[5]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[6]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[7]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[8]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[9]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[10]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[11]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[12]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[13]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[14]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[15]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[16]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[17]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[18]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[19]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[20]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[21]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[22]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[23]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[24]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[25]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[26]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[27]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[28]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[29]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[30]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WDATA[31]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe9]
set_property port_width 2 [get_debug_ports u_ila_0/probe9]
connect_debug_port u_ila_0/probe9 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_BRESP[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_BRESP[1]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe10]
set_property port_width 3 [get_debug_ports u_ila_0/probe10]
connect_debug_port u_ila_0/probe10 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARSIZE[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARSIZE[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARSIZE[2]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe11]
set_property port_width 4 [get_debug_ports u_ila_0/probe11]
connect_debug_port u_ila_0/probe11 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWCACHE[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWCACHE[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWCACHE[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWCACHE[3]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe12]
set_property port_width 4 [get_debug_ports u_ila_0/probe12]
connect_debug_port u_ila_0/probe12 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARCACHE[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARCACHE[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARCACHE[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARCACHE[3]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe13]
set_property port_width 32 [get_debug_ports u_ila_0/probe13]
connect_debug_port u_ila_0/probe13 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[3]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[4]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[5]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[6]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[7]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[8]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[9]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[10]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[11]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[12]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[13]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[14]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[15]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[16]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[17]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[18]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[19]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[20]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[21]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[22]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[23]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[24]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[25]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[26]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[27]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[28]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[29]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[30]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWADDR[31]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe14]
set_property port_width 8 [get_debug_ports u_ila_0/probe14]
connect_debug_port u_ila_0/probe14 [get_nets [list {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[0]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[1]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[2]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[3]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[4]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[5]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[6]} {AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLEN[7]}]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe15]
set_property port_width 1 [get_debug_ports u_ila_0/probe15]
connect_debug_port u_ila_0/probe15 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe16]
set_property port_width 1 [get_debug_ports u_ila_0/probe16]
connect_debug_port u_ila_0/probe16 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARLOCK]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe17]
set_property port_width 1 [get_debug_ports u_ila_0/probe17]
connect_debug_port u_ila_0/probe17 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARREADY]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe18]
set_property port_width 1 [get_debug_ports u_ila_0/probe18]
connect_debug_port u_ila_0/probe18 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_ARVALID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe19]
set_property port_width 1 [get_debug_ports u_ila_0/probe19]
connect_debug_port u_ila_0/probe19 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe20]
set_property port_width 1 [get_debug_ports u_ila_0/probe20]
connect_debug_port u_ila_0/probe20 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWREADY]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe21]
set_property port_width 1 [get_debug_ports u_ila_0/probe21]
connect_debug_port u_ila_0/probe21 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_AWVALID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe22]
set_property port_width 1 [get_debug_ports u_ila_0/probe22]
connect_debug_port u_ila_0/probe22 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_BID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe23]
set_property port_width 1 [get_debug_ports u_ila_0/probe23]
connect_debug_port u_ila_0/probe23 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_BREADY]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe24]
set_property port_width 1 [get_debug_ports u_ila_0/probe24]
connect_debug_port u_ila_0/probe24 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_BVALID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe25]
set_property port_width 1 [get_debug_ports u_ila_0/probe25]
connect_debug_port u_ila_0/probe25 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe26]
set_property port_width 1 [get_debug_ports u_ila_0/probe26]
connect_debug_port u_ila_0/probe26 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RLAST]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe27]
set_property port_width 1 [get_debug_ports u_ila_0/probe27]
connect_debug_port u_ila_0/probe27 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RREADY]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe28]
set_property port_width 1 [get_debug_ports u_ila_0/probe28]
connect_debug_port u_ila_0/probe28 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_RVALID]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe29]
set_property port_width 1 [get_debug_ports u_ila_0/probe29]
connect_debug_port u_ila_0/probe29 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WLAST]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe30]
set_property port_width 1 [get_debug_ports u_ila_0/probe30]
connect_debug_port u_ila_0/probe30 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WREADY]]
create_debug_port u_ila_0 probe
set_property PROBE_TYPE DATA_AND_TRIGGER [get_debug_ports u_ila_0/probe31]
set_property port_width 1 [get_debug_ports u_ila_0/probe31]
connect_debug_port u_ila_0/probe31 [get_nets [list AS02MC04_axi_jtag_i/jtag_axi_0_M_AXI_WVALID]]
set_property C_CLK_INPUT_FREQ_HZ 300000000 [get_debug_cores dbg_hub]
set_property C_ENABLE_CLK_DIVIDER false [get_debug_cores dbg_hub]
set_property C_USER_SCAN_CHAIN 1 [get_debug_cores dbg_hub]
connect_debug_port dbg_hub/clk [get_nets clk]
