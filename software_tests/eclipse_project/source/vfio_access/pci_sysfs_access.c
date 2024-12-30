/*
 * @file pci_sysfs_access.c
 * @date 25 May 2024
 * @author Chester Gillon
 * @brief Functions to access PCI devices by Linux /sys/bus/pci
 * @details
 *   For simplicity these function doen't attempt to cache the contents read from sysfs files.
 *   I.e. every call results in the sysfs files being read.
 */

#include "pci_sysfs_access.h"

#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <dirent.h>


/**
 * @brief Read a property name which is the basename of a symlink of a PCI device
 * @param[in] domain, bus, dev, func Identifies the PCI device
 * @param[in] property_name The name of the property, which is a symlink filename inside the PCI device directory
 * @return The value of the property if non-NULL, or NULL if the device or property doesn't exist
 */
char *pci_sysfs_read_device_symlink_name (const uint32_t domain, const uint32_t bus, const uint32_t dev, const uint32_t func,
                                          const char *const property_name)
{
    char device_pathname[PATH_MAX];
    char property_pathname[PATH_MAX];
    char *property_value = NULL;
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


/**
 * @brief Obtain the physical slot of a PCI device
 * @details
 *   This searches the /sys/bus/pci/slots sysfs directory, to find an address which matches that of the PCI device.
 *
 *   There is no func argument, since the function is not part of the physical slot address. This is because all
 *   functions on a PCI device share the same physical slot.
 * @param[in] domain, bus, dev, Identifies the PCI device
 * @return The physical slot of the device if non-NULL, or NULL if unable to identify the physical slot
 */
char *pci_sysfs_read_physical_slot (const uint32_t domain, const uint32_t bus, const uint32_t dev)
{
    const char *const slots_dirname = "/sys/bus/pci/slots";
    char *property_value = NULL;
    DIR *slots_dir;
    struct dirent *slot_entry;
    char address_pathname[PATH_MAX];
    FILE *address_fid;
    uint32_t slot_domain;
    uint32_t slot_bus;
    uint32_t slot_dev;

    slots_dir = opendir (slots_dirname);
    if (slots_dir != NULL)
    {
        slot_entry = readdir (slots_dir);
        while ((property_value == NULL) && (slot_entry != NULL))
        {
            if (slot_entry->d_type == DT_DIR)
            {
                if ((strcmp (slot_entry->d_name, ".") != 0) && (strcmp (slot_entry->d_name, "..") != 0))
                {
                    snprintf (address_pathname, sizeof (address_pathname), "%s/%s/address", slots_dirname, slot_entry->d_name);
                    address_fid = fopen (address_pathname, "r");
                    if (address_fid != NULL)
                    {
                        const int num_items = fscanf (address_fid, "%x:%x:%x", &slot_domain, &slot_bus, &slot_dev);

                        if (num_items == 3)
                        {
                            if ((slot_domain == domain) && (slot_bus == bus) && (slot_dev == dev))
                            {
                                property_value = strdup (slot_entry->d_name);
                            }
                        }

                        fclose (address_fid);
                    }
                }
            }

            slot_entry = readdir (slots_dir);
        }

        closedir (slots_dir);
    }

    return property_value;
}
