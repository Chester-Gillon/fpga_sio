# Build the dump_info program

project (dump_info C)

add_executable (dump_info "dump_info.c")
target_link_libraries (dump_info pciaccess)