#! /bin/bash
# Bind all devices with a Xilinx vendor to the vfio-pci driver, and give user access to the IOMMU group
# which the device is in.

# Ensure the vfio-pci module is loaded
if [ ! -d /sys/bus/pci/drivers/vfio-pci ]
then
    echo "Loading vfio-pci module"
    sudo modprobe vfio-pci
fi

xilinx_vendor_id=0x10ee
xilinx_pci_vendor_files=$(grep -il ${xilinx_vendor_id} /sys/bus/pci/devices/*/vendor)
for xilinx_pci_vendor_file in ${xilinx_pci_vendor_files}
do
    xilinx_pci_dir=$(dirname ${xilinx_pci_vendor_file})
    xilinx_device=$(basename ${xilinx_pci_dir})
    subsys_vendor=$(< ${xilinx_pci_dir}/subsystem_vendor)
    subsys_id=$(< ${xilinx_pci_dir}/subsystem_device)

    if [ -d ${xilinx_pci_dir}/driver ]
    then
        # If there is an existing device bound, then leave it.
        # I.e. don't try and re-build to vfio-pci
        existing_driver=`basename \`readlink ${xilinx_pci_dir}/driver\``
        echo "${xilinx_device} [${subsys_vendor#0x}:${subsys_id#0x}] is already bound to driver ${existing_driver}"
    else
        # Bind the device to the vfio-pci driver.
        # This will fail with EINVAL if the IOMMU isn't enabled. I.e. the script doesn't attempt to use vfio-pci
        # in enable_unsafe_noiommu_mode
        echo vfio-pci | sudo tee ${xilinx_pci_dir}/driver_override
        echo ${xilinx_device} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
        echo "Bound vfio-pci driver to ${xilinx_device} [${subsys_vendor#0x}:${subsys_id#0x}]"

        if [ -a ${xilinx_pci_dir}/iommu_group ]
        then
            iommu_group=`basename \`readlink ${xilinx_pci_dir}/iommu_group\``
            sudo chmod o+rw /dev/vfio/${iommu_group}
            echo "Giving user permission to IOMMU group ${iommu_group} for ${xilinx_device} [${subsys_vendor#0x}:${subsys_id#0x}]"
        fi
    fi
done