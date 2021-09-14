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
done