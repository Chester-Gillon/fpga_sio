#! /bin/bash
# Bind all PCI devices which match a vendor ID, and optional device ID, to the vfio-pci driver and give user access
# to the IOMMU group which the device is in.
# Can work with and without IOMMU support in the PC. If no IOMMU support then enables enable_unsafe_noiommu_mode

# Parse command line arguments.
# The vendor_id is mandatory, and the device_id is optional.
vendor_id=$1
requested_device_id=$2

# Check the vendor and device ID are in a form which can be be compared against /sys/bus/pci/devices entries
if [ -z "${vendor_id}" ]
then
    echo "Usage: vendor_id [device_id]"
    exit 1
fi

if ! [[ ${vendor_id} =~ ^[0-9a-f]{4,4}$ ]]
then
    echo "Error: vendor_id must be 4 hex lower case digits"
    exit 1 
fi
vendor_id=0x${vendor_id}

if [ -n "${requested_device_id}" ]
then
    if ! [[ ${requested_device_id} =~ ^[0-9a-f]{4,4}$ ]]
    then
        echo "Error: device_id must be 4 hex lower case digits"
       exit 1 
    fi
    requested_device_id=0x${requested_device_id}
fi

# Determine if the IOMMU is supported on the PC
iommus=$(ls -C /sys/class/iommu)
if [ -z "${iommus}" ]
then
    echo "No IOMMUs present"
    iommu_group_prefix="noiommu-"
else
    echo "IOMMU devices present: ${iommus}"
    iommu_group_prefix=""
fi

# Iterate over all PCI devices for the specified vendor
vendor_files=$(grep -il ${vendor_id} /sys/bus/pci/devices/*/vendor)
for vendor_file in ${vendor_files}
do
    pci_dir=$(dirname ${vendor_file})
    pci_dbdf=$(basename ${pci_dir}) # dbdf = PCI <domain>:<bus>:<device>:<function>

    device_id=$(< ${pci_dir}/device)
    subsys_vendor=$(< ${pci_dir}/subsystem_vendor)
    subsys_id=$(< ${pci_dir}/subsystem_device)

    device_identification="${pci_dbdf} ${vendor_id#0x}:${device_id#0x} [${subsys_vendor#0x}:${subsys_id#0x}]"

    if [[ (-z "${requested_device_id}") || ("${requested_device_id}" == "${device_id}") ]]
    then
        # Found a requested device, need to ensure the vfio module(s) are loaded
        if [ -z "${iommus}" ]
        then
            # With no IOMMUs present, then load the vifo-pci module with NOIOMMU mode 

            # Ensure the vfio module is loaded
            if [ ! -e /sys/module/vfio ]
            then
                echo "Loading vfio module and enabling NOIOMMU (this taints the Kernel)"
                sudo modprobe vfio enable_unsafe_noiommu_mode=1
            fi

            # Ensure NOIOMMU mode is enabled.
            # This test is required if the vfio module is built-it, since the previous step won't have loaded the module
            if [ -e /sys/module/vfio/parameters/enable_unsafe_noiommu_mode ]
            then
                existing_mode=$(< /sys/module/vfio/parameters/enable_unsafe_noiommu_mode)
                if [ ${existing_mode} != "Y" ]
                then
                	echo "Enabling NOIOMMU (this taints the Kernel)"
                    echo "Y" | sudo tee /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
            	fi
            fi

            # Ensure the vfio-pci module is loaded
            if [ ! -d /sys/bus/pci/drivers/vfio-pci ]
            then
                echo "Loading vfio-pci module"
                sudo modprobe vfio-pci
            fi
        else
            # With IOMMUs present ensure the vfio-pci module is loaded, which loads the vfio module with IOMMU support
            if [ ! -d /sys/bus/pci/drivers/vfio-pci ]
            then
                echo "Loading vfio-pci module"
                sudo modprobe vfio-pci
            fi
        fi

        if [ -d ${pci_dir}/driver ]
        then
            # If there is an existing device bound, then leave it.
            # I.e. don't try and re-build to vfio-pci
            existing_driver=`basename \`readlink ${pci_dir}/driver\``
            echo "${device_identification} is already bound to driver ${existing_driver}"
        else
            # Bind the device to the vfio-pci driver.
            echo vfio-pci | sudo tee ${pci_dir}/driver_override > /dev/null
            echo ${pci_dbdf} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind > /dev/null
            exit_status=$?
            if [ ${exit_status} -eq 0 ]
            then
                echo "Bound vfio-pci driver to ${device_identification}"

                if [ -a ${pci_dir}/iommu_group ]
                then
                    # Wait for the group file to exist, due to a delay before the file exists following binding the device
                    # to the vfio-pci. Without this wait, sometimes the chmod would fail reporting the group file didn't exist.
                    iommu_group=`basename \`readlink ${pci_dir}/iommu_group\``
                    iommu_group_file=/dev/vfio/${iommu_group_prefix}${iommu_group}
                    echo "Waiting for ${iommu_group_file} to be created"
                    until [ -a ${iommu_group_file} ]
                    do
                        sleep 1
                    done

                    sudo chmod o+rw ${iommu_group_file}
                    echo "Giving user permission to IOMMU group ${iommu_group_prefix}${iommu_group} for ${device_identification}"
                fi
            else
                # This may happen since vfio-pci can only bind to devices with a "normal" PCI header type.
                # I.e. unable to bind to bridges.
                echo "Failed to bind vfio-pci driver to ${device_identification}"
            fi
        fi
    fi
done
