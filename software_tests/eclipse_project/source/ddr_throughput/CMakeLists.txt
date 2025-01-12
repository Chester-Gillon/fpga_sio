# Build the programs for testing DDR throughout

project (ddr_throughput C)

add_executable (measure_ddr_throughput "measure_ddr_throughput.c")
target_link_libraries (measure_ddr_throughput vfio_access transfer_timing)
