#! /bin/bash
# Build the pciutils library from locally installed source, and install it where setup_cmake.sh can use it

if [ ! -d ~/pciutils ]
then
    echo "Error: Clone https://github.com/pciutils/pciutils into ~/pciutils to be able to build pciutils locally"
    exit 1
fi

# Remove any existing installation
rm -rf ~/pciutils_install/

pushd ~/pciutils

# Force a re-build
make clean

# Build pciutils locally with the options:
# a. Set ZLIB=no to disable the dependency on library "z"
# b. Set DNS=no to disable the dependency on library "resolv"
# c. Set IDSDIR to the directory in the local install where the pci.ids file can be read,
#    so that pci_lookup_name() works at runtime
make ZLIB=no DNS=no IDSDIR=~/pciutils_install/usr/local/share

# Install pciutils into a local directory
make DESTDIR=~/pciutils_install install install-lib

popd