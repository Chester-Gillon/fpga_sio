/*
 * @file pci_sysfs_access.c
 * @date 25 May 2024
 * @author Chester Gillon
 * @brief Functions to access PCI devices by Linux /sys/bus/pci
 */

#include "pci_sysfs_access.h"

#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <unistd.h>
#include <libgen.h>


/**
 * @brief Read a property name which is the basename of a symlink of a PCI device
 * @param[in] domain, bus, dev, func Identifies the PCI device
 * @param[in] property_name The name of the property, which is a symlink filename inside the PCI device directory
 * @return The value of the property if non-NULL, or NULL if the device or property doesn't exist
 */
const char *pci_sysfs_read_device_symlink_name (const uint32_t domain, const uint32_t bus, const uint32_t dev, const uint32_t func,
                                                const char *const property_name)
{
    char device_pathname[PATH_MAX];
    char property_pathname[PATH_MAX];
    const char *property_value = NULL;
    ssize_t property_length;

    snprintf (device_pathname, sizeof (device_pathname), "/sys/bus/pci/devices/%04x:%02x:%02x.%x/%s",
            domain, bus, dev, func, property_name);
    property_length = readlink (device_pathname, property_pathname, sizeof (property_pathname) - 1);
    if (property_length > 0)
    {
        property_pathname[property_length] = '\0';
        property_value = strdup (basename (property_pathname));
    }

    return property_value;
}
