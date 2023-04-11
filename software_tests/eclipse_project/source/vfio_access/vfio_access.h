/*
 * @file vfio_access.h
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Provides an API to allow access to device using VFIO
 */

#ifndef SOURCE_VFIO_ACCESS_VFIO_ACCESS_H_
#define SOURCE_VFIO_ACCESS_VFIO_ACCESS_H_

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <pci/pci.h>
#include <linux/pci.h> /* For PCI_STD_NUM_BARS */
#include <linux/vfio.h>

#ifdef HAVE_CMEM
#include <cmem_drv.h>
#endif


/* Defines the option used to allocate a buffer used for VFIO DMA */
typedef enum
{
    /* Allocates the buffer from the heap of the calling process, when using an IOMMU */
    VFIO_BUFFER_ALLOCATION_HEAP,
    /* Allocate the buffer from POSIX shared memory, when using an IOMMU */
    VFIO_BUFFER_ALLOCATION_SHARED_MEMORY,
    /* Allocate the buffer using huge pages (of the default huge page size), when using an IOMMU */
    VFIO_BUFFER_ALLOCATION_HUGE_PAGES,
    /* Allocate the buffer using a physical contiguous memory allocator, when using NOIOMMU */
    VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY
} vfio_buffer_allocation_type_t;


/* Defines one buffer allocated for VFIO DMA */
typedef struct
{
    /* How the memory for the buffer is allocated */
    vfio_buffer_allocation_type_t allocation_type;
    /* The size of the buffer in bytes */
    size_t size;
    /* The allocated buffer, as the virtual address mapped into the process */
    void *vaddr;
    /* For VFIO_BUFFER_ALLOCATION_SHARED_MEMORY the name of the POSIX shared memory file */
    char pathname[PATH_MAX];
    /* For VFIO_BUFFER_ALLOCATION_SHARED_MEMORY the file descriptor of the POSIX shared memory file */
    int fd;
#ifdef HAVE_CMEM
    /* For VFIO_BUFFER_ALLOCATION_PHYSICAL_MEMORY the buffer allocated in physically contiguous memory */
    cmem_host_buf_desc_t cmem_host_buf_desc;
#endif
} vfio_buffer_t;


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


/* Used to track usage of the cmem driver */
typedef enum
{
    /* Have not attempted to use the cmem driver. Either an IOMMU is available or the program hasn't used DMA */
    VFIO_CMEM_USAGE_NONE,
    /* The cmem driver has been successfully opened, following an attempt to use DMA in NOIOMMU mode */
    VFIO_CMEM_USAGE_DRIVER_OPEN,
    /* an attempt was made to use DMA in NOIOMMU mode, but support for the cmem driver hasn't been compiled in */
    VFIO_CMEM_USAGE_SUPPORT_NOT_COMPILED,
    /* An attempt was made to use DMA in NOIOMMU mode, but the cmem driver open failed (probably no module loaded) */
    VFIO_CMEM_USAGE_OPEN_FAILED
} vfio_cmem_usage_t;


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
    /* The IOMMU type which is used for the VFIO container */
    __s32 iommu_type;
    /* Used to allocate the next IOVA address allocated, when iommu_type is other than VFIO_NOIOMMU_IOMMU */
    uint64_t next_iova;
    /* Used to track usage of the cmem driver */
    vfio_cmem_usage_t cmem_usage;
    /* The number of devices which have been opened */
    uint32_t num_devices;
    /* The devices which have been opened */
    vfio_device_t devices[MAX_VFIO_DEVICES];
} vfio_devices_t;


/*
 * Defines a filter which can match PCI devices by identity to open for VFIO access.
 * VFIO_PCI_DEVICE_FILTER_ANY can be used for any field to ignore the value.
 * enable_bus_master is set to enable the PCI device as a bus master (for DMA).
 */
#define VFIO_PCI_DEVICE_FILTER_ANY -1
typedef struct
{
    int vendor_id;
    int device_id;
    int subsystem_vendor_id;
    int subsystem_device_id;
    bool enable_bus_master;
} vfio_pci_device_filter_t;


/* Defines one mapping which has been allocated for DMA using the IOMMU */
typedef struct
{
    /* The allocated buffer in the process used by the mapping */
    vfio_buffer_t buffer;
    /* IO virtual address, for accessing by the device DMA */
    uint64_t iova;
    /* Allows the mapping to have its contents allocated for different uses */
    size_t num_allocated_bytes;
} vfio_dma_mapping_t;


/* Defines one secondary process which is launched to access VFIO device(s) opened by the primary process */
#define VFIO_SECONDARY_MAX_ARGC 16
typedef struct
{
    /* Path to the executable to launch */
    char executable[PATH_MAX];
    /* null terminated list of arguments for the process */
    char *argv[VFIO_SECONDARY_MAX_ARGC + 1];
    /* The identity of the launched process */
    pid_t pid;
    /* Set true when the secondary process has been reaped by the primary process */
    bool reaped;
} vfio_secondary_process_t;


void create_vfio_buffer (vfio_buffer_t *const buffer,
                         const size_t size, const vfio_buffer_allocation_type_t buffer_allocation,
                         const char *const name_suffix);
void free_vfio_buffer (vfio_buffer_t *const buffer);
void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev, const bool enable_bus_master);
void map_vfio_device_bar_before_use (vfio_device_t *const vfio_device, const int bar_index);
void reset_vfio_device (vfio_device_t *const vfio_device);
void open_vfio_devices_matching_filter (vfio_devices_t *const vfio_devices,
                                        const size_t num_filters, const vfio_pci_device_filter_t filters[const num_filters]);
void close_vfio_devices (vfio_devices_t *const vfio_devices);
void allocate_vfio_dma_mapping (vfio_devices_t *const vfio_devices,
                                vfio_dma_mapping_t *const mapping,
                                const size_t size, const uint32_t permission,
                                const vfio_buffer_allocation_type_t buffer_allocation);
void *vfio_dma_mapping_allocate_space (vfio_dma_mapping_t *const mapping,
                                       const size_t allocation_size, uint64_t *const allocated_iova);
void vfio_dma_mapping_align_space (vfio_dma_mapping_t *const mapping);
void free_vfio_dma_mapping (const vfio_devices_t *const vfio_devices, vfio_dma_mapping_t *const mapping);
uint16_t vfio_read_pci_config_word (const vfio_device_t *const vfio_device, const uint32_t offset);
uint32_t vfio_read_pci_config_long (const vfio_device_t *const vfio_device, const uint32_t offset);
void vfio_write_pci_config_word (const vfio_device_t *const vfio_device, const uint32_t offset, const uint16_t config_word);
void vfio_write_pci_config_long (const vfio_device_t *const vfio_device, const uint32_t offset, const uint32_t config_long);
void vfio_display_pci_command (const vfio_device_t *const vfio_device);
void vfio_display_fds (const vfio_devices_t *const vfio_devices);
void vfio_launch_secondary_processes (vfio_devices_t *const vfio_devices,
                                      const uint32_t num_processes, vfio_secondary_process_t processes[const num_processes]);
void vfio_await_secondary_processes (const uint32_t num_processes, vfio_secondary_process_t processes[const num_processes]);


/* Intel processor cache line size */
#define VFIO_CACHE_LINE_SIZE 64


/**
 * @brief Round up a size to be a multiple of cache line size
 * @param[in] requested_size The size to align
 * @return The aligned size
 */
static inline size_t vfio_align_cache_line_size (const size_t requested_size)
{
    return ((requested_size + (VFIO_CACHE_LINE_SIZE - 1)) / VFIO_CACHE_LINE_SIZE) * VFIO_CACHE_LINE_SIZE;
}


/**
 * @brief Perform a read from a 8-bit register in a memory mapped BAR
 * @param[in] mapped_bar The base of the BAR to read
 * @parem[in] reg_offset The byte offset into the BAR of the register to read
 * @return The register value
 */
static inline uint8_t read_reg8 (const uint8_t *const mapped_bar, const uint64_t reg_offset)
{
    const uint8_t *const mapped_reg = (const uint8_t *) &mapped_bar[reg_offset];
    return __atomic_load_n (mapped_reg, __ATOMIC_ACQUIRE);
}


/**
 * @brief Perform a read from a 16-bit register in a memory mapped BAR
 * @param[in] mapped_bar The base of the BAR to read
 * @parem[in] reg_offset The byte offset into the BAR of the register to read
 * @return The register value
 */
static inline uint16_t read_reg16 (const uint8_t *const mapped_bar, const uint64_t reg_offset)
{
    const uint16_t *const mapped_reg = (const uint16_t *) &mapped_bar[reg_offset];
    return __atomic_load_n (mapped_reg, __ATOMIC_ACQUIRE);
}


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


/**
 * @brief Perform a read from a 64-bit register in a memory mapped BAR. formed of two 32-bit lower and upper registers.
 * @details This was created for the Xilinx "DMA/Bridge Subsystem for PCI Express" PG195 configuration registers.
 *          An attempt to perform a single 64-bit read caused all-ones to be returned.
 * @param[in] mapped_bar The base of the BAR to read
 * @parem[in] reg_offset The byte offset into the BAR of the register to read
 * @return The register value
 */
static inline uint64_t read_split_reg64 (const uint8_t *const mapped_bar, const uint64_t reg_offset)
{
    const uint32_t lower = read_reg32 (mapped_bar, reg_offset);
    const uint32_t upper = read_reg32 (mapped_bar, reg_offset + sizeof (uint32_t));

    return (((uint64_t) upper) << 32) + lower;
}


/**
 * @brief Perform a write to a 8-bit register in a memory mapped BAR
 * @param[in/out] mapped_bar The base of the BAR to write
 * @param[in] reg_offset The byte offset into the BAR of the register to write
 * @param[in] reg_value The register value to write
 */
static inline void write_reg8 (uint8_t *const mapped_bar, const uint64_t reg_offset, const uint8_t reg_value)
{
    uint8_t *const mapped_reg = (uint8_t *) &mapped_bar[reg_offset];
    __atomic_store_n (mapped_reg, reg_value, __ATOMIC_RELEASE);
}


/**
 * @brief Perform a write to a 16-bit register in a memory mapped BAR
 * @param[in/out] mapped_bar The base of the BAR to write
 * @param[in] reg_offset The byte offset into the BAR of the register to write
 * @param[in] reg_value The register value to write
 */
static inline void write_reg16 (uint8_t *const mapped_bar, const uint64_t reg_offset, const uint16_t reg_value)
{
    uint16_t *const mapped_reg = (uint16_t *) &mapped_bar[reg_offset];
    __atomic_store_n (mapped_reg, reg_value, __ATOMIC_RELEASE);
}


/**
 * @brief Perform a write to a 32-bit register in a memory mapped BAR
 * @param[in/out] mapped_bar The base of the BAR to write
 * @param[in] reg_offset The byte offset into the BAR of the register to write
 * @param[in] reg_value The register value to write
 */
static inline void write_reg32 (uint8_t *const mapped_bar, const uint64_t reg_offset, const uint32_t reg_value)
{
    uint32_t *const mapped_reg = (uint32_t *) &mapped_bar[reg_offset];
    __atomic_store_n (mapped_reg, reg_value, __ATOMIC_RELEASE);
}


/**
 * @brief Perform a write to a 64-bit register in a memory mapped BAR, formed of two 32-bit lower and upper registers.
 * @details This was created for the Xilinx "DMA/Bridge Subsystem for PCI Express" PG195 configuration registers.
 *          An attempt to perform a single 64-bit write caused the upper value to not change.
 * @param[in/out] mapped_bar The base of the BAR to write
 * @param[in] reg_offset The byte offset into the BAR of the register to write
 * @param[in] reg_value The register value to write
 */
static inline void write_split_reg64 (uint8_t *const mapped_bar, const uint64_t reg_offset, const uint64_t reg_value)
{
    uint32_t *const mapped_reg_lower = (uint32_t *) &mapped_bar[reg_offset];
    uint32_t *const mapped_reg_upper = (uint32_t *) &mapped_bar[reg_offset + sizeof (uint32_t)];
    const uint32_t reg_value_lower = reg_value & 0xffffffff;
    const uint32_t reg_value_upper = (uint32_t) (reg_value >> 32ULL);

    __atomic_store_n (mapped_reg_lower, reg_value_lower, __ATOMIC_RELEASE);
    __atomic_store_n (mapped_reg_upper, reg_value_upper, __ATOMIC_RELEASE);
}

#endif /* SOURCE_VFIO_ACCESS_VFIO_ACCESS_H_ */
