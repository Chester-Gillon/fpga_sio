# Copy the generated device image from the project directory which is under a .gitignore
# to the assumed root directory of the project, so that pre-built device image may
# be added to GIT to allow loading.
#
# This is run as a tcl.post rule on device image generation.
# Can't seem to find a way to locate the enclosing project path,
# so has a hard coded assumption the generated device image is 3 directories below
# the root directory.
set device_image [glob *.pdi]
file copy -force $device_image "../../.."
