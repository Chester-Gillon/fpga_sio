# Build a library for using the Xilinx Quad SPI in user space using VFIO

project (xilinx_quad_spi C)

add_library (xilinx_quad_spi "xilinx_quad_spi.c")
add_library (xilinx_7_series_bitstream "xilinx_7_series_bitstream.c")

add_executable (quad_spi_flasher "quad_spi_flasher.c")
target_link_libraries (quad_spi_flasher xilinx_7_series_bitstream xilinx_quad_spi identify_pcie_fpga_design vfio_access transfer_timing)

add_executable (parse_bitstream_file "parse_bitstream_file.c")
target_link_libraries (parse_bitstream_file xilinx_7_series_bitstream identify_pcie_fpga_design xilinx_quad_spi vfio_access)

add_executable (xilinx_bit_to_bin "xilinx_bit_to_bin.c")
target_link_libraries (xilinx_bit_to_bin xilinx_7_series_bitstream identify_pcie_fpga_design xilinx_quad_spi vfio_access)