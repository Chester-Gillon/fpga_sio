#! /bin/bash
# Wrapper script to call program_bitstream.tcl using the Vivado lab tools under Linux

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

# Set the Vivado lab tools version used
VIVADO_VERSION=2025.2
VIVADO_LAB_EXE=/opt/Xilinx/${VIVADO_VERSION}/Vitis/bin/vivado_lab
if [ ! -x ${VIVADO_LAB_EXE} ]
then
   echo "${VIVADO_LAB_EXE} executable not found"
   exit 1
fi

# Parse command line arguments.
# Simply checks that the supplied bitstream filename is readable; doesn't verify that an actual bitstream file.
if [ "$#" -ne 1 ]
then
   echo "Usage: $0 <bitstream_filename>"
   exit 1
fi

bitstream_filename=$1
if [ ! -r ${bitstream_filename} ]
then
   echo "Error: ${bitstream_filename} does not exist"
   exit 1
fi

# Call the Vivado lab tool to run the TCL script to perform the programming of the supplied bitstream filename.
# The lab tool arguments are set to disable the writing of log and journal files.
#
# -notrace suppresses the echoing of the TCL commands. 
# Not shown in the vivado_lab help, but found in https://support.xilinx.com/s/article/46102?language=en_US
${VIVADO_LAB_EXE} -notrace -nolog -nojournal -mode batch -source ${SCRIPT_PATH}/program_bitstream.tcl -tclargs ${bitstream_filename}
