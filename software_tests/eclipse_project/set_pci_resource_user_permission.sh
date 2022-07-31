#! /bin/bash
# Give other read/write permission on the PCI resources for all devices with a Xilinx vendor.
# This allows users to map the resources via libpciaccess without needing any Linux driver for the device.

xilinx_vendor_id=0x10ee
xilinx_pci_vendor_files=$(grep -il ${xilinx_vendor_id} /sys/bus/pci/devices/*/vendor)
for xilinx_pci_vendor_file in ${xilinx_pci_vendor_files}
do
    xilinx_pci_dir=$(dirname ${xilinx_pci_vendor_file})
    subsys_vendor=$(< ${xilinx_pci_dir}/subsystem_vendor)
    subsys_id=$(< ${xilinx_pci_dir}/subsystem_device)
    echo "Giving user permission to PCI resources for $(basename ${xilinx_pci_dir}) [${subsys_vendor#0x}:${subsys_id#0x}]"
    sudo chmod o+rw ${xilinx_pci_dir}/resource[0-6]*

    # Enable the PCI device response in Memory space, if left disabled at boot 
    # (as seems to be the case when UEFI mode is enabled).
    #
    # If the PCI device is left with response in Memory space attempts to access the BARs results in:
    # - Reads returning all 0xff's
    # - Writes having no effect
    PCI_COMMAND_MEMORY=2 # Enable response in Memory space
    cmd=0x`setpci -s $(basename ${xilinx_pci_dir}) COMMAND`
    if [ $((${cmd} & 0x${PCI_COMMAND_MEMORY})) -eq 0 ]
    then
        sudo setpci -s $(basename ${xilinx_pci_dir}) COMMAND=${PCI_COMMAND_MEMORY}:${PCI_COMMAND_MEMORY}
        echo "Enabling reponse in Memory space for $(basename ${xilinx_pci_dir}) [${subsys_vendor#0x}:${subsys_id#0x}]"
    fi
done