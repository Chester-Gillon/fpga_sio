# Top level CMake file

project (software_tests C CXX)
# This project was originally created in CentOS 6 with cmake 2.18.12.2, and the minimum version specified below
# was 2.8.
#
# When using cmake 3.20.2 in AlmaLinux got the message:
#   CMake Deprecation Warning at CMakeLists.txt:4 (cmake_minimum_required):
#     Compatibility with CMake < 2.8.12 will be removed from a future version of
#     CMake.
#
# To support the older and newer CMake versions bumped the minimum up to 2.8.12.
# @todo on the effect of compatiblity between different CMake versions and the functionality in the different CMake
# files.
cmake_minimum_required (VERSION 2.8.12)

# Set platform build flags
if ("${PLATFORM_TYPE}" STREQUAL "debug")
   set (PLATFORM_CFLAGS "-g")
elseif ("${PLATFORM_TYPE}" STREQUAL "release")
   set (PLATFORM_CFLAGS "-O3 -g")
elseif ("${PLATFORM_TYPE}" STREQUAL "coverage")
   set (PLATFORM_CFLAGS "-g --coverage -fprofile-update=atomic --save-temps")
else()
   message ("Unknown platform")
endif()

include (CheckSymbolExists)

# -fmessage-length=0 is to allow Eclipse traceback for error messages
set (COMMON_FLAGS "${PLATFORM_CFLAGS} -Wall -fmessage-length=0")

set (CMAKE_C_FLAGS "${COMMON_FLAGS}")
set (CMAKE_CXX_FLAGS "${COMMON_FLAGS}")

# If there is a libpciaccess directory in the home directory link against that rather than the system version.
# CMake will set the rpath of executables for the shared objects in this directory.
set (LIBPCIACCESS_ROOT "$ENV{HOME}/libpciaccess")
if (EXISTS ${LIBPCIACCESS_ROOT})
    link_directories ("${LIBPCIACCESS_ROOT}/src/.libs")
    include_directories ("${LIBPCIACCESS_ROOT}/include")
endif()

# Not available in all versions of libpci
CHECK_SYMBOL_EXISTS ("PCI_FILL_IOMMU_GROUP" pci/pci.h HAVE_PCI_FILL_IOMMU_GROUP)

include_directories("${PROJECT_SOURCE_DIR}")

add_subdirectory ("${PROJECT_SOURCE_DIR}/dump_info")
add_subdirectory ("${PROJECT_SOURCE_DIR}/memmapped_tests")
