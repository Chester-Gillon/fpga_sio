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

#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


/**
 * @brief Display if a VFIO extension is supported or not, where 1 means supported
 */
#define DISPLAY_EXTENSION_SUPPORT(vfio_file_fd,extension,supported) \
    display_extension_support (vfio_file_fd, extension, #extension, supported)
static void display_extension_support (const int vfio_file_fd, const __u32 extension,
                                       const char *const name, bool *const supported)
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

    if (supported != NULL)
    {
        *supported = rc > 0;
    }
}

int main (int argc, char *argv[])
{
    int container_fd;
    int group_fd;
    int api_version;
    int rc;
    DIR *vfio_dir;
    struct dirent *dir_entry;
    uint32_t iommu_group;
    char group_pathname[PATH_MAX];
    struct vfio_group_status group_status;
    bool type1_iommu_supported;
    struct vfio_iommu_type1_info iommu_info_get_size;
    struct vfio_iommu_type1_info *iommu_info;
    uint64_t page_size;

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
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_TYPE1_IOMMU, &type1_iommu_supported);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_SPAPR_TCE_IOMMU, NULL);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_TYPE1v2_IOMMU, NULL);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_DMA_CC_IOMMU, NULL);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_EEH, NULL);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_TYPE1_NESTING_IOMMU, NULL);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_SPAPR_TCE_v2_IOMMU, NULL);
    DISPLAY_EXTENSION_SUPPORT (container_fd, VFIO_NOIOMMU_IOMMU, NULL);

    /* Iterate over all IOMMU groups which are bound to a driver, attempting to display information.
     * This does a directory search to find numeric group IDs.
     *
     * If there are multiple groups, the IOMMU capability is reported for each group which is redundant
     * information. This is because the IOMMU capability (on the container) can only be reported
     * once an IOPMMU group has been added to the IOMMU container. */
    vfio_dir = opendir (VFIO_ROOT_PATH);
    if (vfio_dir != NULL)
    {
        for (dir_entry = readdir (vfio_dir); dir_entry != NULL; dir_entry = readdir (vfio_dir))
        {
            if (sscanf (dir_entry->d_name, "%" SCNu32, &iommu_group) == 1)
            {
                /* Attempt to open the group file, which can fail with EBUSY if already open by another program (e.g. DPDK) */
                printf ("\nIOMMU group %" PRIu32 ":\n", iommu_group);
                snprintf (group_pathname, sizeof (group_pathname), "%s%s", VFIO_ROOT_PATH, dir_entry->d_name);
                group_fd = open (group_pathname, O_RDWR);
                if (group_fd == -1)
                {
                    printf ("  open (%s) failed : %s\n", group_pathname, strerror (errno));
                    continue;
                }

                /* Get status of the group */
                memset (&group_status, 0, sizeof (group_status));
                group_status.argsz = sizeof (group_status);
                rc = ioctl (group_fd, VFIO_GROUP_GET_STATUS, &group_status);
                if (rc != 0)
                {
                    printf ("  VFIO_GROUP_GET_STATUS failed : %s\n", strerror (errno));
                    continue;
                }
                printf ("  viable=%d  container_set=%d\n",
                        (group_status.flags & VFIO_GROUP_FLAGS_VIABLE) != 0,
                        (group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) != 0);

                if ((group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0)
                {
                    printf ("  group is not viable (ie, not all devices bound for vfio)\n");
                    continue;
                }

                /* Need to add the group to a container before further IOCTLs are possible */
                if ((group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) == 0)
                {
                    rc = ioctl (group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd);
                    if (rc != 0)
                    {
                        printf ("  VFIO_GROUP_SET_CONTAINER failed : %s\n", strerror (errno));
                        continue;
                    }
                    printf ("  Set container for group\n");
                }

                /* Set the IOMMU type used. As per DPDK uses type 1 if supported, otherwise noiommu */
                const __s32 iommu_type = type1_iommu_supported ? VFIO_TYPE1_IOMMU : VFIO_NOIOMMU_IOMMU;
                rc = ioctl (container_fd, VFIO_SET_IOMMU, iommu_type);
                if (rc != 0)
                {
                    printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (errno));
                    continue;
                }
                printf ("  IOMMU type set to %" PRIi32 "\n", iommu_type);

                /* Determine the size required to get the capabilities for the IOMMU.
                 * This updates the argsz to indicate how much space is required. */
                memset (&iommu_info_get_size, 0, sizeof iommu_info_get_size);
                iommu_info_get_size.argsz = sizeof (iommu_info_get_size);
                rc = ioctl (container_fd, VFIO_IOMMU_GET_INFO, &iommu_info_get_size);
                if (rc != 0)
                {
                    printf ("  VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
                    continue;
                }

                /* Allocate a structure of the required size for the IOMMU information, and get it */
                iommu_info = calloc (iommu_info_get_size.argsz, 1);
                iommu_info->argsz = iommu_info_get_size.argsz;
                rc = ioctl (container_fd, VFIO_IOMMU_GET_INFO, iommu_info);
                if (rc != 0)
                {
                    printf ("  VFIO_IOMMU_GET_INFO failed : %s\n", strerror (errno));
                    continue;
                }

                /* Report fixed information in the vfio_iommu_type1_info structure */
#ifdef VFIO_IOMMU_INFO_CAPS
                printf ("  info supports: pagesizes=%d caps=%d\n",
                        (iommu_info->flags & VFIO_IOMMU_INFO_PGSIZES) != 0,
                        (iommu_info->flags & VFIO_IOMMU_INFO_CAPS) != 0);
#else
                printf ("  info supports: pagesizes=%d\n",
                        (iommu_info->flags & VFIO_IOMMU_INFO_PGSIZES) != 0);
#endif
                printf ("  IOVA supported page sizes:");
                for (page_size = 1; page_size != 0; page_size <<= 1)
                {
                    if ((iommu_info->iova_pgsizes & page_size) == page_size)
                    {
                        printf (" 0x%" PRIx64, page_size);
                    }
                }
                printf ("\n");

#ifdef VFIO_IOMMU_INFO_CAPS
                if (((iommu_info->flags & VFIO_IOMMU_INFO_CAPS) != 0) && (iommu_info->cap_offset > 0))
#endif
                {
                    /* Report IOMMU capabilities, by following the chain */
                    const char *const info_start = (const char *) iommu_info;
#ifdef VFIO_IOMMU_INFO_CAPS
                    __u32 cap_offset = iommu_info->cap_offset;
#else
                    /* Have to assume the initial capability offset if the vfio.h API file doesn't define the
                     * capability flags */
                    __u32 cap_offset = sizeof (struct vfio_iommu_type1_info);
#endif

                    while ((cap_offset > 0) && (cap_offset < iommu_info->argsz))
                    {
                        const struct vfio_info_cap_header *const cap_header =
                                (const struct vfio_info_cap_header *) &info_start[cap_offset];

                        switch (cap_header->id)
                        {
                        case VFIO_REGION_INFO_CAP_SPARSE_MMAP:
                            {
                                const struct vfio_region_info_cap_sparse_mmap *const cap_sparse_mmap =
                                        (const struct vfio_region_info_cap_sparse_mmap *) cap_header;

                                printf ("  VFIO_REGION_INFO_CAP_SPARSE_MMAP version=%" PRIu16 "\n",
                                        cap_sparse_mmap->header.version);
                                for (__u32 area_index = 0; area_index < cap_sparse_mmap->nr_areas; area_index++)
                                {
                                    const struct vfio_region_sparse_mmap_area *const area = &cap_sparse_mmap->areas[area_index];

                                    printf ("    [%" PRIu32 "] offset=0x%llx size=0x%llx\n", area_index, area->offset, area->size);
                                }
                            }
                            break;

                        case VFIO_REGION_INFO_CAP_TYPE:
                            {
                                const struct vfio_region_info_cap_type *const cap_type =
                                        (const struct vfio_region_info_cap_type *) cap_header;

                                printf ("  VFIO_REGION_INFO_CAP_TYPE version=%" PRIu16 " type=0x%" PRIx32 " subtype=0x%" PRIx32 "\n",
                                        cap_type->header.version, cap_type->type, cap_type->subtype);
                            }
                            break;

#ifdef VFIO_REGION_INFO_CAP_MSIX_MAPPABLE
                        case VFIO_REGION_INFO_CAP_MSIX_MAPPABLE:
                            printf ("  VFIO_REGION_INFO_CAP_MSIX_MAPPABLE version=%" PRIu16 "\n", cap_header->version);
                            break;
#endif

#ifdef VFIO_REGION_INFO_CAP_NVLINK2_SSATGT
                        case VFIO_REGION_INFO_CAP_NVLINK2_SSATGT:
                            {
                                struct vfio_region_info_cap_nvlink2_ssatgt *const cap_nvlink2_ssatgt =
                                        (struct vfio_region_info_cap_nvlink2_ssatgt *) cap_header;

                                printf ("  VFIO_REGION_INFO_CAP_NVLINK2_SSATGT version=%" PRIu16 " tgt=0x%llx\n",
                                        cap_nvlink2_ssatgt->header.version, cap_nvlink2_ssatgt->tgt);
                            }
                            break;
#endif

#ifdef VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD
                        case VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD:
                            {

                                const struct vfio_region_info_cap_nvlink2_lnkspd *const cap_nvlink2_lnkspd =
                                        (const struct vfio_region_info_cap_nvlink2_lnkspd *) cap_header;

                                printf ("  VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD version=%" PRIu16 " link_speed=%" PRIu32 "\n",
                                        cap_nvlink2_lnkspd->header.version, cap_nvlink2_lnkspd->link_speed);
                            }
                            break;
#endif

                        default:
                            printf ("  Unknown IOMMU capability id=%" PRIu16 " version=%" PRIu16 "\n",
                                    cap_header->id, cap_header->version);
                            break;
                        }

                        cap_offset = cap_header->next;
                    }
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
