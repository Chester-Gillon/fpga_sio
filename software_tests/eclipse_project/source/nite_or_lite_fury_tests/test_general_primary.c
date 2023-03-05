/*
 * @file test_general_primary.c
 * @date 5 Mar 2023
 * @author Chester Gillon
 * @brief The primary process for testing multi-process VFIO access without using DMA
 */

#include "vfio_access.h"
#include "fury_utils.h"

#include <stdlib.h>
#include <stdio.h>

#include <libgen.h>

int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;
    vfio_secondary_process_t secondary_process = {0};

    /* Define the secondary process to launch:
     * a. The executable is in the same directory as this primary process.
     * b. The only argument is the executable name. */
    char *const my_realpath = realpath (argv[0], NULL);
    char *const my_dir = dirname (my_realpath);
    snprintf (secondary_process.executable, sizeof (secondary_process.executable), "%s/%s", my_dir, "test_general_secondary");
    secondary_process.argv[0] = secondary_process.executable;
    secondary_process.argv[1] = NULL;

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    vfio_display_fds (&vfio_devices);
    display_open_fds ("test_general_primary");

    vfio_launch_secondary_processes (&vfio_devices, 1, &secondary_process);
    vfio_await_secondary_processes (1, &secondary_process);

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
