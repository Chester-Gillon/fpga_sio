/*
 * @file display_vfio_information.c
 * @date Created on: 27 Dec 2022
 * @author Chester Gillon
 * @brief Initial program for displaying information about VFIO
 * @detail Not aware of standard "helper" user space libraries for VFIO, so uses the raw IOCTLs.
 *         DPDK is an example user space application making use of VFIO IOCTLs.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/vfio.h>

#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_FILE_PATH VFIO_ROOT_PATH "vfio"


/*@
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

int main (int argc, char *argv[])
{
    int vfio_file_fd;
    int api_version;
    int rc;

    vfio_file_fd = open (VFIO_FILE_PATH, O_RDONLY);
    if (vfio_file_fd == -1)
    {
        fprintf (stderr, "open (%s) failed : %s\n", VFIO_FILE_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }

    api_version = ioctl (vfio_file_fd, VFIO_GET_API_VERSION);
    if (api_version != VFIO_API_VERSION)
    {
        fprintf (stderr, "Got VFIO_API_VERSION %d, expected %d\n", api_version, VFIO_API_VERSION);
        exit (EXIT_FAILURE);
    }

    /* Display which extensions are supported by the base driver. */
    printf ("Extension support for %s:\n", VFIO_FILE_PATH);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_TYPE1_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_SPAPR_TCE_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_TYPE1v2_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_DMA_CC_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_EEH);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_TYPE1_NESTING_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_SPAPR_TCE_v2_IOMMU);
    DISPLAY_EXTENSION_SUPPORT (vfio_file_fd, VFIO_NOIOMMU_IOMMU);

    rc = close (vfio_file_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", VFIO_FILE_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
