/*
 * @file display_vfio_information.c
 * @date Created on: 27 Dec 2022
 * @author Chester Gillon
 * @brief Initial program for displaying information about VFIO
 * @detail Not aware of standard "helper" user space libraries for VFIO, so uses the raw IOCTLs.
 *         DPDK is an example user space application making use of VFIO IOCTLs.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>

#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include <pci/pci.h>

#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


static const char *const pci_region_names[VFIO_PCI_NUM_REGIONS] =
{
    [VFIO_PCI_BAR0_REGION_INDEX  ] = "BAR0",
    [VFIO_PCI_BAR1_REGION_INDEX  ] = "BAR1",
    [VFIO_PCI_BAR2_REGION_INDEX  ] = "BAR2",
    [VFIO_PCI_BAR3_REGION_INDEX  ] = "BAR3",
    [VFIO_PCI_BAR4_REGION_INDEX  ] = "BAR4",
    [VFIO_PCI_BAR5_REGION_INDEX  ] = "BAR5",
    [VFIO_PCI_ROM_REGION_INDEX   ] = "ROM",
    [VFIO_PCI_CONFIG_REGION_INDEX] = "CONFIG",
    [VFIO_PCI_VGA_REGION_INDEX   ] = "VGA"
};

static const char *const irq_block_names [VFIO_PCI_NUM_IRQS] =
{
    [VFIO_PCI_INTX_IRQ_INDEX] = "INTX",
    [VFIO_PCI_MSI_IRQ_INDEX ] = "MSI",
    [VFIO_PCI_MSIX_IRQ_INDEX] = "MSIX",
    [VFIO_PCI_ERR_IRQ_INDEX ] = "ERR",
    [VFIO_PCI_REQ_IRQ_INDEX ] = "REQ"
};


/**
 * @brief Display if a VFIO extension is supported or not, where 1 means supported
 */
#define DISPLAY_EXTENSION_SUPPORT(vfio_file_fd,extension) display_extension_support (vfio_file_fd, extension, #extension)
static void display_extension_support (const int vfio_file_fd, const __u32 extension, const char *const name)
{
    int saved_errno;
    int rc;

    errno = 0;
    rc = ioctl (vfio_file_fd, VFIO_CHECK_EXTENSION, extension);
    saved_errno = errno;
    printf ("Extension %s support %d", name, rc);
    if (saved_errno != 0)
    {
        printf (" errno %s)\n", strerror (saved_errno));
    }
    else
    {
        printf ("\n");
    }
}


/**
 * @brief Display the capabilities of a type1 IOMMU
 * @details The conditional compilation is to support compiling under Ubuntu 18.04.6 LTS which doesn't have the
 *          capability chain.
 */
static void display_type1_iommu_capabilities (const int container_fd)
{
    int rc;
    struct vfio_iommu_type1_info iommu_info_get_size;
    struct vfio_iommu_type1_info *iommu_info;
    uint64_t page_size;

    /* Determine the size required to get the capabilities for the IOMMU.
     * This updates the argsz to indicate how much space is required. */
    memset (&iommu_info_get_size, 0, sizeof iommu_info_get_size);
    iommu_info_get_size.argsz = sizeof (iommu_info_get_size);
    rc = ioctl (container_fd, VFIO_IOMMU_GET_INFO, &iommu_info_get_size);
    if (rc != 0)
    {
        printf ("  VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
        return;
    }

    /* Allocate a structure of the required size for the IOMMU information, and get it */
    iommu_info = calloc (iommu_info_get_size.argsz, 1);
    iommu_info->argsz = iommu_info_get_size.argsz;
    rc = ioctl (container_fd, VFIO_IOMMU_GET_INFO, iommu_info);
    if (rc != 0)
    {
        printf ("  VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
        return;
    }

    /* Report fixed information in the vfio_iommu_type1_info structure */
    printf ("  info supports: pagesizes=%d caps=%d\n",
            (iommu_info->flags & VFIO_IOMMU_INFO_PGSIZES) != 0,
            (iommu_info->flags & VFIO_IOMMU_INFO_CAPS) != 0);
    printf ("  IOVA supported page sizes:");
    for (page_size = 1; page_size != 0; page_size <<= 1)
    {
        if ((iommu_info->iova_pgsizes & page_size) == page_size)
        {
            printf (" 0x%" PRIx64, page_size);
        }
    }
    printf ("\n");

    if (((iommu_info->flags & VFIO_IOMMU_INFO_CAPS) != 0) && (iommu_info->cap_offset > 0))
    {
        /* Report IOMMU capabilities, by following the chain */
        const char *const info_start = (const char *) iommu_info;
        __u32 cap_offset = iommu_info->cap_offset;

        while ((cap_offset > 0) && (cap_offset < iommu_info->argsz))
        {
            const struct vfio_info_cap_header *const cap_header =
                    (const struct vfio_info_cap_header *) &info_start[cap_offset];

            switch (cap_header->id)
            {
            case VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE:
                {
                    const struct vfio_iommu_type1_info_cap_iova_range *const cap_iova_range =
                            (const struct vfio_iommu_type1_info_cap_iova_range *) cap_header;

                    printf ("  VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE version=%" PRIu16 "\n",
                            cap_iova_range->header.version);
                    for (uint32_t iova_index = 0; iova_index < cap_iova_range->nr_iovas; iova_index++)
                    {
                        const struct vfio_iova_range *const iova_range = &cap_iova_range->iova_ranges[iova_index];

                        printf ("    [%" PRIu32 "] start=0x%llx end=0x%llx\n", iova_index, iova_range->start, iova_range->end);
                    }
                }
                break;

            case VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION:
                {
                    const struct vfio_iommu_type1_info_cap_migration *const cap_migration =
                            (const struct vfio_iommu_type1_info_cap_migration *) cap_header;

                    printf ("  VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION version=%" PRIu16 " flags=0x%" PRIx32 " max_dirty_bitmap_size=0x%llx\n",
                            cap_migration->header.version, cap_migration->flags, cap_migration->max_dirty_bitmap_size);
                    printf ("    supported page sizes for dirty page logging:");
                    for (page_size = 1; page_size != 0; page_size <<= 1)
                    {
                        if ((cap_migration->pgsize_bitmap & page_size) == page_size)
                        {
                            printf (" 0x%" PRIx64, page_size);
                        }
                    }
                    printf ("\n");
                }
                break;

            case VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL:
                {
                    const struct vfio_iommu_type1_info_dma_avail *const dma_avail =
                            (const struct vfio_iommu_type1_info_dma_avail *) cap_header;

                    printf ("  VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL version=%" PRIu16 " avail=%" PRIu32 "\n",
                            dma_avail->header.version, dma_avail->avail);
                }
                break;

            default:
                printf ("  Unknown IOMMU type1 capability id=%" PRIu16 " version=%" PRIu16 "\n",
                        cap_header->id, cap_header->version);
                break;
            }

            cap_offset = cap_header->next;
        }
    }
}


/**
 * @brief Read a number of bytes from the PCI config space of a device, using vfio-pci
 * @details If an error occurs during the read displays diagnostic information and sets the returned bytes to 0xff.
 *          For simplicity looks up the PCI config region on the device for every call.
 * @param[in] device_fd Device to read from
 * @param[in] offset Offset into the configuration space to read
 * @param[in] num_bytes The number of bytes to read
 * @param[out] config_bytes The bytes which have been read.
 */
static void read_pci_config_bytes (const int device_fd, const uint32_t offset, const size_t num_bytes, void *const config_bytes)
{
    int rc;
    ssize_t num_read;
    struct vfio_region_info region_info =
    {
        .argsz = sizeof (region_info),
        .index = VFIO_PCI_CONFIG_REGION_INDEX
    };

    memset (config_bytes, 0xff, num_bytes);

    rc = ioctl (device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);
    if (rc != 0)
    {
        printf ("  VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
        return;
    }

    num_read = pread (device_fd, config_bytes, num_bytes, (off_t) (region_info.offset + offset));
    if (num_read != (ssize_t) num_bytes)
    {
        printf ("  PCI config read of %zu bytes from offset %" PRIu32 " only read %zd bytes : %s\n",
                num_bytes, offset, num_read, strerror (errno));
        return;
    }
}


/**
 * @brief Read a word from the PCI config space of a device, using vfio-pci
 */
static uint16_t read_pci_config_word (const int device_fd, const uint32_t offset)
{
    uint16_t config_word;

    read_pci_config_bytes (device_fd, offset, sizeof (config_word), &config_word);

    return config_word;
}


/**
 * @brief Read a long word from the PCI config space of a device, using vfio-pci
 */
static uint32_t read_pci_config_long (const int device_fd, const uint32_t offset)
{
    uint32_t config_long;

    read_pci_config_bytes (device_fd, offset, sizeof (config_long), &config_long);

    return config_long;
}


/**
 * @brief Display information about one device in an IOMMU group.
 * @details This program was created for investigating use of vfio-pci, so only decodes the information for vfio-pci devices.
 *          The conditional compilation is to support compiling under Ubuntu 18.04.6 LTS which doesn't have the all the
 *          capabilities in vfio.h
 * @param[in] group_fd The file descriptor for the IOMMU group the device is in
 * @param[in] device_name Identifies the device, from the name within /sys/kernel/iommu_groups/<iommu_group>/devices
 */
static void display_device_information (const int group_fd, const char *const device_name)
{
    int rc;
    int device_fd;
    struct vfio_device_info device_info;
    struct vfio_region_info region_info_size;
    struct vfio_region_info *region_info = NULL;
    struct vfio_irq_info irq_info;
    uint16_t command;
    uint64_t raw_base_addr;
    uint64_t base_addr;
    bool is_IO;
    bool is_prefetchable;
    bool is_64;

    device_fd = ioctl (group_fd, VFIO_GROUP_GET_DEVICE_FD, device_name);
    if (device_fd < 0)
    {
        /* This can happen for PCI bridges, which appear in the IOMMU group but which the vfio-pci driver doesn't bind to */
        fprintf (stderr, "VFIO_GROUP_GET_DEVICE_FD (%s) failed : %s\n", device_name, strerror (-device_fd));
        return;
    }

    /* Get the device information. Doesn't attempt to display device capabilities, as only for IBM s390 zPCI devices. */
    memset (&device_info, 0, sizeof (device_info));
    device_info.argsz = sizeof (device_info);
    rc = ioctl (device_fd, VFIO_DEVICE_GET_INFO, &device_info);
    if (rc != 0)
    {
        printf ("  VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
        goto close_device_fd;
    }

    /* Display device information.
     * vfio-pci devices a fixed value for num_regions (VFIO_PCI_NUM_REGIONS) and num_irqs (VFIO_PCI_NUM_IRQS) */
    printf ("  Device %s num_regions=%" PRIu32 " num_irqs=%" PRIu32 "\n", device_name, device_info.num_regions, device_info.num_irqs);
    if ((device_info.flags & VFIO_DEVICE_FLAGS_RESET) != 0)
    {
        printf ("    Device supports reset\n");
    }
    if ((device_info.flags & VFIO_DEVICE_FLAGS_PCI) != 0)
    {
        printf ("    vfio-pci device\n");
    }
    if ((device_info.flags & VFIO_DEVICE_FLAGS_PLATFORM) != 0)
    {
        printf ("    vfio-platform device\n");
    }
    if ((device_info.flags & VFIO_DEVICE_FLAGS_AMBA) != 0)
    {
        printf ("    vfio-amba device\n");
    }
    if ((device_info.flags & VFIO_DEVICE_FLAGS_CCW) != 0)
    {
        printf ("    vfio-ccw device\n");
    }
    if ((device_info.flags & VFIO_DEVICE_FLAGS_AP) != 0)
    {
        printf ("    vfio-ap device\n");
    }

    if ((device_info.flags & VFIO_DEVICE_FLAGS_PCI) != 0)
    {
        /* Display the implemented regions, which have a non-zero size */
        for (uint32_t region_index = 0; region_index < VFIO_PCI_NUM_REGIONS; region_index++)
        {
            /* Determine the size to get the capabilities */
            memset (&region_info_size, 0, sizeof (region_info_size));
            region_info_size.argsz = sizeof (region_info_size);
            region_info_size.index = region_index;
            rc = ioctl (device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info_size);
            if (rc == -EPERM)
            {
                /* Can happen for VFIO_PCI_VGA_REGION_INDEX */
                continue;
            }
            else if (rc != 0)
            {
                printf ("  VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
                goto close_device_fd;
            }

            /* Allocate memory and get the region information including capabilities */
            region_info = realloc (region_info, region_info_size.argsz);
            region_info->argsz = region_info_size.argsz;
            region_info->index = region_index;
            rc = ioctl (device_fd, VFIO_DEVICE_GET_REGION_INFO, region_info);
            if (rc != 0)
            {
                printf ("  VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
                goto close_device_fd;
            }

            if (region_info->size > 0)
            {
                printf ("    PCI region %s size=0x%llx offset=0x%llx supports:%s%s%s\n",
                        pci_region_names[region_index], region_info->size, region_info->offset,
                        (region_info->flags & VFIO_REGION_INFO_FLAG_READ) != 0 ? " read" : "",
                        (region_info->flags & VFIO_REGION_INFO_FLAG_WRITE) != 0 ? " write" : "",
                        (region_info->flags & VFIO_REGION_INFO_FLAG_MMAP) != 0 ? " mmap" : "");

                if (region_index <= VFIO_PCI_BAR5_REGION_INDEX)
                {
                    /* Display information from the BAR which shows how to decode the BAR information */
                    raw_base_addr = read_pci_config_long (device_fd, (uint32_t) (PCI_BASE_ADDRESS_0 + (region_index * sizeof (uint32_t))));
                    is_IO = (raw_base_addr & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO;
                    is_prefetchable = (!is_IO) && ((raw_base_addr & PCI_BASE_ADDRESS_MEM_PREFETCH) != 0);
                    is_64 = (!is_IO) && ((raw_base_addr & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0);

                    if (is_64)
                    {
                        raw_base_addr |= ((uint64_t) read_pci_config_long (device_fd,
                                (uint32_t) (PCI_BASE_ADDRESS_0 + ((region_index + 1) * sizeof (uint32_t))))) << 32;
                    }
                    base_addr = is_IO ? (raw_base_addr & PCI_BASE_ADDRESS_IO_MASK) : (raw_base_addr & PCI_BASE_ADDRESS_MEM_MASK);

                    printf ("    bar[%u] base_addr=0x%" PRIx64 " is_IO=%u is_prefetchable=%u is_64=%u\n",
                            region_index, base_addr, is_IO, is_prefetchable, is_64);
                }

                if ((region_info->flags & VFIO_REGION_INFO_FLAG_CAPS) != 0)
                {
                    /* Report region capabilities, by following the chain */
                    const char *const info_start = (const char *) region_info;
                    __u32 cap_offset = region_info->cap_offset;


                    while ((cap_offset > 0) && (cap_offset < region_info->argsz))
                    {
                        const struct vfio_info_cap_header *const cap_header =
                                (const struct vfio_info_cap_header *) &info_start[cap_offset];

                        switch (cap_header->id)
                        {
                        case VFIO_REGION_INFO_CAP_SPARSE_MMAP:
                            {
                                const struct vfio_region_info_cap_sparse_mmap *const cap_sparse_mmap =
                                        (const struct vfio_region_info_cap_sparse_mmap *) cap_header;

                                printf ("        VFIO_REGION_INFO_CAP_SPARSE_MMAP version=%" PRIu16 "\n",
                                        cap_sparse_mmap->header.version);
                                for (__u32 area_index = 0; area_index < cap_sparse_mmap->nr_areas; area_index++)
                                {
                                    const struct vfio_region_sparse_mmap_area *const area = &cap_sparse_mmap->areas[area_index];

                                    printf ("      [%" PRIu32 "] offset=0x%llx size=0x%llx\n", area_index, area->offset, area->size);
                                }
                            }
                            break;

                        case VFIO_REGION_INFO_CAP_TYPE:
                            {
                                const struct vfio_region_info_cap_type *const cap_type =
                                        (const struct vfio_region_info_cap_type *) cap_header;

                                printf ("      VFIO_REGION_INFO_CAP_TYPE version=%" PRIu16 " type=0x%" PRIx32 " subtype=0x%" PRIx32 "\n",
                                        cap_type->header.version, cap_type->type, cap_type->subtype);
                            }
                            break;

#ifdef VFIO_REGION_INFO_CAP_MSIX_MAPPABLE
                        case VFIO_REGION_INFO_CAP_MSIX_MAPPABLE:
                            printf ("      VFIO_REGION_INFO_CAP_MSIX_MAPPABLE version=%" PRIu16 "\n", cap_header->version);
                            break;
#endif

#ifdef VFIO_REGION_INFO_CAP_NVLINK2_SSATGT
                        case VFIO_REGION_INFO_CAP_NVLINK2_SSATGT:
                            {
                                struct vfio_region_info_cap_nvlink2_ssatgt *const cap_nvlink2_ssatgt =
                                        (struct vfio_region_info_cap_nvlink2_ssatgt *) cap_header;

                                printf ("      VFIO_REGION_INFO_CAP_NVLINK2_SSATGT version=%" PRIu16 " tgt=0x%llx\n",
                                        cap_nvlink2_ssatgt->header.version, cap_nvlink2_ssatgt->tgt);
                            }
                            break;
#endif

#ifdef VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD
                        case VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD:
                            {
                                const struct vfio_region_info_cap_nvlink2_lnkspd *const cap_nvlink2_lnkspd =
                                        (const struct vfio_region_info_cap_nvlink2_lnkspd *) cap_header;

                                printf ("      VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD version=%" PRIu16 " link_speed=%" PRIu32 "\n",
                                        cap_nvlink2_lnkspd->header.version, cap_nvlink2_lnkspd->link_speed);
                            }
                            break;
#endif

                        default:
                            printf ("      Unknown region capability id=%" PRIu16 " version=%" PRIu16 "\n",
                                    cap_header->id, cap_header->version);
                            break;
                        }

                        cap_offset = cap_header->next;
                    }
                }
            }
        }

        /* Display the implemented IRQ blocks, which have non-zero counts */
        for (uint32_t irq_index = 0; irq_index < VFIO_PCI_NUM_IRQS; irq_index++)
        {
            memset (&irq_info, 0, sizeof (irq_info));
            irq_info.argsz = sizeof (irq_info);
            irq_info.index = irq_index;
            rc = ioctl (device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info);
            if (rc == -EPERM)
            {
                /* Can happen for VFIO_PCI_ERR_IRQ_INDEX */
                continue;
            }
            else if (rc != 0)
            {
                printf ("    VFIO_DEVICE_GET_IRQ_INFO failed : %s\n", strerror (-rc));
                goto close_device_fd;
            }

            if (irq_info.count > 0)
            {
                printf ("    IRQ block %s count=%" PRIu32 " flags:%s%s%s%s\n",
                        irq_block_names[irq_index], irq_info.count,
                        (irq_info.flags & VFIO_IRQ_INFO_EVENTFD) != 0 ? " eventfd" : "",
                        (irq_info.flags & VFIO_IRQ_INFO_MASKABLE) != 0 ? " maskable" : "",
                        (irq_info.flags & VFIO_IRQ_INFO_AUTOMASKED) != 0 ? " automasked" : "",
                        (irq_info.flags & VFIO_IRQ_INFO_NORESIZE) != 0 ? " noresize" : "");
            }
        }

        /* Display the device identification */
        printf ("    Device [%04" PRIx16 ":%04" PRIx16 "] Subsystem [%04" PRIx16 ":%04" PRIx16 "]\n",
                read_pci_config_word (device_fd, PCI_VENDOR_ID),
                read_pci_config_word (device_fd, PCI_DEVICE_ID),
                read_pci_config_word (device_fd, PCI_SUBSYSTEM_VENDOR_ID),
                read_pci_config_word (device_fd, PCI_SUBSYSTEM_ID));

        /* Display the command word */
        command = read_pci_config_word (device_fd, PCI_COMMAND);
        printf ("    control: I/O%s Mem%s BusMaster%s\n",
                (command & PCI_COMMAND_IO) ? "+" : "-",
                (command & PCI_COMMAND_MEMORY) ? "+" : "-",
                (command & PCI_COMMAND_MASTER) ? "+" : "-");
    }
    else
    {
        printf ("  Skipping decoding regions for non vfio-pci device\n");
    }

    /* Close the device */
    close_device_fd:
    rc = close (device_fd);
    if (rc != 0)
    {
        printf ("  close (%s) failed : %s\n", device_name, strerror (errno));
    }
}


int main (int argc, char *argv[])
{
    int container_fd;
    int group_fd;
    int api_version;
    int rc;
    DIR *vfio_dir;
    struct dirent *vfio_dir_entry;
    uint32_t iommu_group;
    char group_pathname[PATH_MAX];
    struct vfio_group_status group_status;
    __s32 iommu_type;
    char group_dirname[PATH_MAX];
    DIR *group_dir;
    struct dirent *group_dir_entry;
    int saved_errno;

    /* At boot only root has access to this container file.
     * After loading the vfio-pci module this file then has 0666 permission. Haven't tracked what changed the permission */
    container_fd = open (VFIO_CONTAINER_PATH, O_RDWR);
    if (container_fd == -1)
    {
        fprintf (stderr, "open (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }

    api_version = ioctl (container_fd, VFIO_GET_API_VERSION);
    if (api_version != VFIO_API_VERSION)
    {
        fprintf (stderr, "Got VFIO_API_VERSION %d, expected %d\n", api_version, VFIO_API_VERSION);
        exit (EXIT_FAILURE);
    }

    /* Display which extensions are supported by the base driver. */
    printf ("Extension support for %s:\n", VFIO_CONTAINER_PATH);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_TYPE1_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_SPAPR_TCE_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_TYPE1v2_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_DMA_CC_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_EEH);
/* https://github.com/torvalds/linux/commit/35890f85573c2ebbbf3491dc66f7ee2ad63055af replaced VFIO_TYPE1_NESTING_IOMMU
 * with __VFIO_RESERVED_TYPE1_NESTING_IOMMU.
 * Updating an AlmaLinux 9 installation to kernel-headers-5.14.0-611.9.1.el9_7.x86_64 got this change. */
#ifdef VFIO_TYPE1_NESTING_IOMMU
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_TYPE1_NESTING_IOMMU);
#endif
#ifdef __VFIO_RESERVED_TYPE1_NESTING_IOMMU
    DISPLAY_EXTENSION_SUPPORT (container_fd, __VFIO_RESERVED_TYPE1_NESTING_IOMMU);
#endif
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_SPAPR_TCE_v2_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_NOIOMMU_IOMMU);

    /* Iterate over all IOMMU groups which are bound to a driver, attempting to display information.
     * This does a directory search to find numeric group IDs.
     *
     * If there are multiple groups, the IOMMU capability is reported for each group which is redundant
     * information. This is because the IOMMU capability (on the container) can only be reported
     * once an IOMMU group has been added to the IOMMU container. */
    vfio_dir = opendir (VFIO_ROOT_PATH);
    if (vfio_dir != NULL)
    {
        for (vfio_dir_entry = readdir (vfio_dir); vfio_dir_entry != NULL; vfio_dir_entry = readdir (vfio_dir))
        {
            if ((sscanf (vfio_dir_entry->d_name, "%" SCNu32, &iommu_group) == 1) ||
                (sscanf (vfio_dir_entry->d_name, "noiommu-%" SCNu32, &iommu_group) == 1))
            {
                /* Attempt to open the group file, which can fail with EBUSY if already open by another program (e.g. DPDK).
                 * EBUSY can happen with noiommu mode as well. */
                printf ("\nIOMMU group %s:\n", vfio_dir_entry->d_name);
                snprintf (group_pathname, sizeof (group_pathname), "%s%s", VFIO_ROOT_PATH, vfio_dir_entry->d_name);
                errno = 0;
                group_fd = open (group_pathname, O_RDWR);
                saved_errno = errno;
                if (group_fd == -1)
                {
                    if ((saved_errno == EPERM) && (strncmp (vfio_dir_entry->d_name, "noiommu", strlen ("noiommu")) == 0))
                    {
                        /* With a noiommu group permission on the group file isn't sufficient.
                         * Need to sys_rawio capability to open the group. */
                        printf ("  No permission to open %s. Try:\nsudo setcap cap_sys_rawio=ep %s\n",
                                vfio_dir_entry->d_name, argv[0]);
                    }
                    else
                    {
                        printf ("  open (%s) failed : %s\n", group_pathname, strerror (errno));
                    }
                    continue;
                }

                /* Get status of the group */
                memset (&group_status, 0, sizeof (group_status));
                group_status.argsz = sizeof (group_status);
                rc = ioctl (group_fd, VFIO_GROUP_GET_STATUS, &group_status);
                if (rc != 0)
                {
                    printf ("  VFIO_GROUP_GET_STATUS failed : %s\n", strerror (-rc));
                    continue;
                }
                printf ("  viable=%d  container_set=%d\n",
                        (group_status.flags & VFIO_GROUP_FLAGS_VIABLE) != 0,
                        (group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) != 0);

                if ((group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0)
                {
                    /* For a non-viable group, VFIO_GROUP_GET_DEVICE_FD fails with EPERM for devices in the group */
                    printf ("  group is not viable (ie, not all devices bound for vfio)\n");
                    continue;
                }

                /* Need to add the group to a container before further IOCTLs are possible */
                if ((group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) == 0)
                {
                    rc = ioctl (group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd);
                    if (rc != 0)
                    {
                        printf ("  VFIO_GROUP_SET_CONTAINER failed : %s\n", strerror (-rc));
                        continue;
                    }
                    printf ("  Set container for group\n");
                }

                /* Set the IOMMU type used. As per DPDK uses type 1 if supported, otherwise noiommu. */
                iommu_type = VFIO_TYPE1_IOMMU;
                rc = ioctl (container_fd, VFIO_SET_IOMMU, iommu_type);
                if (rc != 0)
                {
                    iommu_type = VFIO_NOIOMMU_IOMMU;
                    rc = ioctl (container_fd, VFIO_SET_IOMMU, iommu_type);
                }
                if (rc != 0)
                {
                    printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (-rc));
                    continue;
                }
                printf ("  IOMMU type set to %" PRIi32 "\n", iommu_type);

                if (iommu_type == VFIO_TYPE1_IOMMU)
                {
                    display_type1_iommu_capabilities (container_fd);
                }

                /* Display information about all devices in the group */
                snprintf (group_dirname, sizeof (group_dirname), "/sys/kernel/iommu_groups/%" PRIu32 "/devices", iommu_group);
                group_dir = opendir (group_dirname);
                if (group_dir != NULL)
                {
                    for (group_dir_entry = readdir (group_dir); group_dir_entry != NULL; group_dir_entry = readdir (group_dir))
                    {
                        if ((strcmp (group_dir_entry->d_name, ".") != 0) && (strcmp (group_dir_entry->d_name, "..") != 0))
                        {
                            display_device_information (group_fd, group_dir_entry->d_name);
                        }
                    }
                    closedir (group_dir);
                }

                /* Close this group */
                rc = close (group_fd);
                if (rc != 0)
                {
                    printf ("  close (%s) failed : %s\n", group_pathname, strerror (errno));
                }
            }
        }
        closedir (vfio_dir);
    }

    rc = close (container_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
