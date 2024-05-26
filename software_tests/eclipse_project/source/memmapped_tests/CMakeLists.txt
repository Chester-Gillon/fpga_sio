# Build the memory mapped test programs

project (memmapped_tests C)

add_executable (memmapped_persistence_libpciaccess "memmapped_persistence_libpciaccess.c")
target_link_libraries (memmapped_persistence_libpciaccess pciaccess rt)

add_executable (time_memmapped_libpciaccess "time_memmapped_libpciaccess.c")
target_link_libraries (time_memmapped_libpciaccess vfio_access transfer_timing pciaccess)

add_executable (memmapped_persistence_vfio "memmapped_persistence_vfio.c")
target_link_libraries (memmapped_persistence_vfio vfio_access)
