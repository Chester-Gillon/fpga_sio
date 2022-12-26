# Build the dump_info program

project (dump_info C)

add_executable (dump_info_libpciaccess "dump_info_libpciaccess.c")
target_link_libraries (dump_info_libpciaccess pciaccess)

add_executable (dump_info_pciutils "dump_info_pciutils.c")
target_link_libraries (dump_info_pciutils pci)