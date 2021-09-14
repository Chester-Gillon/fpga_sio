# Build the dump_info program

project (dump_info C)
cmake_minimum_required (VERSION 2.8)

add_executable (dump_info "dump_info.c")
target_link_libraries (dump_info pciaccess)