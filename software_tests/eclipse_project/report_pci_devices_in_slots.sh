#! /bin/bash
# Script to call dump_info_libpciaccess with the identities of all PCIe devices in slots,
# for correlating error reporting related fields.
# Write the result to a file containing the current date/time as part of the name. 

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

report_filename=${SCRIPT_PATH}/bin/$(date "+%Y%m%dT%H%M%S")_device_status.log
mellanox_dev_id=15b3 # A connect-X in PCIe slot 1
nvidia_dev_id=10de # Graphics card in PCIe slot 2
xilinx_dev_id=10ee # FPGA cards in PCIe slot 3 and M.2
serial_id=1c00:3253 # Dual serial port in slot 4
intel_x722_id=8086:37d0 # Intel-X722 in slot 5
samsung_nvme_id=144d:a804 # Samsung NVMe in M.2

${SCRIPT_PATH}/bin/debug/dump_info/dump_info_libpciaccess \
${mellanox_dev_id} ${nvidia_dev_id} ${xilinx_dev_id} ${serial_id} ${intel_x722_id} ${samsung_nvme_id} > ${report_filename}
echo "PCI device status saved to ${report_filename}"