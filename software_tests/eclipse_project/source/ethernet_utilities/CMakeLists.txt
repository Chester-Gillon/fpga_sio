# Build Ethernet utilities which are not specific to one specific implementation
#
# The crc32.h file was generated from https://git.bues.ch/git/crcgen.git using:
#    ~/crcgen/crcgen -c -a CRC-32 -b 8 -S > crc32.h

project (ethernet_utilities C)

add_library (ethernet_fcs "ethernet_fcs.c")