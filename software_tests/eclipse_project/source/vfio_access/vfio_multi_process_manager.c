/*
 * @file vfio_multi_process_manager.c
 * @date 20 Jul 2024
 * @author Chester Gillon
 * @brief Manager process to support multiple process use of devices accessed using VFIO
 * @details
 *
 */

#define _GNU_SOURCE /* For ppoll() */

#include "vfio_access.h"
#include "vfio_access_private.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <signal.h>


/* The maximum number of client, assuming 4 bi-directional channels on each device.
 * This simplifies the code by having a compile time maximum number of clients. */
#define VFIO_MAX_CLIENTS (MAX_VFIO_DEVICES * 8)


#define VFIO_MAX_FDS (1 /* listening_socket_fd */ + VFIO_MAX_CLIENTS)


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"isolate_iommu_groups", no_argument, NULL, 0},
    {"once", no_argument, NULL, 0},
    {"daemon", no_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};


/* Contains data about one client */
typedef struct
{
    /* Set true when the client is connected, and the other fields are defined */
    bool connected;
    /* The socket file descriptor for the client */
    int client_socket_fd;
    /* Indicates which devices are in used by the client. This is used to:
     * a. Open a device when the first client requests access.
     * b. Close a device once no clients require access. */
    bool devices_used[MAX_VFIO_DEVICES];
    /* Contains the PID of the client, for reporting diagnostics */
    bool credentials_valid;
    struct ucred credentials;
    /* Contains the executable of the client, for reporting diagnostics */
    bool exe_pathname_valid;
    char exe_pathname[PATH_MAX];
    /* A string combining the PID and exe_pathname, when known, used to describe the client in diagnostic messages */
    char description[10 + PATH_MAX];
} vfio_client_data_t;


/* Defines the content for the manager process */
typedef struct
{
    /* File descriptor used a listening socket to accept client connections */
    int listening_socket_fd;
    /* Data for each connected client. The index into this array is used identify the client */
    vfio_client_data_t clients[VFIO_MAX_CLIENTS];
    /* One more than the highest index in clients[] for a connected client. Used to limit how many
     * clients have to be iterated over. */
    uint32_t maximum_used_clients;
    /* Contains the open IOMMU groups, IOMMU containers and VFIO devices */
    vfio_devices_t vfio_devices;
    /* Used to unblock SIGINT only during ppoll() and not other blocking calls */
    sigset_t signal_mask_during_ppoll;
    /* When true the manager is still running */
    bool running;
    /* When true the manager is pending shutdown once there are no more connected clients */
    bool shutdown_pending;
} vfio_manager_context_t;


/* Command line argument which specifies to shutdown the first time the number of connected clients drops to zero */
static bool arg_once;


/* Command line argument which specifies to run the manager as a daemon */
static bool arg_run_as_daemon;


/* Set from a signal handle to record a request to shutdown the manager */
static volatile bool shutdown_requested;


/**
 * @brief Signal handler to request the manager exits
 * @param[in] sig Not used
 */
static void shutdown_request_handler (int signum)
{
    shutdown_requested = true;
}


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("--isolate_iommu_groups\n");
    printf ("  Causes each IOMMU group to use it's own container\n");
    printf ("--once\n");
    printf ("  Shutdown the first time the number of connected clients drop to zero\n");
    printf ("--daemon\n");
    printf ("  Run in the background as a daemon. The manager initialisation completes\n");
    printf ("  before the daemon in spawned, so by the time returns the manager is ready\n");
    printf ("  to accept clients.\n");

    exit (EXIT_FAILURE);
}


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    int opt_status;

    do
    {
        int option_index = 0;

        opt_status = getopt_long (argc, argv, "", command_line_options, &option_index);
        if (opt_status == '?')
        {
            display_usage ();
        }
        else if (opt_status >= 0)
        {
            const struct option *const optdef = &command_line_options[option_index];

            if (optdef->flag != NULL)
            {
                /* Argument just sets a flag */
            }
            else if (strcmp (optdef->name, "isolate_iommu_groups") == 0)
            {
                vfio_enable_iommu_group_isolation ();
            }
            else if (strcmp (optdef->name, "once") == 0)
            {
                arg_once = true;
            }
            else if (strcmp (optdef->name, "daemon") == 0)
            {
                arg_run_as_daemon = true;
            }
            else
            {
                /* This is a program error, and shouldn't be triggered by the command line options */
                fprintf (stderr, "Unexpected argument definition %s\n", optdef->name);
                exit (EXIT_FAILURE);
            }
        }
    } while (opt_status != -1);
}


/**
 * @brief Update the maximum_used_clients.
 * @details Also is the point at which can trigger a pending shutdown when the "once" command line has been used and
 *          after the first client has connected.
 * @param[in/out] context The context to update
 */
static void update_maximum_used_clients (vfio_manager_context_t *const context)
{
    context->maximum_used_clients = 0;
    for (uint32_t client_index = 0; client_index < VFIO_MAX_CLIENTS; client_index++)
    {
        if (context->clients[client_index].connected)
        {
            context->maximum_used_clients = client_index + 1;
        }
    }

    if (arg_once && !context->shutdown_pending && (context->maximum_used_clients > 0))
    {
        context->shutdown_pending = true;
    }
}


/**
 * @brief Perform the initialisation for the VFIO manager
 * @details This opens all IOMMU groups which have VFIO devices bound, and creates containers.
 *          The actual VFIO devices are not yet opened.
 * @param[out] context The initialised manager context
 */
static bool initialise_vfio_manager (vfio_manager_context_t *const context)
{
    int known_fields;
    DIR *vfio_dir;
    struct dirent *vfio_dir_entry;
    uint32_t iommu_group_numeric;
    char iommu_group_text[64];
    struct pci_dev *dev;
    int rc;
    struct sockaddr_un my_addr;
    int saved_errno;

    /* Initialise to no VFIO devices or container */
    memset (context, 0, sizeof (*context));
    context->vfio_devices.devices_usage = VFIO_DEVICES_USAGE_MANAGER;
    context->vfio_devices.num_containers = 0;
    context->vfio_devices.num_devices = 0;
    context->listening_socket_fd = -1;
    for (uint32_t client_index = 0; client_index < VFIO_MAX_CLIENTS; client_index++)
    {
        context->clients[client_index].connected = false;
        context->clients[client_index].client_socket_fd = -1;
    }
    update_maximum_used_clients (context);

    /* The VFIO manager doesn't need to use the cmem driver */
    context->vfio_devices.cmem_usage = VFIO_CMEM_USAGE_NONE;

    /* Initialise PCI access using the defaults */
    context->vfio_devices.pacc = pci_alloc ();
    if (context->vfio_devices.pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (context->vfio_devices.pacc);

    /* Scan the entire bus */
    pci_scan_bus (context->vfio_devices.pacc);

    /* Locate all IOMMU groups which can be managed, in that they are bound to VFIO */
    uint32_t num_expected_iommu_groups = 0;
    uint32_t num_expected_vfio_devices = 0;
    vfio_dir = opendir (VFIO_ROOT_PATH);
    if (vfio_dir != NULL)
    {
        for (vfio_dir_entry = readdir (vfio_dir); vfio_dir_entry != NULL; vfio_dir_entry = readdir (vfio_dir))
        {
            if ((sscanf (vfio_dir_entry->d_name, "%" SCNu32, &iommu_group_numeric) == 1) ||
                (sscanf (vfio_dir_entry->d_name, "noiommu-%" SCNu32, &iommu_group_numeric) == 1))
            {
                snprintf (iommu_group_text, sizeof (iommu_group_text), "%" PRIu32, iommu_group_numeric);
                num_expected_iommu_groups++;

                /* Iterate over all PCI devices, attempting to add as VFIO devices those which use the IOMMU group.
                 * This will cause the creation of IOMMU containers and opening of the IOMMU group.
                 * The VFIO_DEVICES_USAGE_MANAGER setting means the VFIO device won't yet be opened. */
                const int required_fields = PCI_FILL_IDENT;
                for (dev = context->vfio_devices.pacc->devices; dev != NULL; dev = dev->next)
                {
                    known_fields = pci_fill_info (dev, required_fields);
                    if ((known_fields & required_fields) == required_fields)
                    {
                        const char *const iommu_group = vfio_get_iommu_group (dev);

                        if ((iommu_group != NULL) && (strcmp (iommu_group_text, iommu_group) == 0))
                        {
                            num_expected_vfio_devices++;
                            if (context->vfio_devices.num_devices < MAX_VFIO_DEVICES)
                            {
                                /* The device is initialised to not being DMA capable, may be changed when a client requests
                                 * the device is opened. */
                                (void) open_vfio_device (&context->vfio_devices, dev, VFIO_DEVICE_DMA_CAPABILITY_NONE);
                            }
                        }
                    }
                }
            }
        }
    }
    closedir (vfio_dir);

    /* Verify that the expected number of IOMMU groups and devices were opened.
     * open_vfio_device() will have output diagnostic about all failures except for exceeding the compile time maximum
     * number of VFIO devices. */
    uint32_t num_opened_iommu_groups = 0;
    for (uint32_t container_index = 0; container_index < context->vfio_devices.num_containers; container_index++)
    {
        num_opened_iommu_groups += context->vfio_devices.containers[container_index].num_iommu_groups;
    }
    bool success = (num_opened_iommu_groups > 0) &&
            (context->vfio_devices.num_devices == num_expected_vfio_devices) &&
            (num_opened_iommu_groups == num_expected_iommu_groups);

    if (success)
    {
        /* All IOMMU groups have been opened. Create a listening socket to accept clients.
         * If another manager process is already running, attempting to open an IOMMU group would have failed with EBUSY
         * and the PID of the existing manager reported. Therefore, the bind() is not expected to fail with the namespace
         * already being in use. */
        context->listening_socket_fd = socket (AF_UNIX, SOCK_SEQPACKET, 0);
        if (context->listening_socket_fd == -1)
        {
            fprintf (stderr, "socket() failed\n");
            success = false;
        }

        if (success)
        {
            memset (&my_addr, 0, sizeof (my_addr));
            my_addr.sun_family = AF_UNIX;
            memcpy (my_addr.sun_path, VFIO_MULTI_PROCESS_MANAGER_ABSTRACT_NAMESPACE,
                    sizeof (VFIO_MULTI_PROCESS_MANAGER_ABSTRACT_NAMESPACE));
            const socklen_t socklen = offsetof(struct sockaddr_un, sun_path[1]) + (socklen_t) strlen (&my_addr.sun_path[1]);
            errno = 0;
            rc = bind (context->listening_socket_fd, (struct sockaddr *) &my_addr, socklen);
            saved_errno = errno;
            if (rc != 0)
            {
                fprintf (stderr, "bind() failed %s\n", strerror (saved_errno));
                success = false;
            }
        }

        if (success)
        {
            errno = 0;
            rc = listen (context->listening_socket_fd, VFIO_MAX_CLIENTS);
            saved_errno = errno;
            if (rc != 0)
            {
                fprintf (stderr, "listen() failed %s\n", strerror (saved_errno));
                success = false;
            }
        }
    }
    else
    {
        /* Display a summary of why the initialisation of IOMMU groups failed */
        if (num_opened_iommu_groups == 0)
        {
            printf ("No available IOMMU groups to manage\n");
        }
        else
        {
            if (num_expected_vfio_devices > MAX_VFIO_DEVICES)
            {
                printf ("Number of VFIO devices %u bound to IOMMU groups exceeds the compile time maximum of %u\n",
                        num_expected_vfio_devices, MAX_VFIO_DEVICES);
            }
            printf ("Only opened %u out of %u IOMMU groups, and %u out of %u expected VFIO devices\n",
                    num_opened_iommu_groups, num_expected_iommu_groups,
                    context->vfio_devices.num_devices, num_expected_vfio_devices);
        }
    }

    return success;
}


/**
 * @brief Finalise the VFIO manager, closing the IOMMU groups and freeing the containers.
 * @details The VFIO devices are expected to have been closed before this function is called.
 */
static void finalise_vfio_manager (vfio_manager_context_t *const context)
{
    int rc;

    close_vfio_devices (&context->vfio_devices);

    if (context->listening_socket_fd != -1)
    {
        rc = close (context->listening_socket_fd);
        if (rc != 0)
        {
            printf ("close() failed\n");
        }
    }
}


/**
 * @brief Accept a connection from a client, allocating a free index for the client
 * @param[in/out] context The manager context to accept the connection for
 */
static void accept_client_connection (vfio_manager_context_t *const context)
{
    int rc;
    const int new_client_fd = accept (context->listening_socket_fd, NULL, NULL);

    if (new_client_fd != -1)
    {
        if (context->maximum_used_clients < VFIO_MAX_CLIENTS)
        {
            /* Allocate a free index for the new client */
            bool client_added = false;

            for (uint32_t client_index = 0; !client_added && (client_index < VFIO_MAX_CLIENTS); client_index++)
            {
                if (!context->clients[client_index].connected)
                {
                    vfio_client_data_t *const client = &context->clients[client_index];

                    client->connected = true;
                    client->client_socket_fd = new_client_fd;
                    for (uint32_t device_index = 0; device_index < MAX_VFIO_DEVICES; device_index++)
                    {
                        client->devices_used[device_index] = false;
                    }

                    /* Attempt to obtain identity information of the connected client, for reporting diagnostic information.
                     * Failure to obtain the identity isn't considered an error, in case lack of permissions. */
                    socklen_t sock_len = sizeof (client->credentials);

                    memset (client->description, 0, sizeof (client->description));
                    rc = getsockopt (new_client_fd, SOL_SOCKET, SO_PEERCRED, &client->credentials, &sock_len);
                    client->credentials_valid = rc == 0;
                    client->exe_pathname_valid = false;
                    if (client->credentials_valid)
                    {
                        char pid_exe_symlink[PATH_MAX];
                        ssize_t exe_pathname_len;

                        snprintf (client->description, sizeof (client->description), " PID %d", client->credentials.pid);
                        snprintf (pid_exe_symlink, sizeof (pid_exe_symlink), "/proc/%d/exe", client->credentials.pid);
                        exe_pathname_len = readlink (pid_exe_symlink, client->exe_pathname, sizeof (client->exe_pathname) - 1);
                        if (exe_pathname_len > 0)
                        {
                            client->exe_pathname[exe_pathname_len] = '\0';
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
                            /* Suppress warning of the form, which was seen with GCC 10.3.1 when compiling for the release platform:
                             *   warning: 'snprintf' argument 5 may overlap destination object 'context' [-Wrestrict]
                             *
                             * "Bug 102919 - spurious -Wrestrict warning for sprintf into the same member array as argument plus offset"
                             * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102919 suggests this is a spurious warning
                             */
                            snprintf (client->description, sizeof (client->description),
                                    " PID %d %s", client->credentials.pid, client->exe_pathname);
#pragma GCC diagnostic pop
                            client->exe_pathname_valid = true;
                        }
                    }
                    update_maximum_used_clients (context);
                    client_added = true;
                }
            }
        }
        else
        {
            /* Compile time maximum number of clients already connected */
            printf ("Unable to accept new client as client_socket_fds[] array full\n");
            rc = close (new_client_fd);
            if (rc != 0)
            {
                printf ("close() failed\n");
                exit (EXIT_FAILURE);
            }
        }
    }
    else
    {
        /* Handle accept() failing as just the client exiting before the manager attempted to accept */
        printf ("Client exited before accept()\n");
    }
}


/**
 * @brief Check the groups on a container, and unset them from the container if there are no open devices in the group.
 * @details Unsetting the group from a container is what triggers the IOMMU to be disabled.
 * @param[in/out[ container The container to check for inactive groups on
 * @return The number of groups in the container which are still active
 */
static uint32_t unset_container_on_inactive_groups (vfio_iommu_container_t *const container)
{
    uint32_t num_active_groups;
    uint32_t num_devices_open_in_group;
    int rc;

    num_active_groups = 0;
    for (uint32_t group_index = 0; group_index < container->num_iommu_groups; group_index++)
    {
        vfio_iommu_group_t *const group = &container->iommu_groups[group_index];

        /* Count the number of devices currently open in the group */
        num_devices_open_in_group = 0;
        for (uint32_t device_index = 0; device_index < container->vfio_devices->num_devices; device_index++)
        {
            if ((container->vfio_devices->devices[device_index].group == group) &&
                (container->vfio_devices->devices[device_index].device_fd >= 0))
            {
                num_devices_open_in_group++;
            }
        }

        if (num_devices_open_in_group == 0)
        {
            /* When no devices are open, check if the group has a container set */
            group->group_status.argsz = sizeof (group->group_status);
            rc = ioctl (group->group_fd, VFIO_GROUP_GET_STATUS, &group->group_status);
            if (rc != 0)
            {
                printf ("FIO_GROUP_GET_STATUS failed : %s\n", strerror (-rc));
                exit (EXIT_FAILURE);
            }

            /* If the group has a container set, unset it. When the final container in the group is unset
             * the IOMMU will be disabled. */
            if ((group->group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) != 0)
            {
                rc = ioctl (group->group_fd, VFIO_GROUP_UNSET_CONTAINER);
                if (rc != 0)
                {
                    printf ("VFIO_GROUP_UNSET_CONTAINER failed : %s\n", strerror (-rc));
                    exit (EXIT_FAILURE);
                }
            }
        }
        else
        {
            num_active_groups++;
        }
    }

    return num_active_groups;
}


/**
 * @brief Called after a device is closed to disable IOMMU containers which are no longer needed.
 * @details
 *   Disabling the IOMMU containers will free any IOVA allocations which were left behind following an unclean shutdown
 *   of a client.
 * @param[in/out] context The manager context to disable unused containers for
 */
static void disable_unused_containers (vfio_manager_context_t *const context)
{
    uint32_t num_active_groups;
    uint32_t num_allocated_regions;
    size_t num_allocated_bytes;

    /* Iterate over all containers */
    for (uint32_t container_index = 0; container_index < context->vfio_devices.num_containers; container_index++)
    {
        vfio_iommu_container_t *const container = &context->vfio_devices.containers[container_index];

        /* Check any containers which are currently enabled */
        if (container->container_enabled)
        {
            /* Determine which groups in the container are active, in that have one of more device still open */
            num_active_groups = unset_container_on_inactive_groups (container);

            if (num_active_groups == 0)
            {
                /* The container is no longer used by any groups.
                 * If all IOVA allocations were freed by the clients there should be no allocations so report diagnostics
                 * if still some outstanding allocations. */
                num_allocated_regions = 0;
                num_allocated_bytes = 0;
                for (uint32_t region_index = 0; region_index < container->num_iova_regions; region_index++)
                {
                    const vfio_iova_region_t *const region = &container->iova_regions[region_index];

                    if (region->allocated)
                    {
                        num_allocated_regions++;
                        num_allocated_bytes += (region->end + 1) - region->start;
                    }
                }

                if (num_allocated_regions > 0)
                {
                    printf ("Disabling container is freeing %u IOVA regions of %zu bytes\n",
                            num_allocated_regions, num_allocated_bytes);
                }

                /* The container is now disabled as has been unset from all groups. Clear the list of regions and free the
                 * iommu_info since are no longer needed. If the container is required by a further client, it will be re-enabled. */
                container->num_iova_regions = 0;
                free (container->iommu_info);
                container->iommu_info = NULL;
                container->container_enabled = false;
            }
        }
    }
}


/**
 * @brief Close a device which was used by one client. The actual device is only closed once no longer used by any client.
 * @param[in/out] context The manager context to close the device for
 * @param[in] client_index Which client to close the device for
 * @param[in] device_index Which device to close for the client
 */
static void close_device_for_client (vfio_manager_context_t *const context,
                                     const uint32_t client_index, const uint32_t device_index)
{
    int rc;
    int saved_errno;
    vfio_client_data_t *const client = &context->clients[client_index];

    /* Mark the client as no longer using the device */
    client->devices_used[device_index] = false;

    /* Determine if the device is still in use by any other clients */
    bool device_still_used = false;
    for (uint32_t scan_index = 0; !device_still_used && (scan_index < VFIO_MAX_CLIENTS); scan_index++)
    {
        device_still_used = context->clients[scan_index].devices_used[device_index];
    }

    if (!device_still_used)
    {
        /* Once the device is no longer used by any client, then close the device */
        vfio_device_t *const device = &context->vfio_devices.devices[device_index];

        errno = 0;
        rc = close (device->device_fd);
        saved_errno = errno;
        if (rc != 0)
        {
            fprintf (stderr, "close (%s) failed : %s\n", device->device_name, strerror (saved_errno));
            exit (EXIT_FAILURE);
        }
        device->device_fd = -1;
        device->dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE;
    }
}


/**
 * @brief Close the connection to a client
 * @details This will close any devices still in used by the client, and attempt to free IOVA allocations, in case the
 *          client connection has been closed following a client crash rather than a clean shutdown.
 * @param[in/out] context The manager context to close the client connection for
 * @param[in] client_index Identifies the client to close the connection for
 */
static void close_client_connection (vfio_manager_context_t *const context, const uint32_t client_index)
{
    vfio_client_data_t *const client = &context->clients[client_index];
    int rc;

    /* If the client didn't free all the IOVA allocations it made then:
     * a. Report diagnostics.
     * b. The manager is unable to call VFIO_IOMMU_UNMAP_DMA, the ioctl() returns success but the dma_unmap.size returned
     *    is zero meaning failed to unmap the DMA created by the client process.
     * c. Can't clear the IOVA regions which were allocated for the client, since if a new client attempts to map them
     *    the VFIO_IOMMU_MAP_DMA fails with EPERM.
     * d. disable_unused_containers() will free the outstanding IOVA allocations once the container is no longer used
     *    by any IOMMU groups. */
    uint32_t num_outstanding_regions = 0;
    size_t num_outstanding_bytes = 0;
    bool region_outstanding;
    for (uint32_t container_index = 0; container_index < context->vfio_devices.num_containers; container_index++)
    {
        vfio_iommu_container_t *const container = &context->vfio_devices.containers[container_index];

        do
        {
            region_outstanding = false;
            for (uint32_t region_index = 0; !region_outstanding && (region_index < container->num_iova_regions); region_index++)
            {
                vfio_iova_region_t region = container->iova_regions[region_index];
                const size_t region_size = (region.end + 1) - region.start;

                if (region.allocated && (client_index == region.allocating_client_id))
                {
                    /* Leave the IOVA region as allocated, but set the client ID which performed the allocation to an
                     * invalid value. This is so that if the same client ID is re-used this function won't re-report
                     * the same IOVA regions.  */
                    container->iova_regions[region_index].allocating_client_id = UINT32_MAX;
                    num_outstanding_regions++;
                    num_outstanding_bytes += region_size;
                }
            }
        } while (region_outstanding);
    }
    if (num_outstanding_regions > 0)
    {
        printf ("Client%s still had %u IOVA regions with %zu bytes allocated at client connection close\n",
                client->description, num_outstanding_regions, num_outstanding_bytes);
    }

    /* If the client didn't close the devices it opened, close them now */
    for (uint32_t device_index = 0; device_index < context->vfio_devices.num_devices; device_index++)
    {
        if (client->devices_used[device_index])
        {
            printf ("Client%s still had device %s open at client connection close\n",
                    client->description, context->vfio_devices.devices[device_index].device_name);
            close_device_for_client (context, client_index, device_index);
        }
    }

    disable_unused_containers (context);

    /* Close the socket for the client */
    rc = close (client->client_socket_fd);
    if (rc != 0)
    {
        printf ("close() failed\n");
    }
    client->client_socket_fd = -1;
    client->connected = false;
    update_maximum_used_clients (context);
}


/**
 * @brief Find a VFIO device being managed, by a PCI identity received from a client
 * @param[in] context The manager context used to find the requested device
 * @param[in] device_id The device the client has requested
 * @param[out] device_index The index for the found device, used to track which clients use the device.
 * @return A pointer to the device found in the context, or NULL if not found
 */
static vfio_device_t *find_client_requested_device (vfio_manager_context_t *const context,
                                                    const vfio_device_identity_t *const device_id,
                                                    uint32_t *const device_index)
{
    for (*device_index = 0; (*device_index < context->vfio_devices.num_devices); (*device_index)++)
    {
        vfio_device_t *const candidate_device = &context->vfio_devices.devices[*device_index];

        if ((candidate_device->pci_dev->domain == device_id->domain) &&
            (candidate_device->pci_dev->bus    == device_id->bus   ) &&
            (candidate_device->pci_dev->dev    == device_id->dev   ) &&
            (candidate_device->pci_dev->func   == device_id->func  )   )
        {
            return candidate_device;
        }
    }

    printf ("Client requested device %04x:%02x:%02x.%x which isn't being managed\n",
            device_id->domain, device_id->bus, device_id->dev, device_id->func);

    *device_index = 0;
    return NULL;
}


/**
 * @brief Find a container being managed, but an identity received from a client
 * @param[in] context The manager context used to find the requested container
 * @param[in] container_id Identifies the container requested by the client
 * @return A pointer to the container found in the context, or NULL if not found
 */
static vfio_iommu_container_t *find_client_requested_container (vfio_manager_context_t *const context,
                                                                const uint32_t container_id)
{
    if (container_id < context->vfio_devices.num_containers)
    {
        return &context->vfio_devices.containers[container_id];
    }
    else
    {
        printf ("Out of range container_id %u received\n", container_id);
        return NULL;
    }
}


/**
 * @brief Process a request from a connected client to open a VFIO device, sending a reply
 * @details This opens the VFIO device on the first client which requests it, and has the effect of VFIO reseting the device
 * @param[in/out] context The manager context to update with the request
 * @param[in] client_index Identifies which client sent the request, to update the list of in use devices
 * @param[in] request The request received from the client, which contains the VFIO device to open
 */
static void process_open_device_request (vfio_manager_context_t *const context, const uint32_t client_index,
                                         const vfio_open_device_request_t *const request)
{
    vfio_client_data_t *const client = &context->clients[client_index];
    uint32_t device_index;
    uint32_t group_index;
    vfio_manage_messages_t tx_buffer = {0};
    vfio_open_device_reply_fds_t vfio_fds =
    {
        .container_fd = -1,
        .device_fd = -1
    };

    tx_buffer.open_device_reply.msg_id = VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY;
    tx_buffer.open_device_reply.success = false;

    /* Search for the requested device known to the manager.
     * This could potentially fail if a new VFIO device was bound after the manager initialised, and before a client was started. */
    vfio_device_t *const device = find_client_requested_device (context, &request->device_id, &device_index);
    if (device != NULL)
    {
        /* The VFIO device is known to the manager */
        if (device->device_fd < 0)
        {
            /* Ensure the IOMMU groups have a container set, which can happen when re-opening a device */
            tx_buffer.open_device_reply.success = true;
            for (group_index = 0;
                 tx_buffer.open_device_reply.success && (group_index < device->group->container->num_iommu_groups);
                 group_index++)
            {
                tx_buffer.open_device_reply.success = vfio_ensure_iommu_container_set_for_group (device->group);
            }

            /* The VFIO device is not already open. Need to open it, using the dma_capability requested by the client */
            if (tx_buffer.open_device_reply.success)
            {
                device->dma_capability = request->dma_capability;
                tx_buffer.open_device_reply.success = open_vfio_device_fd (device);
            }
        }
        else
        {
            /* The VFIO device is already open. Different clients may not need DMA capability for the same device,
             * so update the DMA capability and enable bus master as required.
             * A32 only DMA capability takes precedence. */
            if (request->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A32)
            {
                device->dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A32;
            }
            else if (device->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_NONE)
            {
                device->dma_capability = request->dma_capability;
            }
            enable_bus_master_for_dma (device);
            tx_buffer.open_device_reply.success = true;
        }

        if (tx_buffer.open_device_reply.success)
        {
            /* On success complete the reply and indicate the client is using the device */
            tx_buffer.open_device_reply.iommu_type = device->group->container->iommu_type;
            tx_buffer.open_device_reply.num_iommu_groups = device->group->container->num_iommu_groups;
            for (group_index = 0; group_index < device->group->container->num_iommu_groups; group_index++)
            {
                snprintf (tx_buffer.open_device_reply.iommu_group_names[group_index],
                        sizeof (tx_buffer.open_device_reply.iommu_group_names[group_index]),
                        "%s", device->group->container->iommu_groups[group_index].iommu_group_name);
            }
            tx_buffer.open_device_reply.container_id = device->group->container->container_id;
            vfio_fds.device_fd = device->device_fd;
            vfio_fds.container_fd = request->container_fd_required ? device->group->container->container_fd : -1;
            client->devices_used[device_index] = true;
        }
    }

    if (tx_buffer.open_device_reply.success)
    {
        /* Send successful reply, which includes the file descriptors as ancillary information */
        vfio_send_manage_message (client->client_socket_fd, &tx_buffer, &vfio_fds);
    }
    else
    {
        /* Send failure reply, without ancillary information */
        vfio_send_manage_message (client->client_socket_fd, &tx_buffer, NULL);
    }
}


/**
 * @brief Process a request from a connected client to close a VFIO device, sending a reply
 * @param[in/out] context The manager context to update with the request
 * @param[in] client_index Identifies which client sent the request, to update the list of in use devices
 * @param[in] request The request received from the client, which contains the VFIO device to close
 */
static void process_close_device_request (vfio_manager_context_t *const context, const uint32_t client_index,
                                          const vfio_close_device_request_t *const request)
{
    vfio_client_data_t *const client = &context->clients[client_index];
    uint32_t device_index;
    vfio_manage_messages_t tx_buffer = {0};

    tx_buffer.close_device_reply.msg_id = VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REPLY;
    tx_buffer.close_device_reply.success = false;
    vfio_device_t *const device = find_client_requested_device (context, &request->device_id, &device_index);
    if (device != NULL)
    {
        if (client->devices_used[device_index])
        {
            close_device_for_client (context, client_index, device_index);
            tx_buffer.close_device_reply.success = true;
        }
        else
        {
            printf ("Request for client to close a device which the client isn't using\n");
        }
    }

    disable_unused_containers (context);

    vfio_send_manage_message (client->client_socket_fd, &tx_buffer, NULL);
}


/**
 * @brief Process a request from a connected client to perform an IOVA allocation, sending a reply
 * @param[in/out] context The manager context to update with the request
 * @param[in] client_index Identifies which client sent the request, to track the allocations from the client
 * @param[in] request The request received from the client, which contains the requested size of the allocation
 */
static void process_allocate_iova_request (vfio_manager_context_t *const context, const uint32_t client_index,
                                           const vfio_allocate_iova_request_t *const request)
{
    vfio_client_data_t *const client = &context->clients[client_index];
    vfio_manage_messages_t tx_buffer = {0};
    vfio_iova_region_t region;

    tx_buffer.allocate_iova_reply.msg_id = VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REPLY;
    tx_buffer.allocate_iova_reply.success = false;
    vfio_iommu_container_t *const container = find_client_requested_container (context, request->container_id);
    if (container != NULL)
    {
        allocate_iova_region_direct (container, request->dma_capability, request->requested_size,
                client_index, &region);
        tx_buffer.allocate_iova_reply.start = region.start;
        tx_buffer.allocate_iova_reply.end = region.end;
        tx_buffer.allocate_iova_reply.success = region.allocated;
    }

    vfio_send_manage_message (client->client_socket_fd, &tx_buffer, NULL);
}


/**
 * @brief Process a request from a connect client to free an IOVA region, sending a reply
 * @param[in/out] context The manager context to update with the request
 * @param[in] client_index Identifies which client sent the request, to validate the request
 * @param[in] request The request received from the client, which contains the VFIO region to free
 */
static void process_free_iova_request (vfio_manager_context_t *const context, const uint32_t client_index,
                                       const vfio_free_iova_request_t *const request)

{
    vfio_client_data_t *const client = &context->clients[client_index];
    vfio_manage_messages_t tx_buffer = {0};

    tx_buffer.free_iova_reply.msg_id = VFIO_MANAGE_MSG_ID_FREE_IOVA_REPLY;
    tx_buffer.free_iova_reply.success = false;
    vfio_iommu_container_t *const container = find_client_requested_container (context, request->container_id);
    if (container != NULL)
    {
        bool region_to_free_exists = false;

        /* Verify that the IOVA region the client is requesting to free matches a region the client has allocated */
        for (uint32_t region_index = 0; !region_to_free_exists && (region_index < container->num_iova_regions); region_index++)
        {
            const vfio_iova_region_t *const existing_region = &container->iova_regions[region_index];

            region_to_free_exists = existing_region->allocated &&
                    (existing_region->allocating_client_id == client_index) &&
                    (request->start == existing_region->start) &&
                    (request->end == existing_region->end);
        }

        if (region_to_free_exists)
        {
            /* Free the requested region */
            vfio_iova_region_t free_region =
            {
                .start = request->start,
                .end = request->end,
                .allocating_client_id = 0,
                .allocated = false
            };

            update_iova_regions (container, &free_region);
            tx_buffer.free_iova_reply.success = true;
        }
        else
        {
            printf ("Client attempted to free VFIO region start=%zu end=%zu which isn't covered by its existing allocations\n",
                    request->start, request->end);
        }
    }

    vfio_send_manage_message (client->client_socket_fd, &tx_buffer, NULL);
}

/**
 * @brief Process a request from a client to obtain exclusive access to VFIO
 * @details This only returns when the client has completed the exclusive access, which is what means the manager
 *          doesn't attempt to service any other clients during the interval of exclusive access.
 * @param[in/out] context The manager context. Is only changed if have to close the client connection if the client
 *                        crashes during the exclusive access.
 * @param[in] client_index Identifies which client sent the request
 */
static void process_exclusive_access_request (vfio_manager_context_t *const context, const uint32_t client_index)
{
    vfio_client_data_t *const client = &context->clients[client_index];
    vfio_manage_messages_t tx_buffer = {0};
    vfio_manage_messages_t rx_buffer;
    bool valid_message;

    /* Tell the client it has exclusive access */
    tx_buffer.msg_id = VFIO_MANAGE_MSG_ID_EXCLUSIVE_ACCESS_ALLOWED;
    vfio_send_manage_message (client->client_socket_fd, &tx_buffer, NULL);

    /* Block waiting for the client to indicate it has completed the exclusive access.
     * While waiting the manager won't attempt to process messages from other clients. */
    valid_message = vfio_receive_manage_message (client->client_socket_fd, &rx_buffer, NULL) &&
            (rx_buffer.msg_id == VFIO_MANAGE_MSG_ID_EXCLUSIVE_ACCESS_COMPLETED);
    if (!valid_message)
    {
        /* The client sent an unexpected message, or has crashed */
        close_client_connection (context, client_index);
    }
}


/**
 * @brief The run loop for the VFIO manager
 * @param[in/out] context The initialised context to manage.
 *                        On entry the IOMMU groups have been created and the listening socket created
 */
static void run_vfio_manager (vfio_manager_context_t *const context)
{
    struct pollfd poll_fds[VFIO_MAX_FDS] = {0};
    uint32_t num_fds;
    uint32_t client_index;
    int num_ready_fds;
    int saved_errno;
    vfio_manage_messages_t rx_buffer;
    bool valid_message;
    bool close_connection;

    /* Run servicing requests from clients, until requested to shutdown */
    context->shutdown_pending = false;
    context->running = true;
    while (context->running)
    {
        /* Create the list of fds to poll as the listening socket and all currently connected clients.
         * clients[] might be sparse, but entries for unconnected clients have a value of -1 and poll()
         * ignores any fds with a negative value. */
        num_fds = 0;
        poll_fds[num_fds].fd = context->listening_socket_fd;
        poll_fds[num_fds].events = POLLIN;
        num_fds++;
        for (client_index = 0; client_index < context->maximum_used_clients; client_index++)
        {
            const vfio_client_data_t *const client = &context->clients[client_index];

            poll_fds[num_fds].fd = client->connected ? client->client_socket_fd : -1;
            poll_fds[num_fds].events = POLLIN;
            num_fds++;
        }

        /* Wait for a socket to be readable with no timeout */
        errno = 0;
        num_ready_fds = ppoll (poll_fds, num_fds, NULL, &context->signal_mask_during_ppoll);
        saved_errno = errno;
        if ((num_ready_fds <= 0) && (saved_errno != EINTR) && (saved_errno != EAGAIN))
        {
            fprintf (stderr, "poll() failed : %s\n", strerror (saved_errno));
            exit (EXIT_FAILURE);
        }

        /* Process ready sockets */
        if (num_ready_fds > 0)
        {
            for (uint32_t fd_index = 0; fd_index < num_fds; fd_index++)
            {
                if ((poll_fds[fd_index].revents & POLLIN) != 0)
                {
                    if (fd_index == 0)
                    {
                        /* First fd index is the listening socket to accept clients */
                        accept_client_connection (context);
                    }
                    else
                    {
                        /* Convert the index in poll_fds[] into the client index, by a simple offset */
                        const uint32_t client_index = fd_index - 1;

                        close_connection = false;
                        valid_message = vfio_receive_manage_message (poll_fds[fd_index].fd, &rx_buffer, NULL);
                        if (valid_message)
                        {
                            switch (rx_buffer.msg_id)
                            {
                            case VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST:
                                process_open_device_request (context, client_index, &rx_buffer.open_device_request);
                                break;

                            case VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST:
                                process_close_device_request (context, client_index, &rx_buffer.close_device_request);
                                break;

                            case VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST:
                                process_allocate_iova_request (context, client_index, &rx_buffer.allocate_iova_request);
                                break;

                            case VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST:
                                process_free_iova_request (context, client_index, &rx_buffer.free_iova_request);
                                break;

                            case VFIO_MANAGE_MSG_ID_EXCLUSIVE_ACCESS_REQUEST:
                                process_exclusive_access_request (context, client_index);
                                break;

                            default:
                                printf ("Received unexpected message ID %u for manager\n", rx_buffer.msg_id);
                                valid_message = false;
                                break;
                            }
                        }
                        else
                        {
                            /* The client connection is closed if no valid message was read.
                             * vfio_receive_manage_message() has reported a diagnostic message if the message validation
                             * checks failed. */
                            close_connection = true;
                        }

                        if (close_connection)
                        {
                            close_client_connection (context, client_index);
                        }
                    }
                }
            }
        }

        if (shutdown_requested && !context->shutdown_pending)
        {
            context->shutdown_pending = true;
            if (context->maximum_used_clients > 0)
            {
                printf ("\nManager will shutdown once there are no connected clients\n");
            }
        }

        if (context->shutdown_pending && (context->maximum_used_clients == 0))
        {
            context->running = false;
        }
    }

    printf ("\nShutting down\n");
}


/**
 * @brief Install a Ctrl-C signal handler, used to request the manager is shutdown
 * @details We want the signal to interrupt up the process when blocked in a ppoll() call waiting for a socket to be readable,
 *          but not any other blocking calls.
 *
 *          To achieve this:
 *          a. Ensure SIGINT is unblocked.
 *          b. Block SIGINT, saving the original signal mask in context->signal_mask_during_ppoll
 *          c. Don't set the SA_RESTART flag when installing the signal handler.
 *          d. Use context->signal_mask_during_ppoll to unblock SIGINT during ppoll(), so SIGINT will cause ppoll() to return
 *             with EINTR to allow the shutdown request to be seen.
 * @param[in/out] context The manager context
 */
static void install_shutdown_signal_handler (vfio_manager_context_t *const context)
{
    sigset_t sigint_set;
    struct sigaction action;
    int rc;

    rc = sigemptyset (&sigint_set);
    if (rc != 0)
    {
        printf ("sigemptyset failed\n");
        exit (EXIT_FAILURE);
    }
    rc = sigaddset (&sigint_set, SIGINT);
    if (rc != 0)
    {
        printf ("sigaddset failed\n");
        exit (EXIT_FAILURE);
    }

    rc = sigprocmask (SIG_UNBLOCK, &sigint_set, NULL);
    if (rc != 0)
    {
        printf ("sigprocmask failed\n");
        exit (EXIT_FAILURE);
    }

    rc = sigprocmask (SIG_BLOCK, &sigint_set, &context->signal_mask_during_ppoll);
    if (rc != 0)
    {
        printf ("sigprocmask failed\n");
        exit (EXIT_FAILURE);
    }

    memset (&action, 0, sizeof (action));
    action.sa_handler = shutdown_request_handler;
    action.sa_flags = 0;
    rc = sigaction (SIGINT, &action, NULL);
    if (rc != 0)
    {
        printf ("sigaction failed\n");
        exit (EXIT_FAILURE);
    }
}


int main (int argc, char *argv[])
{
    vfio_manager_context_t context;
    bool success;
    int rc;

    parse_command_line_arguments (argc, argv);
    success = initialise_vfio_manager (&context);
    if (success)
    {
        if (arg_run_as_daemon)
        {
            /* When requested by a command line argument, now that the manager has successfully initialised
             * run the manager as a daemon.
             *
             * The daemon respects the once command line argument or SIGINT to request a shutdown.
             * There is no SIGHUP handler as no configuration file to re-load. */
            const int nochdir = 0;
            const int noclose = 1;
            rc = daemon (nochdir, noclose);
            if (rc != 0)
            {
                if (rc != 0)
                {
                    printf ("daemon() failed\n");
                    exit (EXIT_FAILURE);
                }
            }
        }

        /* Install signal handler, used to request the manager is shutdown */
        install_shutdown_signal_handler (&context);
        run_vfio_manager (&context);
    }

    finalise_vfio_manager (&context);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
