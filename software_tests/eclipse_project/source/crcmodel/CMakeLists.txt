# Build the CRC calculation library obtained from https://zlib.net/crc_v3.txt
#
# Changes were:
# a. Modified the DONE_STYLE section in crcmodel.h so that the underlying type is 64-bits.
# b. Modified literals in crcmodel.c to be unsigned to remove "may change the sign of the result" warnings.
#
# While crc model.h has MASK32, that macro isn't used.
#
# Status  : Copyright (C) Ross Williams, 1993. However, permission is
#           granted to make and distribute verbatim copies of this
# 
#           document provided that this information block and copyright
#           notice is included. Also, the C code modules included
#           in this document are fully public domain.

project (crcmodel C)

add_library (crcmodel "crcmodel.c")