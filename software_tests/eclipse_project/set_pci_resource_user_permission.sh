#! /bin/bash
# Give other read/write permission on the PCI resources for all devices which match a vendor ID, and optional device ID.
# This allows users to map the resources via libpciaccess without needing any Linux driver for the device.
#
# This script can't be used when secure boot is enabled, since kernel_lockdown prevents use of direct PCI BAR access.

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
        # Found a requested device
        echo "Giving user permission to PCI resources for ${device_identification}"
        sudo chmod o+rw ${pci_dir}/resource[0-6]*

        # Enable the PCI device response in Memory space, if left disabled at boot 
        # (as seems to be the case when UEFI mode is enabled).
        #
        # If the PCI device is left with response in Memory space attempts to access the BARs results in:
        # - Reads returning all 0xff's
        # - Writes having no effect
        PCI_COMMAND_MEMORY=2 # Enable response in Memory space
        cmd=0x`setpci -s $(basename ${pci_dir}) COMMAND`
        if [ $((${cmd} & 0x${PCI_COMMAND_MEMORY})) -eq 0 ]
        then
            sudo setpci -s $(basename ${pci_dir}) COMMAND=${PCI_COMMAND_MEMORY}:${PCI_COMMAND_MEMORY}
            echo "Enabling reponse in Memory space for ${device_identification}"
        fi
    fi
done