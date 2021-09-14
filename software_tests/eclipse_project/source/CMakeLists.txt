# Top level CMake file

project (software_tests C CXX)
cmake_minimum_required (VERSION 2.8)

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

# -fmessage-length=0 is to allow Eclipse traceback for error messages
set (COMMON_FLAGS "${PLATFORM_CFLAGS} -Wall -fmessage-length=0")

set (CMAKE_C_FLAGS "${COMMON_FLAGS}")
set (CMAKE_CXX_FLAGS "${COMMON_FLAGS}")

add_subdirectory ("${PROJECT_SOURCE_DIR}/dump_info")
