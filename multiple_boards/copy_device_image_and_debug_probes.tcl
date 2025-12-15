# Copy the generated device image and debug probes from the project directory which is under a .gitignore
# to the assumed root directory of the project so that:
# a) Pre-built device images be added to GIT to allow programming the FPGA.
# b) Pre-built debug probes may allow the ILA to be used.
#
# This is run as a tcl.post rule on device image generation.
# Can't seem to find a way to locate the enclosing project path,
# so has a hard coded assumption the generated device image is 3 directories below
# the root directory.
set device_image [glob *.pdi]
file copy -force $device_image "../../.."

# @todo Filters out the debug_nets.ltx file which seems to have the same contents
#       as the project specific filename.
set debug_probes [lsearch -inline -all -not [glob *.ltx] debug_nets.ltx]
file copy -force $debug_probes "../../.."
 
