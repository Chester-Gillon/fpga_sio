/*
 * @file test_general_primary.c
 * @date 5 Mar 2023
 * @author Chester Gillon
 * @brief The primary process for testing multi-process VFIO to Fury devices
 */

#include "vfio_access.h"
#include "fury_utils.h"

#include <stdlib.h>
#include <stdio.h>

#include <libgen.h>

#define MAX_SECONDARY_PROCESSES 8

int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;
    vfio_secondary_process_t secondary_processes[MAX_SECONDARY_PROCESSES] = {0};
    uint32_t num_secondary_processes = 0;

    if (argc == 1)
    {
        printf ("Usage: %s <secondary_process_1> [ .. <secondary_process_N>\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    /* The command line specifies which secondary processes to launch, the executables for which are in the same
     * directory as this primary processes. The only argument is the executable name. */
    char *const my_realpath = realpath (argv[0], NULL);
    char *const my_dir = dirname (my_realpath);
    for (int argc_index = 1; (num_secondary_processes < MAX_SECONDARY_PROCESSES) && (argc_index < argc); argc_index++)
    {
        vfio_secondary_process_t *const secondary_process = &secondary_processes[num_secondary_processes];

        snprintf (secondary_process->executable, sizeof (secondary_process->executable), "%s/%s", my_dir, argv[argc_index]);
        secondary_process->argv[0] = secondary_process->executable;
        secondary_process->argv[1] = NULL;
        num_secondary_processes++;
    }

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    vfio_display_fds (&vfio_devices);
    display_open_fds ("test_primary");

    vfio_launch_secondary_processes (&vfio_devices, num_secondary_processes, secondary_processes);
    vfio_await_secondary_processes (num_secondary_processes, secondary_processes);

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
