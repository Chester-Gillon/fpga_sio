#! /bin/bash
# Bind a Micro Memory MM-5425CN NVRAM device to the vfio-pci driver, and give user access to the IOMMU group
# which the device is in.

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

mm_vendor_id=1332
mm_nvram_device_id=5425
${SCRIPT_PATH}/../../bind_device_to_vfio.sh ${mm_vendor_id} ${mm_nvram_device_id}
