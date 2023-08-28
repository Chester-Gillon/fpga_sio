# Build a library for using the Xilinx Quad SPI in user space using VFIO

add_library (xilinx_quad_spi "xilinx_quad_spi.c")
add_library (xilinx_7_series_bitstream "xilinx_7_series_bitstream.c")

add_executable (quad_spi_flasher "quad_spi_flasher.c")
target_link_libraries (quad_spi_flasher xilinx_7_series_bitstream xilinx_quad_spi fury_utils xilinx_xadc vfio_access transfer_timing
                       pci rt)

add_executable (parse_bitstream_file "parse_bitstream_file.c")
target_link_libraries (parse_bitstream_file xilinx_7_series_bitstream xilinx_quad_spi fury_utils xilinx_xadc vfio_access transfer_timing
                       pci rt)