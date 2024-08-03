/*
 * @file vfio_access_private.h
 * @date 20 Jul 2024
 * @author Chester Gillon
 * @brief Contains private definitions for the VFIO access library which are also for use by the manager process
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
 * Is of type SOCK_SEQPACKET since:
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
    VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REPLY,
    /* A request from a client to close a VFIO device */
    VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST,
    /* The response from the manager for a VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST */
    VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REPLY,
    /* A request from a client to allocate an IOVA region */
    VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST,
    /* The response from the manager for a VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST */
    VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REPLY,
    /* A request from a client to free an IOVA region */
    VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST,
    /* The response from the manager for a VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST */
    VFIO_MANAGE_MSG_ID_FREE_IOVA_REPLY,
    /* Message ID only from client to request exclusive access */
    VFIO_MANAGE_MSG_ID_EXCLUSIVE_ACCESS_REQUEST,
    /* Message ID only from manager to client that exclusive access is allowed */
    VFIO_MANAGE_MSG_ID_EXCLUSIVE_ACCESS_ALLOWED,
    /* Message ID only sent from client to indicate the exclusive access has been completed */
    VFIO_MANAGE_MSG_ID_EXCLUSIVE_ACCESS_COMPLETED
} vfio_manager_msg_id_t;


/* Used by a client to identify a VFIO device in a request, as the PCI location */
typedef struct
{
    int domain;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
} vfio_device_identity_t;


/* The message body for a VFIO_MANAGE_MSG_ID_OPEN_DEVICE_REQUEST */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Identifies the VFIO device which the client is requesting to be opened */
    vfio_device_identity_t device_id;
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
    /* The identity of the container, which is used by the client to allocate / free IOVA regions */
    uint32_t container_id;
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


/* The message body for a VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REQUEST */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Identifies the VFIO device which the client is requesting to be closed */
    vfio_device_identity_t device_id;
} vfio_close_device_request_t;


/* The message body for a VFIO_MANAGE_MSG_ID_CLOSE_DEVICE_REPLY */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* If true the device close succeeded, otherwise failed */
    bool success;
} vfio_close_device_reply_t;


/* The message body for a VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REQUEST */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Indicates if the allocation is for a 64-bit IOVA capable device */
    vfio_device_dma_capability_t dma_capability;
    /* Identifies which container to use for the IOVA allocation */
    uint32_t container_id;
    /* The requested IOVA size in bytes */
    size_t requested_size;
} vfio_allocate_iova_request_t;


/* The message body for a VFIO_MANAGE_MSG_ID_ALLOCATE_IOVA_REPLY.
 * The size of the allocation, compared to the requested size, has been rounded up to be a multiple of the IOVA page size. */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* If true the IOVA allocation close succeeded, otherwise failed */
    bool success;
    /* The start IOVA of the allocated region */
    uint64_t start;
    /* The inclusive end IOVA of the allocated region */
    uint64_t end;
} vfio_allocate_iova_reply_t;


/* The message body for a VFIO_MANAGE_MSG_ID_FREE_IOVA_REQUEST */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Identifies which container are freeing the IOVA allocation for */
    uint32_t container_id;
    /* The start IOVA of the region to free */
    uint64_t start;
    /* The inclusive end IOVA of the region to free */
    uint64_t end;
} vfio_free_iova_request_t;


/* The message body for a VFIO_MANAGE_MSG_ID_FREE_IOVA_REPLY */
typedef struct
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* If true the freeing of the IOVA region succeeded, otherwise failed */
    bool success;
} vfio_free_iova_reply_t;


/* Used to allocate a buffer to receive different messages */
typedef union
{
    /* Common placement of message identification */
    vfio_manager_msg_id_t msg_id;
    /* Message type specific structures */
    vfio_open_device_request_t   open_device_request;
    vfio_open_device_reply_t     open_device_reply;
    vfio_close_device_request_t  close_device_request;
    vfio_close_device_reply_t    close_device_reply;
    vfio_allocate_iova_request_t allocate_iova_request;
    vfio_allocate_iova_reply_t   allocate_iova_reply;
    vfio_free_iova_request_t     free_iova_request;
    vfio_free_iova_reply_t       free_iova_reply;
} vfio_manage_messages_t;


char *vfio_get_iommu_group (struct pci_dev *const pci_dev);
void enable_bus_master_for_dma (vfio_device_t *const device);
bool open_vfio_device_fd (vfio_device_t *const new_device);
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev,
                       const vfio_device_dma_capability_t dma_capability);
void update_iova_regions (vfio_iommu_container_t *const container, const vfio_iova_region_t *const new_region);
bool vfio_ensure_iommu_container_set_for_group (vfio_iommu_group_t *const group);
void allocate_iova_region_direct (vfio_iommu_container_t *const container,
                                  const vfio_device_dma_capability_t dma_capability,
                                  const size_t requested_size,
                                  const uint32_t allocating_client_id,
                                  vfio_iova_region_t *const region);
bool vfio_receive_manage_message (const int socket_fd, vfio_manage_messages_t *const rx_buffer,
                                  vfio_open_device_reply_fds_t *const vfio_fds);
void vfio_send_manage_message (const int socket_fd, vfio_manage_messages_t *const tx_buffer,
                               vfio_open_device_reply_fds_t *const vfio_fds);

#endif /* SOURCE_VFIO_ACCESS_VFIO_ACCESS_PRIVATE_H_ */
