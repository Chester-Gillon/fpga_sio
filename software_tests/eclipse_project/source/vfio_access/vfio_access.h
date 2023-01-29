/*
 * @file vfio_access.h
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Provides an API to allow access to device using VFIO
 */

#ifndef SOURCE_VFIO_ACCESS_VFIO_ACCESS_H_
#define SOURCE_VFIO_ACCESS_VFIO_ACCESS_H_

#include <stdint.h>
#include <limits.h>
#include <pci/pci.h>
#include <linux/pci.h> /* For PCI_STD_NUM_BARS */
#include <linux/vfio.h>


/* Defines one device which has been opened using vfio and has all its memory BARs mapped */
typedef struct
{
    /* The IOMMU group for the device, read when scanning the PCI bus */
    char *iommu_group;
    /* The pathname for the vfio group character file */
    char group_pathname[PATH_MAX];
    /* The PCI device name as <domain>:<bus>:<device>.<function> */
    char device_name[64];
    /* The IOMMU group descriptor */
    int group_fd;
    /* The status of the IOMMU group, used to check that is viable */
    struct vfio_group_status group_status;
    /* The vfio device descriptor */
    int device_fd;
    /* The vfio device information */
    struct vfio_device_info device_info;
    /* The vfio information about each PCI BAR. */
    struct vfio_region_info regions_info[PCI_STD_NUM_BARS];
    /* For each BAR, if can be memory mapped then points at the mapping for the BAR.
     * Size of the mapping is given by the corresponding bars_info[].size.
     * NULL if the BAR is not present or doesn't support being mapped.
     *
     * As of AlmaLinux 8.7 with a 4.18.0-425.3.1.el8.x86_64 Kernel some limitations are:
     * 1. With prefetchable BARs a "uncached-minus" PAT mapping is always used, can't see any way to request
     *    a "write-combining" PAT mapping to be used.
     *
     *    https://patchwork.kernel.org/project/kvm/patch/20171009025000.39435-1-aik@ozlabs.ru/ was a patch to allow vfio to
     *    use write-combining mappings for pre-fetchable BARs, but not sure what happened to the patch.
     * 2. gdb is unable to view the contents of the mapped memory, reporting errors of the form:
     *      Error message from debugger back end:
     *      Cannot access memory at address 0x7ffff7ee1000.
     *
     *    In /usr/src/debug/kernel-4.18.0-425.3.1.el8/linux-4.18.0-425.3.1.el8.x86_64/drivers/vfio/pci/vfio_pci.c the only
     *    operations are the following. I.e. doesn't set access operation which ptrace (and thus gdb) uses to access the mapping:
     *       static const struct vm_operations_struct vfio_pci_mmap_ops = {
     *           .open = vfio_pci_mmap_open,
     *           .close = vfio_pci_mmap_close,
     *           .fault = vfio_pci_mmap_fault,
     *       };
     * 3. Based upon adding debugpat to the command line, when checking the PAT mapping used, think the mapping isn't populated
     *    in the application until the first access, which triggers a page fault. Haven't yet confirmed this by tracing
     *    page faults. */
    uint8_t *mapped_bars[PCI_STD_NUM_BARS];
} vfio_device_t;


/* Contains all devices which have opened using vfio */
#define MAX_VFIO_DEVICES 4
typedef struct
{
    /* If non-NULL used to search for PCI devices */
    struct pci_access *pacc;
    /* The VFIO container used by all devices.
     * DMA mapping is done for the container, so having one container for multiple devices should all the DMA mappings
     * to be used by multiple devices.
     *
     * The description of VFIO_GROUP_SET_CONTAINER contains:
     *    "Containers may, at their discretion, support multiple groups."
     *
     * With the intel_iommu was able to add two devices in different /sys/class/iommu/dmar?/devices directory
     * to the same container. */
    int container_fd;
    /* Used to allocate the next IOVA address allocated */
    uint64_t next_iova;
    /* The number of devices which have been opened */
    uint32_t num_devices;
    /* The devices which have been opened */
    vfio_device_t devices[MAX_VFIO_DEVICES];
} vfio_devices_t;


/*
 * Defines a filter which can match PCI devices by identity to open for VFIO access.
 * VFIO_PCI_DEVICE_FILTER_ANY can be used for any field to ignore the value.
 */
#define VFIO_PCI_DEVICE_FILTER_ANY -1
typedef struct
{
    int vendor_id;
    int device_id;
    int subsystem_vendor_id;
    int subsystem_device_id;
} vfio_pci_device_filter_t;


/* Defines one mapping which has been allocated for DMA using the IOMMU */
typedef struct
{
    /* The virtual address of the allocated region, for using by the process */
    void *vaddr;
    /* IO virtual address, for accessing by the device DMA */
    uint64_t iova;
    /* Size of the mapping in bytes */
    size_t size;
} vfio_dma_mapping_t;


void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev);
void open_vfio_devices_matching_filter (vfio_devices_t *const vfio_devices,
                                        const size_t num_filters, const vfio_pci_device_filter_t filters[const num_filters]);
void close_vfio_devices (vfio_devices_t *const vfio_devices);
void allocate_vfio_dma_mapping (vfio_devices_t *const vfio_devices,
                                vfio_dma_mapping_t *const mapping,
                                const size_t size, const uint32_t permission);
void free_vfio_dma_mapping (const vfio_devices_t *const vfio_devices, vfio_dma_mapping_t *const mapping);


/**
 * @brief Perform a read from a 32-bit register in a memory mapped BAR
 * @param[in] mapped_bar The base of the BAR to read
 * @parem[in] reg_offset The byte offset into the BAR of the register to read
 * @return The register value
 */
static inline uint32_t read_reg32 (const uint8_t *const mapped_bar, const uint64_t reg_offset)
{
    const uint32_t *const mapped_reg = (const uint32_t *) &mapped_bar[reg_offset];
    return __atomic_load_n (mapped_reg, __ATOMIC_ACQUIRE);
}


#endif /* SOURCE_VFIO_ACCESS_VFIO_ACCESS_H_ */
