#! /bin/bash
# Bind a Sealevel Serial device to the vfio-pci driver, and give user access to the IOMMU group
# which the device is in.

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

# Bind to the local bus part of the PEX8311 device used by the Sealevel Serial device
plx_vendor_id=10b5
plx_9056_device_id=9056
${SCRIPT_PATH}/../../bind_device_to_vfio.sh ${plx_vendor_id} ${plx_9056_device_id}
