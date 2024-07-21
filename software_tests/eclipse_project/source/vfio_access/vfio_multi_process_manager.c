/*
 * @file vfio_multi_process_manager.c
 * @date 20 Jul 2024
 * @author Chester Gillon
 * @brief Manager process to support multiple process use of devices accessed using VFIO
 * @details
 *
 */

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


/* The maximum number of client, assuming 4 bi-directional channels on each device.
 * This simplifies the code by having a compile time maximum number of clients. */
#define VFIO_MAX_CLIENTS (MAX_VFIO_DEVICES * 8)


#define VFIO_MAX_FDS (1 /* listening_socket_fd */ + VFIO_MAX_CLIENTS)


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"isolate_iommu_groups", no_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};


/* Defines the content for the manager process */
typedef struct
{
    /* File descriptor used a listening socket to accept client connections */
    int listening_socket_fd;
    /* Socket file descriptors for the connected clients. Unused entries are set to -1.
     * This index into this array is used to identify the clients in other arrays. */
    int client_socket_fds[VFIO_MAX_CLIENTS];
    /* One more than the highest index in client_socket_fds[] for a connected client. Used to limit how many
     * clients have to be iterated over. */
    uint32_t maximum_used_clients;
    /* Contains the open IOMMU groups, IOMMU containers and VFIO devices */
    vfio_devices_t vfio_devices;
    /* For each client indicates which devices are in use. This is used to:
     * a. Open a device when the first client requests access.
     * b. Close a device once no clients require access. */
    bool devices_used_per_client[VFIO_MAX_CLIENTS][MAX_VFIO_DEVICES];
} vfio_manager_context_t;


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("--isolate_iommu_groups\n");
    printf ("  Causes each IOMMU group to use it's own container\n");

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
 * @brief Update the maximum_used_clients
 * @param[in/out] context The context to update
 */
static void update_maximum_used_clients (vfio_manager_context_t *const context)
{
    context->maximum_used_clients = 0;
    for (uint32_t client_index = 0; client_index < VFIO_MAX_CLIENTS; client_index++)
    {
        if (context->client_socket_fds[client_index] != -1)
        {
            context->maximum_used_clients = client_index + 1;
        }
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
        context->client_socket_fds[client_index] = -1;
        for (uint32_t device_index = 0; device_index < MAX_VFIO_DEVICES; device_index++)
        {
            context->devices_used_per_client[client_index][device_index] = false;
        }
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
                        if (strcmp (iommu_group_text, vfio_get_iommu_group (dev)) == 0)
                        {
                            num_expected_vfio_devices++;
                            if (context->vfio_devices.num_devices < MAX_VFIO_DEVICES)
                            {
                                /* The device is initialised to not being DMA capable, may be changed when a client requests
                                 * the device is opened. */
                                open_vfio_device (&context->vfio_devices, dev, VFIO_DEVICE_DMA_CAPABILITY_NONE);
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
                if (context->client_socket_fds[client_index] == -1)
                {
                    context->client_socket_fds[client_index] = new_client_fd;
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
 * @brief Close the connection to a client
 * @param[in/out] context The manager context to close the client connection for
 * @param[in] client_index Identifies the client to close the connection for
 */
static void close_client_connection (vfio_manager_context_t *const context, const uint32_t client_index)
{
    int rc;

    /*@todo check for resources to free */

    /* Close the socket for the client */
    rc = close (context->client_socket_fds[client_index]);
    if (rc != 0)
    {
        printf ("close() failed\n");
    }
    context->client_socket_fds[client_index] = -1;
    update_maximum_used_clients (context);
}


/**
 * @brief Process a request from a connected client to open a VFIO device, sending a reply
 * @details This opens the VFIO device on the first client which requests it, and has the effect of VFIO reseting the device
 * @param[in/out] context The manager context to update with the request
 * @param[in] client_index Identifies which client sent the request, to update the list of in use devices
 * @param[in] request The request received from the client, which contains the VFIO devide to open
 */
static void process_open_device_request (vfio_manager_context_t *const context, const uint32_t client_index,
                                         const vfio_open_device_request_t *const request)
{
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
    for (uint32_t device_index = 0;
         !tx_buffer.open_device_reply.success && (device_index < context->vfio_devices.num_devices);
         device_index++)
    {
        vfio_device_t *const candidate_device = &context->vfio_devices.devices[device_index];

        if ((candidate_device->pci_dev->domain == request->pci_domain) &&
            (candidate_device->pci_dev->bus    == request->pci_bus   ) &&
            (candidate_device->pci_dev->dev    == request->pci_dev   ) &&
            (candidate_device->pci_dev->func   == request->pci_func  )   )
        {
            /* The VFIO device is known to the manager */
            if (candidate_device->device_fd < 0)
            {
                /* The VFIO device is not already open. Need to open it, using the dma_capability requested by the client */
                candidate_device->dma_capability = request->dma_capability;
                tx_buffer.open_device_reply.success = open_vfio_device_fd (candidate_device);
            }
            else
            {
                /* The VFIO device is already open. Different clients may not need DMA capability for the same device,
                 * so update the DMA capability and enable bus master as required.
                 * A32 only DMA capability takes precedence. */
                if (request->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_A32)
                {
                    candidate_device->dma_capability = VFIO_DEVICE_DMA_CAPABILITY_A32;
                }
                else if (candidate_device->dma_capability == VFIO_DEVICE_DMA_CAPABILITY_NONE)
                {
                    candidate_device->dma_capability = request->dma_capability;
                }
                enable_bus_master_for_dma (candidate_device);
                tx_buffer.open_device_reply.success = true;
            }

            if (tx_buffer.open_device_reply.success)
            {
                /* On success complete the reply and indicate the client is using the device */
                tx_buffer.open_device_reply.iommu_type = candidate_device->group->container->iommu_type;
                tx_buffer.open_device_reply.num_iommu_groups = candidate_device->group->container->num_iommu_groups;
                for (uint32_t group_index = 0; group_index < candidate_device->group->container->num_iommu_groups; group_index++)
                {
                    snprintf (tx_buffer.open_device_reply.iommu_group_names[group_index],
                            sizeof (tx_buffer.open_device_reply.iommu_group_names[group_index]),
                            "%s", candidate_device->group->container->iommu_groups[group_index].iommu_group_name);
                }
                vfio_fds.device_fd = candidate_device->device_fd;
                vfio_fds.container_fd = request->container_fd_required ? candidate_device->group->container->container_fd : -1;
                context->devices_used_per_client[client_index][device_index] = true;
            }
        }
    }

    if (tx_buffer.open_device_reply.success)
    {
        /* Send successful reply, which includes the file descriptors as ancillary information */
        vfio_send_manage_message (context->client_socket_fds[client_index], &tx_buffer, &vfio_fds);
    }
    else
    {
        /* Send failure reply, without ancillary information */
        vfio_send_manage_message (context->client_socket_fds[client_index], &tx_buffer, NULL);
    }
}


/**
 * @brief The run loop for the VFIO manager
 * @param[in/out] context The initialised context to manage.
 *                        On entry the IOMMU groups have been created and the listening socket created
 */
static void run_vfio_manager (vfio_manager_context_t *const context)
{
    bool running = true;
    struct pollfd poll_fds[VFIO_MAX_FDS] = {0};
    uint32_t num_fds;
    uint32_t client_index;
    int num_ready_fds;
    int saved_errno;
    vfio_manage_messages_t rx_buffer;
    bool valid_message;
    bool close_connection;

    while (running)
    {
        /* Create the list of fds to poll as the listening socket and all currently connected clients.
         * client_socket_fds[] might be sparse, but entries for unconnected clients have a value of -1 and poll()
         * ignores any fds with a negative value. */
        num_fds = 0;
        poll_fds[num_fds].fd = context->listening_socket_fd;
        poll_fds[num_fds].events = POLLIN;
        num_fds++;
        for (client_index = 0; client_index < context->maximum_used_clients; client_index++)
        {
            poll_fds[num_fds].fd = context->client_socket_fds[client_index];
            poll_fds[num_fds].events = POLLIN;
            num_fds++;
        }

        /* Wait for a socket to be readable with no timeout */
        errno = 0;
        num_ready_fds = poll (poll_fds, num_fds, -1);
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
    }
}


int main (int argc, char *argv[])
{
    vfio_manager_context_t context;
    bool success;

    parse_command_line_arguments (argc, argv);
    success = initialise_vfio_manager (&context);
    if (success)
    {
        run_vfio_manager (&context);
    }

    finalise_vfio_manager (&context);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
