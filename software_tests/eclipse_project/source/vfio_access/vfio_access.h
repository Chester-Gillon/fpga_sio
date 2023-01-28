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
     * NULL if the BAR is not present or doesn't support being mapped. */
    uint8_t *mapped_bars[PCI_STD_NUM_BARS];
} vfio_device_t;


/* Contains all devices which have opened using vfio */
#define MAX_VFIO_DEVICES 4
typedef struct
{
    /* The VFIO container used by all devices.
     * Not clear what the benefits are of having one container for multiple devices, .vs. one container per device.
     *
     * The description of VFIO_GROUP_SET_CONTAINER contains:
     *    "Containers may, at their discretion, support multiple groups."
     *
     * With the intel_iommu was able to add two devices in different /sys/class/iommu/dmar?/devices directory
     * to the same container. */
    int container_fd;
    /* The number of devices which have been opened */
    uint32_t num_devices;
    /* The devices which have been opened */
    vfio_device_t devices[MAX_VFIO_DEVICES];
} vfio_devices_t;


void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev);
void close_vfio_devices (vfio_devices_t *const vfio_devices);


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
