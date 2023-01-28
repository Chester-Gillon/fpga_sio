/*
 * @file fury_utils.c
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Utilities to support testing a NiteFury or LiteFury
 */

#include "fury_utils.h"

#include <string.h>


/**
 * @brief Identify if a PCI device is a NiteFury or LiteFury
 * @param[in] vfio_device The PCI device to identify.
 * @param[out] board_version If the PCI device is a NiteFury or LiteFury, it's version
 * @return Indicates if the PCI device is a NiteFury / LiteFury or not.
 */
fury_type_t identify_fury (const vfio_device_t *const vfio_device, uint32_t *const board_version)
{
    fury_type_t fury_type = DEVICE_OTHER;

    if (vfio_device->regions_info[FURY_AXI_PERIPHERALS_BAR].size == 0x20000)
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
