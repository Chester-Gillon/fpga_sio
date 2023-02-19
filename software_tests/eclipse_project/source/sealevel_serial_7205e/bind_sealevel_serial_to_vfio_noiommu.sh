#! /bin/bash
# Bind a Sealevel Serial device vfio-pci driver, and give user access to the IOMMU group
# which the device is in. Forces use of NOIOMMU on the assumption run in a PCI which doesn't contain IOMMU support.
# This serves as an example of using VFIO to access a device when IOMMU support is available in the PC.

# Ensure the vfio module is loaded
if [ ! -e /sys/module/vfio ]
then
    echo "Loading vfio module and enabling NOIOMMU (this taints the Kernel)"
    sudo modprobe vfio enable_unsafe_noiommu_mode=1
fi

# Ensure the vfio-pci module is loaded
if [ ! -d /sys/bus/pci/drivers/vfio-pci ]
then
    echo "Loading vfio-pci module"
    sudo modprobe vfio-pci
fi

plx_vendor_id=0x10b5
plx_pci_vendor_files=$(grep -il ${plx_vendor_id} /sys/bus/pci/devices/*/vendor)
for plx_pci_vendor_file in ${plx_pci_vendor_files}
do
    plx_pci_dir=$(dirname ${plx_pci_vendor_file})
    plx_device=$(basename ${plx_pci_dir})
    subsys_vendor=$(< ${plx_pci_dir}/subsystem_vendor)
    subsys_id=$(< ${plx_pci_dir}/subsystem_device)

    if [ "${subsys_id}" == "0x3198" ] 
    then
        if [ -d ${plx_pci_dir}/driver ]
        then
            # If there is an existing device bound, then leave it.
            # I.e. don't try and re-build to vfio-pci
            existing_driver=`basename \`readlink ${plx_pci_dir}/driver\``
            echo "${plx_device} [${subsys_vendor#0x}:${subsys_id#0x}] is already bound to driver ${existing_driver}"
        else
            # Bind the device to the vfio-pci driver.
            echo vfio-pci | sudo tee ${plx_pci_dir}/driver_override
            echo ${plx_device} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
            echo "Bound vfio-pci driver to ${plx_device} [${subsys_vendor#0x}:${subsys_id#0x}]"

            if [ -a ${plx_pci_dir}/iommu_group ]
            then
                iommu_group=`basename \`readlink ${plx_pci_dir}/iommu_group\``
                sudo chmod o+rw /dev/vfio/noiommu-${iommu_group}
                echo "Giving user permission to IOMMU group noiommu-${iommu_group} for ${plx_device} [${subsys_vendor#0x}:${subsys_id#0x}]"
            fi
        fi
    fi
done