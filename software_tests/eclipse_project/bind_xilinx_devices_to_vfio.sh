#! /bin/bash
# Bind all devices with a Xilinx vendor to the vfio-pci driver, and give user access to the IOMMU group
# which the device is in.

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

xilinx_vendor_id=10ee
${SCRIPT_PATH}/bind_device_to_vfio.sh ${xilinx_vendor_id}
