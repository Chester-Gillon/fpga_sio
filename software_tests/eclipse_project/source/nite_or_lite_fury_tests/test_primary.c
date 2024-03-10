/*
 * @file test_primary.c
 * @date 5 Mar 2023
 * @author Chester Gillon
 * @brief The primary process for testing multi-process VFIO
 */

#include "vfio_access.h"
#include "identify_pcie_fpga_design.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#define MAX_SECONDARY_PROCESSES 8


/**
 * @brief Display the file descriptors which are open in the calling process
 * @details This will open a file descriptor to pass the procfs directory containing the file descriptors,
 *          which is suppressed from being displayed.
 * @param[in] process_name Reported to identify the calling process
 */
static void display_open_fds (const char *const process_name)
{
    DIR *fd_dir;
    struct dirent *fd_ent;
    char pid_fd_dir[PATH_MAX];
    char fd_ent_pathname[PATH_MAX];
    char pathname_of_fd[PATH_MAX];
    volatile size_t num_chars;

    snprintf (pid_fd_dir, sizeof (pid_fd_dir), "/proc/%d/fd", getpid ());
    fd_dir = opendir (pid_fd_dir);
    printf ("Contents of %s in %s:\n", pid_fd_dir, process_name);
    if (fd_dir != NULL)
    {
        fd_ent = readdir (fd_dir);
        while (fd_ent != NULL)
        {
            if (fd_ent->d_type == DT_LNK)
            {
                /* use of num_chars suppresses -Wformat-truncation, as suggested by
                 * https://stackoverflow.com/a/70938456/4207678 */
                num_chars = sizeof (fd_ent_pathname);
                snprintf (fd_ent_pathname, num_chars, "%s/%s", pid_fd_dir, fd_ent->d_name);

                ssize_t link_num_bytes = readlink (fd_ent_pathname, pathname_of_fd, sizeof (pathname_of_fd));
                if (link_num_bytes > 0)
                {
                    if (strncmp (pathname_of_fd, pid_fd_dir, (size_t) link_num_bytes) != 0)
                    {
                        printf ("  fd %s -> %.*s\n", fd_ent->d_name, (int) link_num_bytes, pathname_of_fd);
                    }
                }
            }

            fd_ent = readdir (fd_dir);
        }

        closedir (fd_dir);
    }
}

int main (int argc, char *argv[])
{
    int rc;
    fpga_designs_t designs;
    vfio_secondary_process_t secondary_processes[MAX_SECONDARY_PROCESSES] = {0};
    int argc_index;
    uint32_t num_secondary_processes = 0;

    if (argc == 1)
    {
        printf ("Usage: %s <secondary_process_1> [ <procesess_1_arg_1> .. <procesess_1_arg_N>] --\n", argv[0]);
        printf ("The arguments consist of secondary process executables to run, with optional arguments.\n");
        printf ("-- is used to delimit the start of the next process\n");
        exit (EXIT_FAILURE);
    }

    /* Get the list of secondary processes to launch, and any arguments, from the command line */
    argc_index = 1;
    num_secondary_processes = 0;
    while ((num_secondary_processes < MAX_SECONDARY_PROCESSES) && (argc_index < argc))
    {
        vfio_secondary_process_t *const secondary_process = &secondary_processes[num_secondary_processes];
        uint32_t num_secondary_arguments = 0;

        /* Store pathname of secondary process */
        snprintf (secondary_process->executable, sizeof (secondary_process->executable), "%s", argv[argc_index]);
        secondary_process->argv[num_secondary_arguments] = secondary_process->executable;
        num_secondary_arguments++;
        argc_index++;

        bool found_next_process = false;
        while ((argc_index < argc) && (!found_next_process))
        {
            if (strcmp (argv[argc_index], "--") == 0)
            {
                found_next_process = true;
            }
            else if (num_secondary_arguments < VFIO_SECONDARY_MAX_ARGC)
            {
                secondary_process->argv[num_secondary_arguments] = strdup (argv[argc_index]);
                num_secondary_arguments++;
            }
            argc_index++;
        }
        secondary_process->argv[num_secondary_arguments] = NULL;

        num_secondary_processes++;
    }

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Ignore Ctrl-C in the primary, to wait until the child processes have exited following forwarding of the Ctrl-C */
    struct sigaction action =
    {
        .sa_handler = SIG_IGN,
        .sa_flags = 0
    };

    rc = sigaction (SIGINT, &action, NULL);
    if (rc != 0)
    {
        printf ("sigaction() failed\n");
        exit (EXIT_FAILURE);
    }

    vfio_display_fds (&designs.vfio_devices);
    display_open_fds ("test_primary");

    vfio_launch_secondary_processes (&designs.vfio_devices, num_secondary_processes, secondary_processes);
    vfio_await_secondary_processes (num_secondary_processes, secondary_processes);

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
