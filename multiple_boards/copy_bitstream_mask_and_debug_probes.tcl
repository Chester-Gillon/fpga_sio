# Copy the generated bitstream, mask and debug from the project directory which is under a .gitignore
# to the assumed root directory of the project so that:
# a) Pre-built bitstreams be added to GIT to allow programming and verifying the FPGA.
# b) Pre-built debug probes may allow the ILA to be used.
#
# This is run as a tcl.post rule on bitsream generation.
# Can't seem to find a way to locate the enclosing project path,
# so has a hard coded assumption the generated bitstream is 3 directories below
# the root directory.
set bitstream [glob *.bit]
file copy -force $bitstream "../../.."

set mask [glob *.msk]
file copy -force $mask "../../.."

# @todo Filters out the debug_nets.ltx file which seems to have the same contents
#       as the project specific filename.
set debug_probes [lsearch -inline -all -not [glob *.ltx] debug_nets.ltx]
file copy -force $debug_probes "../../.."
 