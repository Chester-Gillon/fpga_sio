/*
 * @file libpciaccess_pci_access.c
 * @date 4 May 2024
 * @author Chester Gillon
 * @brief Implements an instantiation of generic_pci_access using the libpciaccess library
 * @details
 *   Only one generic_pci_access_context_p can be used by a process, since the libpciaccess library doesn't provide
 *   an access context structure.
 */

#include "generic_pci_access.h"
#include "pci_sysfs_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pciaccess.h>
#include <linux/pci_regs.h>


/* Defines the context for the PCI access mechanism using the libpciaccess library */
typedef struct generic_pci_access_context_s
{
    /* libpciaccess doesn't require any context */
} libpciaccess_pci_access_context_t;


/* Defines an iterator for finding matching PCI devices */
typedef struct generic_pci_access_iterator_s
{
    /* Filter used to find matching PCI devices */
    struct pci_id_match match;
    /* The iterator used to find the matching PCI devices */
    struct pci_device_iterator *device_iterator;
} libpciaccess_pci_access_iterator_t;


/**
 * @brief Initialise the PCI access mechanism using libpciaccess
 * @return The initialised context used to perform PCI access
 */
generic_pci_access_context_p generic_pci_access_initialise (void)
{
    int rc;
    libpciaccess_pci_access_context_t *const context = calloc (1, sizeof (*context));

    /* Initialise using defaults */
    rc = pci_system_init ();
    if (rc != 0)
    {
        fprintf (stderr, "pci_system_init failed\n");
        exit (EXIT_FAILURE);
    }

    return context;
}


/**
 * @brief Finalise the PCI access mechanism, freeing the resources
 * @param[in/out] context The context to finalise. Can't be used after this function returns.
 */
void generic_pci_access_finalise (generic_pci_access_context_p const context)
{
    pci_system_cleanup ();
    free (context);
}


/**
 * @brief Create an iterator to find PCI devices matching a filter
 * @param[in/out] context The context to use for the iterator.
 * @param[in] filter
 */
generic_pci_access_iterator_p generic_pci_access_iterator_create (generic_pci_access_context_p const context,
                                                                  const generic_pci_access_filter_t *const filter)
{
    libpciaccess_pci_access_iterator_t *const iterator = calloc (1, sizeof (*iterator));

    /* Initialise the match filter used by libpciaccess */
    iterator->match.vendor_id = (filter->vendor_id != GENERIC_PCI_MATCH_ANY) ? filter->vendor_id : PCI_MATCH_ANY;
    iterator->match.device_id = (filter->device_id != GENERIC_PCI_MATCH_ANY) ? filter->device_id : PCI_MATCH_ANY;
    iterator->match.subvendor_id = PCI_MATCH_ANY;
    iterator->match.subdevice_id = PCI_MATCH_ANY;
    iterator->match.device_class = 0;
    iterator->match.device_class_mask = 0;
    iterator->match.match_data = 0;

    /* Create the iterator */
    iterator->device_iterator = pci_id_match_iterator_create (&iterator->match);

    return iterator;
}


/**
 * @brief Probe a device to obtain information for used in subsequent operations which query the device
 * @param[in/out] device The device to probe. Set to a NULL pointer if the probe fails, to prevent use
 */
static void probe_before_use (struct pci_device **const device)
{
    int rc;
    int saved_errno;

    if (*device != NULL)
    {
        errno = 0;
        rc = pci_device_probe (*device);
        saved_errno = errno;

        if (rc != 0)
        {
            printf ("pci_device_probe() failed with errno %s\n", strerror (saved_errno));
            *device = NULL;
        }
    }
}


/**
 * @brief Return the next matching device for an iterator
 * @param[in/out] iterator The iterator to return the next matching device
 * @return If non-NULL the next device which matches the filter used for the iterator.
 *         NULL means no more matching devices.
 */
generic_pci_access_device_p generic_pci_access_iterator_next (generic_pci_access_iterator_p const iterator)
{
    struct pci_device *device;

    device = pci_device_next (iterator->device_iterator);

    if (device != NULL)
    {
        probe_before_use (&device);
    }

    /* Return the pointer to the underlying libpciaccess device type, as avoids the need to perform our own memory management */
    return (generic_pci_access_device_p) device;
}


/**
 * @brief Destroy a device iterator, releasing its resources
 * @param[in/out] iterator The iterator to destroy
 */
void generic_pci_access_iterator_destroy (generic_pci_access_iterator_p const iterator)
{
    pci_iterator_destroy (iterator->device_iterator);
    free (iterator);
}


/**
 * @brief Obtain the parent bridge for a PCI device
 * @param[in] generic_device Which PCI device to obtain the parent bridge for
 * @return If non-NULL the parent bridge, or NULL if no parent bridge found.
 */
generic_pci_access_device_p generic_pci_access_get_parent_bridge (generic_pci_access_device_p const generic_device)
{
    struct pci_device *const device = (struct pci_device *) generic_device;
    struct pci_device *parent_bridge = NULL;

    if (device != NULL)
    {
        parent_bridge = pci_device_get_parent_bridge (device);
        probe_before_use (&parent_bridge);
    }

    return (generic_pci_access_device_p) parent_bridge;
}


/**
 * @brief Read a 8-bit, 16-bit or 32-bit configuration value for a device
 * @details The read will fail for PCIe capabilities unless the process has CAP_SYS_ADMIN capability.
 * @param[in/out] generic_device The device to perform the configuration read for.
 * @param[in] offset The byte offset for the configuration value to read
 * @param[out] value The configuration value read.
 *                   Will be all-ones if the configuration read fails
 * @return Returns true if the configuration value was read.
 */
bool generic_pci_access_cfg_read_u8 (generic_pci_access_device_p const generic_device,
                                     const uint32_t offset, uint8_t *const value)
{
    bool success;
    struct pci_device *const device = (struct pci_device *) generic_device;
    int rc;

    rc = pci_device_cfg_read_u8 (device, value, offset);
    success = rc == 0;
    if (!success)
    {
        *value = UINT8_MAX;
    }

    return success;
}

bool generic_pci_access_cfg_read_u16 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint16_t *const value)
{
    bool success;
    struct pci_device *const device = (struct pci_device *) generic_device;
    int rc;

    rc = pci_device_cfg_read_u16 (device, value, offset);
    success = rc == 0;
    if (!success)
    {
        *value = UINT16_MAX;
    }

    return success;
}

bool generic_pci_access_cfg_read_u32 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint32_t *const value)
{
    bool success;
    struct pci_device *const device = (struct pci_device *) generic_device;
    int rc;

    rc = pci_device_cfg_read_u32 (device, value, offset);
    success = rc == 0;
    if (!success)
    {
        *value = UINT32_MAX;
    }

    return success;
}


/**
 * @brief Get an unsigned integer property for a device
 * @param[in/out] generic_device Which device to get the property for
 * @param[in] property The property value to get
 * @param[out] value The property value
 * @return True if the property is available
 */
bool generic_pci_access_uint_property (generic_pci_access_device_p const generic_device,
                                       const generic_pci_access_device_uint_property_t property, uint32_t *const value)
{
    struct pci_device *const device = (struct pci_device *) generic_device;
    uint8_t cfg_u8;
    uint8_t header_type;
    bool available = false;

    *value = UINT32_MAX;

    switch (property)
    {
    case GENERIC_PCI_ACCESS_DOMAIN:
        available = true;
        *value = (uint32_t) device->domain;
        break;

    case GENERIC_PCI_ACCESS_BUS:
        available = true;
        *value = device->bus;
        break;

    case GENERIC_PCI_ACCESS_DEV:
        available = true;
        *value = device->dev;
        break;

    case GENERIC_PCI_ACCESS_FUNC:
        available = true;
        *value = device->func;
        break;

    case GENERIC_PCI_ACCESS_VENDOR_ID:
        available = true;
        *value = device->vendor_id;
        break;

    case GENERIC_PCI_ACCESS_DEVICE_ID:
        available = true;
        *value = device->device_id;
        break;

    case GENERIC_PCI_ACCESS_SUBVENDOR_ID:
        if (generic_pci_access_cfg_read_u8 (generic_device, PCI_HEADER_TYPE, &cfg_u8))
        {
            header_type = cfg_u8 & PCI_HEADER_TYPE_MASK;
            if (header_type == PCI_HEADER_TYPE_NORMAL)
            {
                available = true;
                *value = device->subvendor_id;
            }
        }
        break;

    case GENERIC_PCI_ACCESS_SUBDEVICE_ID:
        if (generic_pci_access_cfg_read_u8 (generic_device, PCI_HEADER_TYPE, &cfg_u8))
        {
            header_type = cfg_u8 & PCI_HEADER_TYPE_MASK;
            if (header_type == PCI_HEADER_TYPE_NORMAL)
            {
                available = true;
                *value = device->subdevice_id;
            }
        }
        break;
    }

    return available;
}


/**
 * @brief Get a text property for a device
 * @param[in/out] generic_device Which device to get the property for
 * @param[in] property The text property to get
 * @return The text property, or NULL if not available
 */
const char *generic_pci_access_text_property (generic_pci_access_device_p const generic_device,
                                              const generic_pci_access_device_text_property_t property)
{
    struct pci_device *const device = (struct pci_device *) generic_device;
    const char *property_text = NULL;

    switch (property)
    {
    case GENERIC_PCI_ACCESS_VENDOR_NAME:
        property_text = pci_device_get_vendor_name (device);
        break;

    case GENERIC_PCI_ACCESS_DEVICE_NAME:
        property_text = pci_device_get_device_name (device);
        break;

    case GENERIC_PCI_ACCESS_IOMMU_GROUP:
        property_text = pci_sysfs_read_device_symlink_name (device->domain, device->bus, device->dev, device->func, "iommu_group");
        break;

    case GENERIC_PCI_ACCESS_DRIVER:
        property_text = pci_sysfs_read_device_symlink_name (device->domain, device->bus, device->dev, device->func, "driver");
    }

    return property_text;
}


/**
 * @brief Get the BARs for a device
 * @param[in/out] generic_device Which device to get the BARs for
 * @param[out] regions The definition of each region for the BARs
 */
void generic_pci_access_get_bars (generic_pci_access_device_p const generic_device,
                                  generic_pci_access_mem_region_t *const regions)
{
    struct pci_device *const device = (struct pci_device *) generic_device;

    for (uint32_t region_index = 0; region_index < PCI_STD_NUM_BARS; region_index++)
    {
        const struct pci_mem_region *const device_region = &device->regions[region_index];
        generic_pci_access_mem_region_t *const region_out = &regions[region_index];

        region_out->size = device_region->size;
        region_out->base_address = device_region->base_addr;
        region_out->is_IO = device_region->is_IO;
        region_out->is_prefetchable = device_region->is_prefetchable;
        region_out->is_64 = device_region->is_64;
    }
}
