# Copy the generated bitstream and mask from the project directory which is under a .gitignore
# to the assumed root directory of the project, so that pre-built bitstreams may
# be added to GIT to allow verifying the FPGA.
#
# This is run as a tcl.post rule on bitsream generation.
# Can't seem to find a way to locate the enclosing project path,
# so has a hard coded assumption the generated bitstream is 3 directories below
# the root directory.
set bitstream [glob *.bit]
file copy -force $bitstream "../../.."

set mask  [glob *.msk]
file copy -force $mask "../../.."
