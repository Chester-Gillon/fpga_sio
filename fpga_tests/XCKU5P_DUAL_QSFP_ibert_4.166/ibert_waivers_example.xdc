
############################################################


############################################################



create_waiver -internal -quiet -type CDC -id {CDC-1} -user ibert_ultrascale_gty -tags "1165692" -description "CDC-1 waiver for CPLL Calibration logic" \
                        -scope -from [get_ports {gty_refclk*p_i[*]}] \
						       -to [get_pins -quiet -filter {REF_PIN_NAME=~*D} -of_objects [get_cells -hierarchical -filter {NAME =~*QUAD*.u_q/u_common/U_COMMON_REGS/reg_*/I_EN_STAT_EQ1.U_STAT/xsdb_reg_reg[*]}]]



