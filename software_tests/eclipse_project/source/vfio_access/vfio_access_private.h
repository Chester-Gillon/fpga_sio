/*
 * @file vfio_access_private.h
 * @date 20 Jul 2024
 * @author Chester Gillon
 * @brief Contains private definitions for the VFIO access library which are for use by the manager process
 */

#ifndef SOURCE_VFIO_ACCESS_VFIO_ACCESS_PRIVATE_H_
#define SOURCE_VFIO_ACCESS_VFIO_ACCESS_PRIVATE_H_

#include "vfio_access.h"


/*
 * Paths for the VFIO character devices
 */
#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


/* Name of the abstract Unix domain socket used to communicate with the VFIO multi process manager.
 *
 * Uses the abstract namespace so that automatically disappears when all open references are closed.
 *
 * Is of type SOCK_SEQPACKET:
 * a. Preserving message boundaries makes the code simpler.
 * b. Connection oriented means the manager can detect when a client exits uncleanly, and free up resources.
 */
#define VFIO_MULTI_PROCESS_MANAGER_ABSTRACT_NAMESPACE "\0VFIO_MULTI_PROCESS_MANAGER"


/* The different types of messages which can be exchanged between the VFIO multi process manager and the connected clients */
typedef enum
{
    /* A request from a client to open a VFIO device */
    VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST,
    /* The response from the manager for a VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST */
    VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY
} vfio_manager_msg_id_t;


/* The message body for a VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Identifies the VFIO device which the client is requesting to be opened, as the PCI location */
    int pci_domain;
    uint8_t pci_bus;
    uint8_t pci_dev;
    uint8_t pci_func;
    /* The DMA capability the client requires */
    vfio_device_dma_capability_t dma_capability;
    /* Set true if the container_fd needs to be sent in the reply */
    bool container_fd_required;
} vfio_open_device_request_t;


/* The message body for a VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* If true the device open succeeded, otherwise failed */
    bool success;
    /* The IOMMU type which is used for the VFIO container */
    __s32 iommu_type;
    /* The number of IOMMU groups the container is used on */
    uint32_t num_iommu_groups;
    /* The names of the IOMMU groups in the container. This is used by the client to determine which IOMMU groups are used
     * by which container. */
    char iommu_group_names[MAX_VFIO_DEVICES][32];
} vfio_open_device_reply_t;


/* Contents of the SCM_RIGHTS ancillary data sent with VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY to contain the file descriptors
 * which the client needs to use. The group_fd isn't needed by the client for VFIO_DEVICES_USAGE_INDIRECT_ACCESS. */
typedef struct
{
    /* The vfio device descriptor. Needed to map BARs, access configuration space or reset the device.
     * Always sent. */
    int device_fd;
    /* The file descriptor for the container. Needed for VFIO_IOMMU_MAP_DMA.
     * Only sent when container_fd_required was set in the request. */
    int container_fd;
} vfio_open_device_reply_fds_t;


/* Used to allocate a buffer to receive different messages */
typedef union
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Message type specific structures */
    vfio_open_device_request_t open_device_request;
    vfio_open_device_reply_t open_device_reply;
} vfio_manage_messages_t;


char *vfio_get_iommu_group (struct pci_dev *const pci_dev);
void enable_bus_master_for_dma (vfio_device_t *const device);
bool open_vfio_device_fd (vfio_device_t *const new_device);
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev,
                       const vfio_device_dma_capability_t dma_capability);
bool vfio_receive_manage_message (const int socket_fd, vfio_manage_messages_t *const rx_buffer,
                                  vfio_open_device_reply_fds_t *const vfio_fds);
void vfio_send_manage_message (const int socket_fd, vfio_manage_messages_t *const tx_buffer,
                               vfio_open_device_reply_fds_t *const vfio_fds);

#endif /* SOURCE_VFIO_ACCESS_VFIO_ACCESS_PRIVATE_H_ */
