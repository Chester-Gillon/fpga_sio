/*
 * @file fury_utils.c
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Utilities to support testing a NiteFury or LiteFury
 */

#include "fury_utils.h"
#include "fpga_sio_pci_ids.h"

#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

/* Filters to identify Fury PCI devices, which are DMA capable */
const vfio_pci_device_filter_t fury_pci_device_filters[] =
{
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = 0x7011,
        .subsystem_vendor_id = 0,
        .subsystem_device_id = 0,
        .enable_bus_master = true
    }
};
const size_t fury_num_pci_device_filters = sizeof (fury_pci_device_filters) / sizeof (fury_pci_device_filters[0]);

/* Names for the Fury devices */
const char *const fury_names[] =
{
    [DEVICE_LITE_FURY] = "LiteFury",
    [DEVICE_NITE_FURY] = "NiteFury"
};

/* DDR sizes for the Fury devices */
const size_t fury_ddr_sizes_bytes[] =
{
    [DEVICE_LITE_FURY] =  512 * 1024 * 1024,
    [DEVICE_NITE_FURY] = 1024 * 1024 * 1024
};


/**
 * @brief Identify if a PCI device is a NiteFury or LiteFury
 * @param[in/out] vfio_device The PCI device to identify.
 * @param[out] board_version If the PCI device is a NiteFury or LiteFury, it's version
 * @return Indicates if the PCI device is a NiteFury / LiteFury or not.
 */
fury_type_t identify_fury (vfio_device_t *const vfio_device, uint32_t *const board_version)
{
    fury_type_t fury_type = DEVICE_OTHER;

    map_vfio_device_bar_before_use (vfio_device, FURY_AXI_PERIPHERALS_BAR);
    if ((vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR] != NULL) &&
        (vfio_device->regions_info[FURY_AXI_PERIPHERALS_BAR].size == 0x20000))
    {
        const uint8_t *const mapped_bar = vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR];

        /* pid string is a constant value fed to the GPIO input value */
        const uint32_t pid_integer = read_reg32 (mapped_bar, 0x0);
        const char *const pid_bytes = (const char *) &pid_integer;
        char pid_string[4];

        /* Need to reverse the bytes to get the pid string */
        pid_string[0] = pid_bytes[3];
        pid_string[1] = pid_bytes[2];
        pid_string[2] = pid_bytes[1];
        pid_string[3] = pid_bytes[0];

        if (strncmp (pid_string, "LITE", 4) == 0)
        {
            fury_type = DEVICE_LITE_FURY;
        }
        else if (strncmp (pid_string, "NITE", 4) == 0)
        {
            fury_type = DEVICE_NITE_FURY;
        }

        if (fury_type != DEVICE_OTHER)
        {
            /* board_version is a constant value fed to the GPIO2 input value */
            *board_version = read_reg32 (mapped_bar, 0x8);
        }
    }

    return fury_type;
}


/**
 * @brief Display the XADC values within Fury devices
 * @details A version of https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/blob/master/Sample-Projects/Project-0/Host/test-general.py
 *          which is:
 *          - Written in C rather than Python.
 *          - Uses the vfio_access library to use memory mapped BARs in a user space application, rather a Kernel driver.
 * @param[in/out] vfio_devices The VFIO devices to check for Fury devices
 */
void display_fury_xadc_values (vfio_devices_t *const vfio_devices)
{
    uint32_t board_version;
    fury_type_t fury_type;
    uint32_t xadc_register_value;

    for (uint32_t device_index = 0; device_index < vfio_devices->num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices->devices[device_index];

        fury_type = identify_fury (vfio_device, &board_version);
        if (fury_type != DEVICE_OTHER)
        {
            printf ("Found %s board version 0x%x for PCI device %s IOMMU group %s\n",
                    fury_names[fury_type], board_version, vfio_device->device_name, vfio_device->iommu_group);

            /* Read and convert XADC register values.
             * The scaling is defined in
             * https://www.xilinx.com/content/dam/xilinx/support/documents/user_guides/ug480_7Series_XADC.pdf
             *
             * The reported values were sanity checked against that shown by the XADC System Monitor values
             * reported over JTAG by the Vivado Hardware Manager. */
            map_vfio_device_bar_before_use (vfio_device, FURY_AXI_PERIPHERALS_BAR);
            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3200);
            printf ("Temp C=%.1f\n", ((double) (xadc_register_value >> 4) * 503.975 / 4096.0) - 273.15);

            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3204);
            printf ("VCCInt=%.2f\n", (double) (xadc_register_value >> 4) * 3.0 / 4096.0);

            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3208);
            printf ("vccaux=%.2f\n", (double) (xadc_register_value >> 4) * 3.0 / 4096.0);

            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3218);
            printf ("vbram=%.2f\n", (double) (xadc_register_value >> 4) * 3.0 / 4096.0);
        }
    }
}


/**
 * @brief Display the file descriptors which are open in the calling process
 * @details This will open a file descriptor to pass the procfs directory containing the file descriptors,
 *          which is suppressed from being displayed.
 * @param[in] process_name Reported to identify the calling process
 */
void display_open_fds (const char *const process_name)
{
    DIR *fd_dir;
    struct dirent *fd_ent;
    char pid_fd_dir[PATH_MAX];
    char fd_ent_pathname[PATH_MAX];
    char pathname_of_fd[PATH_MAX];
    volatile size_t num_chars;

    snprintf (pid_fd_dir, sizeof (pid_fd_dir), "/proc/%d/fd", getpid ());
    fd_dir = opendir (pid_fd_dir);
    printf ("Contents of %s in %s:\n", pid_fd_dir, process_name);
    if (fd_dir != NULL)
    {
        fd_ent = readdir (fd_dir);
        while (fd_ent != NULL)
        {
            if (fd_ent->d_type == DT_LNK)
            {
                /* use of num_chars suppresses -Wformat-truncation, as suggested by
                 * https://stackoverflow.com/a/70938456/4207678 */
                num_chars = sizeof (fd_ent_pathname);
                snprintf (fd_ent_pathname, num_chars, "%s/%s", pid_fd_dir, fd_ent->d_name);

                ssize_t link_num_bytes = readlink (fd_ent_pathname, pathname_of_fd, sizeof (pathname_of_fd));
                if (link_num_bytes > 0)
                {
                    if (strncmp (pathname_of_fd, pid_fd_dir, link_num_bytes) != 0)
                    {
                        printf ("  fd %s -> %.*s\n", fd_ent->d_name, (int) link_num_bytes, pathname_of_fd);
                    }
                }
            }

            fd_ent = readdir (fd_dir);
        }

        closedir (fd_dir);
    }
}
