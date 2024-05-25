#! /bin/bash
# Use sudo to set capabilities to allow executables to use VFIO as a normal user.
# The reason this is done as a standalone script rather than a CMake custom build rule are:
# 1. To avoid complications with running sudo when the CMake build is used in Eclipse.
# 2. To allow switch capabilities for VFIO to allow for if the PC currently has an IOMMU supported or not.

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

# Adjust the capabilities on an executable to match those required, either to add or remove capabilities.
# Only needs to take sudo action if the existing capabilities need to change.
function adjust_capabilities
{
    local executable=${SCRIPT_PATH}/bin/${platform}/$1
    local required_capabilities=$2

    # Only attempt to adjust capabilities on executables which exist, in case not all platforms have been built
    if [ -e ${executable} ]
    then
        if [ -z ${required_capabilities} ]
        then
            # Need to remove any existing capabilities
            local existing_capabilities=$(getcap ${executable})
            if [ -n "${existing_capabilities}" ]
            then
                echo "Removing all capabilities from ${executable}"
                sudo setcap -q -r ${executable}
            fi
        else
            # Need to set capabilities
            setcap -q -v ${required_capabilities} ${executable}
            local exit_status=$?
            if [ ${exit_status} -ne 0 ]
            then
                echo "Setting ${required_capabilities} on ${executable}"
                sudo setcap -q ${required_capabilities} ${executable}
            fi
        fi
    fi
}

# Determine if the IOMMU is supported on the PC
iommus=$(ls -C /sys/class/iommu)
noiommu_access_required=0
if [ -z "${iommus}" ]
then
    echo "No IOMMUs present"
    noiommu_access_required=1
fi

# Iterate over all possible platforms the executables have been built for.
platforms="debug release coverage"
for platform in ${platforms}
do
    # Requires cap_sys_admin capability to read PCIe capabilities regardless of if an IOMMU is in use
    adjust_capabilities dump_info/dump_pci_info_libpciaccess "cap_sys_admin=ep"
    adjust_capabilities dump_info/dump_pci_info_pciutils "cap_sys_admin=ep"

    # Adjust the capabilities for the executables which link to the vfio_access library since they can perform VFIO access.
    if [ ${noiommu_access_required} -eq 1 ]
    then
        # With no IOMMU present, need the cap_sys_rawio capability to open devices using VFIO
        vfio_access_capabilities="cap_sys_rawio=ep"
    else
        # With no IOMMU present no capabilities are needed to open devices using VFIO
        vfio_access_capabilities=""
    fi
    for executable in $(< ${SCRIPT_PATH}/bin/${platform}/vfio_access_usage.txt)
    do
        adjust_capabilities ${executable} ${vfio_access_capabilities}
    done

    # This performs VFIO access directly, rather than using the vfio_access library
    adjust_capabilities dump_info/display_vfio_information ${vfio_access_capabilities}
done