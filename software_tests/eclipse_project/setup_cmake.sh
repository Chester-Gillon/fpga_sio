#! /bin/bash
# Set up the cmake build environment under Linux, to a clean state

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

# Prefer GNAT Community edition 2021, but since that can't be run on CentOS 6 due to glibc dependencies fall back
# to 2020 if the later version isn't installed.
GCC_PATH=/opt/GNAT/2021/bin
if [ ! -d ${GCC_PATH} ]
then
    GCC_PATH=/opt/GNAT/2020/bin
fi

# Create the native platforms
platforms="debug release coverage"
for platform in ${platforms}
do
   build_dir=${SCRIPT_PATH}/bin/${platform}
   rm -rf ${build_dir}
   mkdir -p ${build_dir}
   pushd ${build_dir}
   cmake -G "Unix Makefiles" -DPLATFORM_TYPE=${platform} ${SCRIPT_PATH}/source -DCMAKE_C_COMPILER=${GCC_PATH}/gcc -DCMAKE_CXX_COMPILER=${GCC_PATH}/g++
   popd
done
