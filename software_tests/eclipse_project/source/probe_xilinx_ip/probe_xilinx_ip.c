/*
 * @file probe_xilinx_ip.c
 * @date 21 Jan 2023
 * @author Chester Gillon
 * @brief Probe PCI devices with the Xilinx vendor ID for Xilinx IP
 * @details Works by mapping the BARs looking for the identity registers for Xilinx IP.
 *          Assumes reads are not destructive.
 *          Uses vifo to map the BARs.
 *
 *          This was created after looking at the Xilinx Kernel module for the DMA/Bridge Subsystem for PCI Express
 *          and seeing that probed to identify the capability of the system, with a view that was applicable to
 *          other IP. However, the limitations when trying to probe other IP was:
 *          a. In the examples used, didn't find AXI slaves which had identification registers.
 *          b. Attempting to read from from unimplemented AXI slave addresses can hang the PC, requiring the
 *             a hard power cycle to recover. See probe_nite_fury_or_lite_fury().
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <limits.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pci/pci.h>
#include <linux/vfio.h>

#include "fpga_sio_pci_ids.h"


#define VFIO_ROOT_PATH "/dev/vfio/"
#define VFIO_CONTAINER_PATH VFIO_ROOT_PATH "vfio"


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


/**
 * @brief Open an VFIO device, and map all its memory BARs
 * @param[in/out] vfio_devices The list of vfio devices to append the opened device to.
 *                             If this function is successful vfio_devices->num_devices is incremented
 * @param[in] pci_dev The PCI device to open using VFIO
 */
static void open_vfio_device (vfio_devices_t *const vfio_devices, struct pci_dev *const pci_dev)
{
    int rc;
    int saved_errno;
    int api_version;
    void *addr;
    vfio_device_t *const new_device = &vfio_devices->devices[vfio_devices->num_devices];

    /* Check the PCI device has an IOMMU group. */
    snprintf (new_device->device_name, sizeof (new_device->device_name), "%04x:%02x:%02x.%x",
            pci_dev->domain, pci_dev->bus, pci_dev->dev, pci_dev->func);
    new_device->iommu_group = pci_get_string_property (pci_dev, PCI_FILL_IOMMU_GROUP);
    if (new_device->iommu_group == NULL)
    {
        printf ("Skipping device %s (%04x:%04x) as no IOMMU group\n",
                new_device->device_name, pci_dev->vendor_id, pci_dev->device_id);
        return;
    }

    /* Sanity check that the IOMMU group file exists and the effective user ID has read/write permission before attempting
     * to probe the device. This checks that bind_xilinx_devices_to_vfio.sh script has been run to bind the vfio-pci driver
     * (which creates the IOMMU group file) and has given the user permission. */
    snprintf (new_device->group_pathname, sizeof (new_device->group_pathname), "%s%s",
            VFIO_ROOT_PATH, new_device->iommu_group);
    errno = 0;
    rc = faccessat (0, new_device->group_pathname, R_OK | W_OK, AT_EACCESS);
    saved_errno = errno;
    if (rc != 0)
    {
        /* Sanity check failed, reported diagnostic message and return to skip the device */
        if (saved_errno == ENOENT)
        {
            printf ("Skipping device %s (%04x:%04x) as %s doesn't exist implying vfio-pci driver not bound to the device\n",
                    new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->group_pathname);
        }
        else if (saved_errno == EACCES)
        {
            printf ("Skipping device %s (%04x:%04x) as %s doesn't have read/write permission\n",
                    new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->group_pathname);
        }
        else
        {
            printf ("Skipping device %s (%04x:%04x) as %s : %s\n",
                    new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->group_pathname,
                    strerror (saved_errno));
        }
        return;
    }

    /* For the first VFIO device open a VFIO container, which is also used for subsequent devices */
    if (vfio_devices->num_devices == 0)
    {
        vfio_devices->container_fd = open (VFIO_CONTAINER_PATH, O_RDWR);
        if (vfio_devices->container_fd == -1)
        {
            fprintf (stderr, "open (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
            exit (EXIT_FAILURE);
        }

        api_version = ioctl (vfio_devices->container_fd, VFIO_GET_API_VERSION);
        if (api_version != VFIO_API_VERSION)
        {
            fprintf (stderr, "Got VFIO_API_VERSION %d, expected %d\n", api_version, VFIO_API_VERSION);
            exit (EXIT_FAILURE);
        }
    }

    /* Open the IOMMU group */
    printf ("Opening device %s (%04x:%04x) with IOMMU group %s\n",
            new_device->device_name, pci_dev->vendor_id, pci_dev->device_id, new_device->iommu_group);
    snprintf (new_device->group_pathname, sizeof (new_device->group_pathname), "%s%s", VFIO_ROOT_PATH, new_device->iommu_group);
    new_device->group_fd = open (new_device->group_pathname, O_RDWR);
    if (new_device->group_fd == -1)
    {
        printf ("open (%s) failed : %s\n", new_device->group_pathname, strerror (errno));
        return;
    }

    /* Get the status of the group and check that viable */
    memset (&new_device->group_status, 0, sizeof (new_device->group_status));
    new_device->group_status.argsz = sizeof (new_device->group_status);
    rc = ioctl (new_device->group_fd, VFIO_GROUP_GET_STATUS, &new_device->group_status);
    if (rc != 0)
    {
        printf ("FIO_GROUP_GET_STATUS failed : %s\n", strerror (-rc));
        return;
    }

    if ((new_device->group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0)
    {
        printf ("group is not viable (ie, not all devices bound for vfio)\n");
        return;
    }

    /* Need to add the group to a container before further IOCTLs are possible */
    if ((new_device->group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET) == 0)
    {
        rc = ioctl (new_device->group_fd, VFIO_GROUP_SET_CONTAINER, &vfio_devices->container_fd);
        if (rc != 0)
        {
            printf ("VFIO_GROUP_SET_CONTAINER failed : %s\n", strerror (-rc));
            return;
        }
    }

    if (vfio_devices->num_devices == 0)
    {
        /* Set the IOMMU type used. As this is done on the IOMMU container, only performed when the first device is opended */
        rc = ioctl (vfio_devices->container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
        if (rc != 0)
        {
            printf ("  VFIO_SET_IOMMU failed : %s\n", strerror (-rc));
            return;
        }
    }

    /* Open the device */
    new_device->device_fd = ioctl (new_device->group_fd, VFIO_GROUP_GET_DEVICE_FD, new_device->device_name);
    if (new_device->device_fd < 0)
    {
        fprintf (stderr, "VFIO_GROUP_GET_DEVICE_FD (%s) failed : %s\n", new_device->device_name, strerror (-new_device->device_fd));
        return;
    }

    /* Get the device information. As this program is written for a PCI device which has fixed enumerations for regions,
     * the only use of the device information is a sanity check that VFIO reports a PCI device. */
    memset (&new_device->device_info, 0, sizeof (new_device->device_info));
    new_device->device_info.argsz = sizeof (new_device->device_info);
    rc = ioctl (new_device->device_fd, VFIO_DEVICE_GET_INFO, &new_device->device_info);
    if (rc != 0)
    {
        printf ("VFIO_DEVICE_GET_INFO failed : %s\n", strerror (-rc));
        return;
    }

    if ((new_device->device_info.flags & VFIO_DEVICE_FLAGS_PCI) == 0)
    {
        printf ("VFIO_DEVICE_GET_INFO flags don't report a PCI device\n");
        return;
    }

    /* Map all possible BARs */
    for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        struct vfio_region_info *const region_info = &new_device->regions_info[bar_index];

        /* Get region information for PCI BAR, to determine if an implemented BAR which can be mapped */
        memset (region_info, 0, sizeof (*region_info));
        region_info->argsz = sizeof (*region_info);
        region_info->index = bar_index;
        rc = ioctl (new_device->device_fd, VFIO_DEVICE_GET_REGION_INFO, region_info);
        if (rc != 0)
        {
            printf ("VFIO_DEVICE_GET_REGION_INFO failed : %s\n", strerror (-rc));
            return;
        }


        if ((region_info->size > 0) && ((region_info->flags & VFIO_REGION_INFO_FLAG_MMAP) != 0))
        {
            /* Map the entire BAR */
            addr = mmap (NULL, region_info->size, PROT_READ | PROT_WRITE, MAP_SHARED, new_device->device_fd, region_info->offset);
            if (addr == MAP_FAILED)
            {
                printf ("mmap() failed : %s\n", strerror (errno));
                return;
            }
            new_device->mapped_bars[bar_index] = addr;
        }
        else
        {
            new_device->mapped_bars[bar_index] = NULL;
        }
    }

    /* Record device successfully opened */
    vfio_devices->num_devices++;
}


/**
 * @brief Close all the open VFIO devices
 * @param[in/out] vfio_devices The VFIO devices to close
 */
static void close_vfio_devices (vfio_devices_t *const vfio_devices)
{
    int rc;

    for (uint32_t device_index = 0; device_index < vfio_devices->num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices->devices[device_index];

        for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
        {
            if (vfio_device->mapped_bars[bar_index] != NULL)
            {
                /* Unmap the BAR */
                rc = munmap (vfio_device->mapped_bars[bar_index], vfio_device->regions_info[bar_index].size);
                if (rc != 0)
                {
                    printf ("munmap() failed : %s\n", strerror (errno));
                    exit (EXIT_FAILURE);
                }
                vfio_device->mapped_bars[bar_index] = NULL;
            }
        }

        rc = close (vfio_device->device_fd);
        if (rc != 0)
        {
            fprintf (stderr, "close (%s) failed : %s\n", vfio_device->device_name, strerror (errno));
            exit (EXIT_FAILURE);
        }
        vfio_device->device_fd = -1;

        rc = close (vfio_device->group_fd);
        if (rc != 0)
        {
            fprintf (stderr, "close (%s) failed : %s\n", vfio_device->group_pathname, strerror (errno));
            exit (EXIT_FAILURE);
        }
        vfio_device->group_fd = -1;
    }

    rc = close (vfio_devices->container_fd);
    if (rc != 0)
    {
        fprintf (stderr, "close (%s) failed : %s\n", VFIO_CONTAINER_PATH, strerror (errno));
        exit (EXIT_FAILURE);
    }
    vfio_devices->container_fd = -1;
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
 * @details Check if a memory mapped BAR is that of the "PCIe to AXI Lite Master" in the
 *          https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/tree/master/Sample-Projects/Project-0/FPGA project
 *
 *          This is done by checking the fixed value used in a GPIO input register, which is set to a constant input
 *          inside the FGPA.
 * @param[in] mapped_bar Start of the memory mapped BAR to prove
 * @param[in] bar_size Size of the memory mapped BAR in bytes to probe
 * @return Returns true when the mapped BAR matches the search
 */
static bool probe_nite_fury_or_lite_fury (const uint8_t *const mapped_bar, const uint64_t bar_size)
{
    const uint64_t register_frame_size = 1 << 9;

    for (uint64_t bar_offset = 0; (bar_offset + register_frame_size) <= bar_size; bar_offset += register_frame_size)
    {
        /* pid string is a constant value fed to the GPIO input value */
        const uint32_t pid_integer = read_reg32 (mapped_bar, bar_offset + 0x0);
        const char *const pid_bytes = (const char *) &pid_integer;
        char pid_string[4];

        /* Need to reverse the bytes to get the pid string */
        pid_string[0] = pid_bytes[3];
        pid_string[1] = pid_bytes[2];
        pid_string[2] = pid_bytes[1];
        pid_string[3] = pid_bytes[0];

        if ((strncmp (pid_string, "LITE", 4) == 0) || (strncmp (pid_string, "NITE", 4) == 0))
        {
            /* board_version is a constant value fed to the GPIO2 input value */
            const uint32_t board_version = read_reg32 (mapped_bar, bar_offset + 0x8);

            printf ("Found %.4s Fury at BAR offset 0x%" PRIx64" board_version=%" PRIu32 "\n", pid_string, bar_offset, board_version);

            /* @todo Stop the probe if the Nite-Fury or Lite-Fury PID is found since:
             *       a. None of the Xilinx IP (AXI-GPIO, AXI Quad SPI, XADC Wizard) used in the BAR has any identity registers.
             *       b. Since not all address bits seem to be decoded the pid_string can be found at multiple aliases addresses.
             *       c. Attempting to read from an unimplemented offset can cause the PC to hang.
             *          Re-loading the FGPA didn't help to cause the PC to resume; had to hard power cycle.
             *          Not sure why the PCIe read doesn't fail with a completion timeout.
             */
            return true;
        }
    }

    return false;
}


/**
 * @brief probe the the registers in the DMA bridge of the Xilinx DMA/Bridge Subsystem for PCI Express
 * @details The identification registers checked for are from https://docs.xilinx.com/r/en-US/pg195-pcie-dma/Register-Space
 * @param[in] mapped_bar Start of the memory mapped BAR to prove
 * @param[in] bar_size Size of the memory mapped BAR in bytes to probe
 */
static void probe_xilinx_dma_bridge (const uint8_t *const mapped_bar, const uint64_t bar_size)
{
    const uint64_t register_frame_size = 1 << 9;
    const uint64_t dma_subsystem_identity = 0x1fc;

    /* Enumeration for the channel_target field in the channel identification register */
    enum
    {
        target_h2c_channels = 0,
        target_c2h_channels = 1,
        target_irq_block = 2,
        target_config = 3,
        target_h2c_sgdma = 4,
        target_c2h_sgdma = 5,
        target_sgdma_common = 6,
        target_msi_x = 8 /* Can't be reported as the MSI-X block doesn't have a channel_identification register */
    };

    for (uint64_t bar_offset = 0; (bar_offset + register_frame_size) <= bar_size; bar_offset += register_frame_size)
    {
        const uint32_t channel_identification = read_reg32 (mapped_bar, bar_offset + 0);
        const uint32_t subsystem_identifier = (channel_identification & 0xFFF00000) >> 20;
        const uint32_t channel_target =       (channel_identification & 0x000F0000) >> 16;
        const uint32_t stream =               (channel_identification & 0x00008000) >> 15;
        const uint32_t channel_id_target =    (channel_identification & 0x00000F00) >> 8;
        const uint32_t version =              (channel_identification & 0x000000FF);

        if (subsystem_identifier == dma_subsystem_identity)
        {
            switch (channel_target)
            {
            case target_h2c_channels:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " H2C Channels stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_c2h_channels:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " C2H Channels stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_irq_block:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " IRQ Block version=%" PRIu32 "\n",
                        bar_offset, version);
                break;

            case target_config:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " Config Block version=%" PRIu32 "\n",
                        bar_offset, version);
                break;

            case target_h2c_sgdma:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " H2C SGDMA stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_c2h_sgdma:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " C2H SGDMA stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_sgdma_common:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " SGDMA Common version=%" PRIu32 "\n",
                        bar_offset, version);
                break;

            default:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " unknown channel_target=0x%" PRIu32 "\n",
                        bar_offset, channel_target);
                break;
            }
        }
    }
}


/**
 * @brief Probe the memory mapped BARs of a vfio device looking for fixed identifiers for IP
 * @param[in] vfio_device The VFIO device to probe
 */
static void probe_vfio_device_for_xilinx_ip (vfio_device_t *const vfio_device)
{
    bool match;
    for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        const uint8_t *const mapped_bar = vfio_device->mapped_bars[bar_index];
        const uint64_t bar_size = vfio_device->regions_info[bar_index].size;

        if (mapped_bar != NULL)
        {
            printf ("Probing BAR %d in device %s of size 0x%" PRIx64 "\n", bar_index, vfio_device->device_name, bar_size);

            /* Since the "PCIe to AXI Lite Master" in the Nite Fury or Lite Fury can hang the PC when try and
             * read an unimplemented address, only try and probe the next type if not a Nite Fury or Lite Fury. */
            match = probe_nite_fury_or_lite_fury (mapped_bar, bar_size);
            if (!match)
            {
                probe_xilinx_dma_bridge (mapped_bar, bar_size);
            }
        }
    }
}


int main (int argc, char *argv[])
{
    struct pci_access *pacc;
    struct pci_filter filter;
    struct pci_dev *dev;
    int known_fields;
    vfio_devices_t vfio_devices;

    /* Initialise using the defaults */
    pacc = pci_alloc ();
    if (pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (pacc);

    /* Select to filter by vendor only */
    pci_filter_init (pacc, &filter);
    filter.vendor = FPGA_SIO_VENDOR_ID;

    /* Scan the entire bus */
    pci_scan_bus (pacc);

    /* Open the FPGA devices which have an IOMMU group assigned */
    memset (&vfio_devices, 0, sizeof (vfio_devices));
    const int required_fields = PCI_FILL_IDENT | PCI_FILL_IOMMU_GROUP;
    for (dev = pacc->devices; (dev != NULL) && (vfio_devices.num_devices < MAX_VFIO_DEVICES); dev = dev->next)
    {
        if (pci_filter_match (&filter, dev))
        {
            known_fields = pci_fill_info (dev, required_fields);
            if ((known_fields & required_fields) == required_fields)
            {
                open_vfio_device (&vfio_devices, dev);
            }
        }
    }

    /* Probe the VFIO devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        probe_vfio_device_for_xilinx_ip (&vfio_devices.devices[device_index]);
    }

    close_vfio_devices (&vfio_devices);
    pci_cleanup (pacc);

    return EXIT_SUCCESS;
}
