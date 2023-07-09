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
set (COMMON_FLAGS "${PLATFORM_CFLAGS} -Wall -Wconversion -fmessage-length=0")

set (CMAKE_C_FLAGS "${COMMON_FLAGS}")
set (CMAKE_CXX_FLAGS "${COMMON_FLAGS}")

# If there is a libpciaccess directory in the home directory link against that rather than the system version.
# CMake will set the rpath of executables for the shared objects in this directory.
set (LIBPCIACCESS_ROOT "$ENV{HOME}/libpciaccess")
if (EXISTS ${LIBPCIACCESS_ROOT})
    link_directories ("${LIBPCIACCESS_ROOT}/src/.libs")
    include_directories ("${LIBPCIACCESS_ROOT}/include")
endif()

# If there is a pciutils directory in the home directory link against that rather than the system version.
# This is a static library.
set (PCIUTILS_ROOT "$ENV{HOME}/pciutils_install")
if (EXISTS ${PCIUTILS_ROOT})
    link_directories ("${PCIUTILS_ROOT}/usr/local/lib")
    include_directories ("${PCIUTILS_ROOT}/usr/local/include")

    # Make the following CHECK_SYMBOL_EXISTS use the local pcutils directory for the search
    get_property(CMAKE_REQUIRED_INCLUDES DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
endif()

# Not available in all versions of libpci
CHECK_SYMBOL_EXISTS ("PCI_FILL_IOMMU_GROUP" pci/pci.h HAVE_PCI_FILL_IOMMU_GROUP)

# If there is a cmem_gdb_access directory in the home directory enable use of it for a physical memory allocator for
# using DMA in noiommu mode
set (CMEM_ROOT "$ENV{HOME}/cmem_gdb_access")
set (HAVE_CMEM FALSE)
if (EXISTS ${CMEM_ROOT})
    # Have to define HAVE_CMEM at the top level since impacts the size of the vfio_buffer_t structure
    # in the vfio_access.h API include file.
    include_directories ("${CMEM_ROOT}/cmem_test")
    add_definitions (-DHAVE_CMEM)
    set (HAVE_CMEM TRUE)
endif()

# If there is a embeddedsw directory in the home directory enable it for peripheral access
set (XILINX_EMBEDDEDSW_ROOT "$ENV{HOME}/embeddedsw")
set (HAVE_XILINX_EMBEDDEDSW FALSE)
if (EXISTS ${XILINX_EMBEDDEDSW_ROOT})
    set (HAVE_XILINX_EMBEDDEDSW TRUE)
    add_definitions (-DHAVE_XILINX_EMBEDDEDSW)
    include_directories("${XILINX_EMBEDDEDSW_ROOT}/XilinxProcessorIPLib/drivers/iic/src")
    include_directories("${XILINX_EMBEDDEDSW_ROOT}/lib/bsp/standalone/src/common")
    include_directories("${PROJECT_SOURCE_DIR}/xilinx_embeddedsw")
endif()

include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}/vfio_access")
include_directories("${PROJECT_SOURCE_DIR}/xilinx_dma_bridge_for_pcie")
include_directories("${PROJECT_SOURCE_DIR}/sealevel_serial_7205e")
include_directories("${PROJECT_SOURCE_DIR}/xilinx_axi_iic")
include_directories("${PROJECT_SOURCE_DIR}/xilinx_xadc")
include_directories("${PROJECT_SOURCE_DIR}/nite_or_lite_fury_tests")
if (${HAVE_XILINX_EMBEDDEDSW})
    add_subdirectory ("${PROJECT_SOURCE_DIR}/xilinx_embeddedsw")
endif()
if (${HAVE_PCI_FILL_IOMMU_GROUP})
    add_subdirectory ("${PROJECT_SOURCE_DIR}/vfio_access")
    add_subdirectory ("${PROJECT_SOURCE_DIR}/xilinx_dma_bridge_for_pcie")
    add_subdirectory ("${PROJECT_SOURCE_DIR}/xilinx_axi_iic")
    add_subdirectory ("${PROJECT_SOURCE_DIR}/i2c_probe")
    add_subdirectory ("${PROJECT_SOURCE_DIR}/xilinx_xadc")
    add_subdirectory ("${PROJECT_SOURCE_DIR}/xilinx_quad_spi")
endif()
add_subdirectory ("${PROJECT_SOURCE_DIR}/dump_info")
add_subdirectory ("${PROJECT_SOURCE_DIR}/memmapped_tests")
add_subdirectory ("${PROJECT_SOURCE_DIR}/probe_xilinx_ip")
add_subdirectory ("${PROJECT_SOURCE_DIR}/nite_or_lite_fury_tests")
add_subdirectory ("${PROJECT_SOURCE_DIR}/sealevel_serial_7205e")
add_subdirectory ("${PROJECT_SOURCE_DIR}/micromemory_5425_nvram")
