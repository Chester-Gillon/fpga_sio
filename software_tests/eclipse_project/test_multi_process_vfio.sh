#! /bin/bash
# Perform a VFIO multi-process test by running different programs which perform VFIO access "in parallel".
# This was written when investigating mmap() sometimes failing with EBUSY when multiple processes were started.
#
# There is no check if mmap() fails, rely on manually checking the console output.

# Get the absolute path of the workspace root directory, which is the parent directory of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

# Command line argument is which build platform to run the executables for
case ${1}
in
    debug|coverage|release)
        platform=${1}
    ;;
    *)
       echo "usage: ${0} debug|coverage|release"
       exit 1
    ;;
esac

executable_root_dir=${SCRIPT_PATH}/bin/${platform}

# Start the manager as a daemon.
# The --once argument isn't used to allow for the test programs maybe being delayed in starting.
${executable_root_dir}/vfio_access/vfio_multi_process_manager --daemon

# Start test programs which use VFIO access in parallel. The first uses DMA and the rest only memory mapped access.
# To simplify this script there is no attempt to save the output from each program into a different file, the output
# is just inter-mingled on the console.
${executable_root_dir}/xilinx_dma_bridge_for_pcie/test_dma_descriptor_credits &
${executable_root_dir}/xilinx_quad_spi/quad_spi_flasher &
${executable_root_dir}/xilinx_sensors/display_sensor_values &
${executable_root_dir}/identify_pcie_fpga_design/display_identified_pcie_fpga_designs &
${executable_root_dir}/dump_info/dump_pci_info_vfio &

# Wait for client processes to exit
for job in `jobs -p`
do
echo $job
    wait $job
done

# Tell the manager to shutdown
kill -SIGINT `pgrep vfio_multi_proc`
