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

    # Enable the PCI device if left disabled at boot.
    # While there is the libpciaccess pci_device_enable() function which can do this from a program, giving
    # user write permission to the enable file isn't sufficient to use the user permission to enable the device.
    # Think the user process needs the CAP_SYS_ADMIN capability.
    enable_value=$(< ${xilinx_pci_dir}/enable)
    if [ ${enable_value} -eq 0 ]
    then
        echo "Enabling PCI device for $(basename ${xilinx_pci_dir}) [${subsys_vendor#0x}:${subsys_id#0x}]"
        echo 1 | sudo tee ${xilinx_pci_dir}/enable
    fi
done