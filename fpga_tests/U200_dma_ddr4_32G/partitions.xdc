# Placement constraints for:
# 1. The SmartConnect axi_smc used to connect the XDMA configuration registers for each of
#    the 4 DDR4 memory channels.
# 2. The SmartConnect axi_smc_1 used to connect the XDMA data to each of the 4 DDR4 memory channels.
#
# Placement is:
# a. Switchboard is on SLR1, along with DDR4 memory channels 1 and 2, plus PCIe.
# b. DDR4 memory channel 0, SmartConnect master 0, is on SLR0.
# c. DDR4 memory channel 3, SmartConnect master 3, is on SLR2.
#
# This file was created based upon the "Constraining the Core" section of PG247 (v1.0).
# It sets SLR crossing based placment for the SmartConnect switchboard and masters 0 and 3,
# which are on different SLRs. 

# SLR1 of SmartConnect core Switchboard
create_pblock pblock_smartconnect_switchboard
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_ar_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_aw_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_b_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_r_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_w_node/inst/inst_si_handler]]

add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_ar_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_aw_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_b_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_r_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_w_node/inst/inst_si_handler]]

add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_ar_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_aw_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_b_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_r_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_w_node/inst/inst_si_handler]]

add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_ar_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_aw_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_b_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_r_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_smartconnect_switchboard] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_w_node/inst/inst_si_handler]]

# SLR1 clock region as found by get_clock_regions -of_objects [get_slrs SLR1]
resize_pblock [get_pblocks pblock_smartconnect_switchboard] -add {CLOCKREGION_X0Y5:CLOCKREGION_X5Y9}


# SLR0 of Endpoint Slave 0 IP
create_pblock pblock_axi_slave_0_on_mi
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_ar_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_aw_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_b_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_r_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m00_nodes/m00_w_node/inst/inst_mi_handler]]

add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_ar_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_aw_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_b_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_r_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_0_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m00_nodes/m00_w_node/inst/inst_mi_handler]]

# SLR0 clock region as found by get_clock_regions -of_objects [get_slrs SLR0]
resize_pblock [get_pblocks pblock_axi_slave_0_on_mi] -add {CLOCKREGION_X0Y0:CLOCKREGION_X5Y4}



# SLR2 of Endpoint Slave 3 IP
create_pblock pblock_axi_slave_3_on_mi
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_ar_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_aw_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_b_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_r_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc/inst/m03_nodes/m03_w_node/inst/inst_mi_handler]]

add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_ar_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_aw_node/inst/inst_mi_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_b_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_r_node/inst/inst_si_handler]]
add_cells_to_pblock [get_pblocks pblock_axi_slave_3_on_mi] \
[get_cells [list U200_dma_ddr4_32G_i/axi_smc_1/inst/m03_nodes/m03_w_node/inst/inst_mi_handler]]

# SLR2 clock region as found by get_clock_regions -of_objects [get_slrs SLR2]
resize_pblock [get_pblocks pblock_axi_slave_3_on_mi] -add {CLOCKREGION_X0Y10:CLOCKREGION_X5Y14}
