# Build the memory mapped test programs

project (memmapped_tests C)
cmake_minimum_required (VERSION 2.8)

add_executable (memmapped_persistence "memmapped_persistence.c")
target_link_libraries (memmapped_persistence pciaccess)
