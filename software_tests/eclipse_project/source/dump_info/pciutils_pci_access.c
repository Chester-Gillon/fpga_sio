/*
 * @file pciutils_pci_access.c
 * @date 4 May 2024
 * @author Chester Gillon
 * @brief Implements an instantiation of generic_pci_access using the pciutils library
 */

#include "generic_pci_access.h"
#include "pci_sysfs_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <pci/pci.h>
#include <linux/pci_regs.h>

/* Defines the context for the PCI access mechanism using the pciutils library */
typedef struct generic_pci_access_context_s
{
    /* PCI access mechanism */
    struct pci_access *pacc;
} pciutils_pci_access_context_t;


/* Defines an iterator for finding matching PCI devices */
typedef struct generic_pci_access_iterator_s
{
    /* Reference to PCI access mechanism */
    pciutils_pci_access_context_t *context;
    /* Filter used to find matching PCI devices */
    struct pci_filter filter;
    /* The current device when iterating for matches */
    struct pci_dev *current_device;
} pciutils_pci_access_iterator_t;


/**
 * @brief Initialise the PCI access mechanism using pciutils
 * @return The initialised context used to perform PCI access
 */
generic_pci_access_context_p generic_pci_access_initialise (void)
{
    pciutils_pci_access_context_t *const context = calloc (1, sizeof (*context));

    /* Initialise using defaults */
    context->pacc = pci_alloc ();
    if (context->pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (context->pacc);

    /* Scan the entire bus */
    pci_scan_bus (context->pacc);

    return context;
}


/**
 * @brief Finalise the PCI access mechanism, freeing the resources
 * @param[in/out] context The context to finalise. Can't be used after this function returns.
 */
void generic_pci_access_finalise (generic_pci_access_context_p const context)
{
    pci_cleanup (context->pacc);
    free (context);
}


/**
 * @brief Create an iterator to find PCI devices matching a filter
 * @param[in/out] context The context to use for the iterator.
 * @param[in] filter The filter used to find matching PCI devices
 */
generic_pci_access_iterator_p generic_pci_access_iterator_create (generic_pci_access_context_p const context,
                                                                  const generic_pci_access_filter_t *const filter)
{
    pciutils_pci_access_iterator_t *const iterator = calloc (1, sizeof (*iterator));

    iterator->context = context;

    /* Initialise the filter used by pciutils */
    pci_filter_init (iterator->context->pacc, &iterator->filter);
    switch (filter->filter_type)
    {
    case GENERIC_PCI_ACCESS_FILTER_ID:
        if (filter->vendor_id != GENERIC_PCI_MATCH_ANY)
        {
            iterator->filter.vendor = (int) filter->vendor_id;
        }
        if (filter->vendor_id != GENERIC_PCI_MATCH_ANY)
        {
            iterator->filter.device = (int) filter->device_id;
        }
        break;

    case GENERIC_PCI_ACCESS_FILTER_LOCATION:
        iterator->filter.domain = (int) filter->domain;
        iterator->filter.bus = filter->bus;
        iterator->filter.slot = filter->dev;
        iterator->filter.func = filter->func;
        break;
    }

    /* Reset to the first device */
    iterator->current_device = context->pacc->devices;

    return iterator;
}


/**
 * @brief Return the next matching device for an iterator
 * @param[in/out] iterator The iterator to return the next matching device
 * @return If non-NULL the next device which matches the filter used for the iterator.
 *         NULL means no more matching devices.
 */
generic_pci_access_device_p generic_pci_access_iterator_next (generic_pci_access_iterator_p const iterator)
{
    generic_pci_access_device_p matching_device = NULL;

    while ((iterator->current_device != NULL) && (matching_device == NULL))
    {
        if (pci_filter_match (&iterator->filter, iterator->current_device))
        {
            /* Return the pointer to the underlying pciutils device type, as avoids the need to perform our own memory management */
            matching_device = (generic_pci_access_device_p) iterator->current_device;
        }
        iterator->current_device = iterator->current_device->next;
    }

    return matching_device;
}


/**
 * @brief Destroy a device iterator, releasing its resources
 * @param[in/out] iterator The iterator to destroy
 */
void generic_pci_access_iterator_destroy (generic_pci_access_iterator_p const iterator)
{
    free (iterator);
}


/**
 * @brief Obtain the parent bridge for a PCI device
 * @param[in] generic_device Which PCI device to obtain the parent bridge for
 * @return If non-NULL the parent bridge, or NULL if no parent bridge found.
 */
generic_pci_access_device_p generic_pci_access_get_parent_bridge (generic_pci_access_device_p const generic_device)
{
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    struct pci_dev *parent_bridge = NULL;
    uint8_t cfg_u8;
    uint8_t secondary_bus;

    if ((device != NULL) && (device->access != NULL))
    {
        struct pci_dev *search_device = device->access->devices;

        while ((search_device != NULL) && (parent_bridge == NULL))
        {
            /* Search for a bridge which is in the same domain as the device, with its secondary bus the same as the bus
             * for the device.
             *
             * This code assumes all classes of bridges have the secondary bus number defined.
             * Whereas the pci_device_get_bridge_buses() function in libpciaccess has additional tests on the device class.
             * E.g. for a PCI-to-ISA bridge the secondary bus number isn't defined. */
            if (search_device->domain == device->domain)
            {
                if (generic_pci_access_cfg_read_u8 ((generic_pci_access_device_p) search_device, PCI_HEADER_TYPE, &cfg_u8))
                {
                    const uint8_t header_type = cfg_u8 & PCI_HEADER_TYPE_MASK;

                    if ((header_type == PCI_HEADER_TYPE_BRIDGE) &&
                         generic_pci_access_cfg_read_u8 ((generic_pci_access_device_p) search_device, PCI_SECONDARY_BUS,
                                 &secondary_bus) &&
                        (device->bus == secondary_bus))
                    {
                        parent_bridge = search_device;
                    }
                }
            }

            search_device = search_device->next;
        }
    }

    return (generic_pci_access_device_p) parent_bridge;
}


/**
 * @brief Perform a configuration read for a device
 * @details
 *   Have to use pci_read_block() for the configuration read since the pci_read_byte(), pci_read_word() and pci_read_long()
 *   functions don't return an error indication. See https://unix.stackexchange.com/a/758359/535730
 *
 *   The read will fail for PCIe capabilities unless the process has CAP_SYS_ADMIN capability.
 * @param[in/out] generic_device The device to perform the configuration read for.
 * @param[in] offset The byte offset for the configuration value to read
 * @param[out] value The configuration value read.
 *                   Will be all-ones if the configuration read fails.
 * @param[in] num_bytes The number of bytes to read
 * @return Returns true if the configuration value was read, or false if an error
 */
static bool generic_pci_access_cfg_read (generic_pci_access_device_p const generic_device,
                                         const uint32_t offset, void *const value, const size_t num_bytes)
{
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    int rc;
    bool success;

    rc = pci_read_block (device, (int) offset, value, (int) num_bytes);
    success = rc > 0;
    if (!success)
    {
        memset (value, 0xff, num_bytes);
    }

    return success;
}


/**
 * @brief Read a 8-bit, 16-bit or 32-bit configuration value for a device
 * @param[in/out] generic_device The device to perform the configuration read for.
 * @param[in] offset The byte offset for the configuration value to read
 * @param[out] value The configuration value read.
 *                   Will be all-ones if the configuration read fails
 * @return Returns true if the configuration value was read.
 */
bool generic_pci_access_cfg_read_u8 (generic_pci_access_device_p const generic_device,
                                     const uint32_t offset, uint8_t *const value)
{
    return generic_pci_access_cfg_read (generic_device, offset, value, sizeof (*value));
}

bool generic_pci_access_cfg_read_u16 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint16_t *const value)
{
    return generic_pci_access_cfg_read (generic_device, offset, value, sizeof (*value));
}

bool generic_pci_access_cfg_read_u32 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint32_t *const value)
{
    return generic_pci_access_cfg_read (generic_device, offset, value, sizeof (*value));
}


/**
 * @brief Write a 8-bit, 16-bit or 32-bit configuration value for a device
 * @param[in/out] generic_device The device to perform the configuration write for.
 * @param[in] offset The byte offset for the configuration value to write
 * @param[out] value The configuration value to write.
 * @return Returns true if the configuration value was written, or false if an error
 */
bool generic_pci_access_cfg_write_u8 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, const uint8_t value)
{
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    int rc;
    bool success;

    rc = pci_write_byte (device, (int) offset, value);
    success = rc > 0;

    return success;
}

bool generic_pci_access_cfg_write_u16 (generic_pci_access_device_p const generic_device,
                                       const uint32_t offset, const uint16_t value)
{
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    int rc;
    bool success;

    rc = pci_write_word (device, (int) offset, value);
    success = rc > 0;

    return success;
}

bool generic_pci_access_cfg_write_u32 (generic_pci_access_device_p const generic_device,
                                       const uint32_t offset, const uint32_t value)
{
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    int rc;
    bool success;

    rc = pci_write_long (device, (int) offset, value);
    success = rc > 0;

    return success;
}


/**
 * @brief Fill in a unsigned integer property value for a device
 * @param[in/out] device Which device to fill in the property value for
 * @param[in] flag A PCI_FILL_* flag which indicates which category of property value to fill in
 * @return True if the property is available
 */
static bool fill_uint_property (struct pci_dev *const device, const int flag)
{
    const int known_fields = pci_fill_info (device, flag);

    return (known_fields & flag) != 0;
}


/**
 * @brief Fill in a string property for a device
 * @param[in/out] device Which device to fill in the property value for
 * @param[in] flag A PCI_FILL_* flag which indicates which category of property value to fill in
 * @return The value of the property when non-NULL.
 *         NULL means the property is not available
 */
#ifdef HAVE_PCI_GET_STRING_PROPERTY
static const char *fill_string_property (struct pci_dev *const device, const int flag)
{
    const char *property_text = NULL;
    const int known_fields = pci_fill_info (device, flag);

    if ((known_fields & flag) != 0)
    {
        property_text = pci_get_string_property (device, (u32) flag);
    }

    return property_text;
}
#endif


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
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    uint8_t cfg_u8;
    uint16_t cfg_u16;
    uint32_t header_type;
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
        available = fill_uint_property (device, PCI_FILL_IDENT);
        *value = device->vendor_id;
        break;

    case GENERIC_PCI_ACCESS_DEVICE_ID:
        available = fill_uint_property (device, PCI_FILL_IDENT);
        *value = device->device_id;
        break;

    case GENERIC_PCI_ACCESS_REVISION_ID:
        available = generic_pci_access_cfg_read_u8 (generic_device, PCI_REVISION_ID, &cfg_u8);
        *value = cfg_u8;
        break;

    case GENERIC_PCI_ACCESS_SUBVENDOR_ID:
        if (generic_pci_access_cfg_read_u8 (generic_device, PCI_HEADER_TYPE, &cfg_u8))
        {
            header_type = cfg_u8 & PCI_HEADER_TYPE_MASK;
            if (header_type == PCI_HEADER_TYPE_NORMAL)
            {
                available = generic_pci_access_cfg_read_u16 (generic_device, PCI_SUBSYSTEM_VENDOR_ID, &cfg_u16);
                *value = cfg_u16;
            }
        }
        break;

    case GENERIC_PCI_ACCESS_SUBDEVICE_ID:
        if (generic_pci_access_cfg_read_u8 (generic_device, PCI_HEADER_TYPE, &cfg_u8))
        {
            header_type = cfg_u8 & PCI_HEADER_TYPE_MASK;
            if (header_type == PCI_HEADER_TYPE_NORMAL)
            {
                available = generic_pci_access_cfg_read_u16 (generic_device, PCI_SUBSYSTEM_ID, &cfg_u16);
                *value = cfg_u16;
            }
        }
        break;
    }

    return available;
}


/**
 * @brief Get a text property for a device
 * @details Since pci_lookup_name() requires a caller supplied buffer, for
 *          GENERIC_PCI_ACCESS_VENDOR_NAME and GENERIC_PCI_ACCESS_DEVICE_NAME this function has to dynamically allocate
 *          a buffer for the returned text. This is no way for the allocated buffer to be freed.
 * @param[in/out] generic_device Which device to get the property for
 * @param[in] property The text property to get
 * @return The text property, or NULL if not available
 */
const char *generic_pci_access_text_property (generic_pci_access_device_p const generic_device,
                                              const generic_pci_access_device_text_property_t property)
{
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    const char *property_text = NULL;
    char *name;
    const int max_name_len = 256;

    switch (property)
    {
    case GENERIC_PCI_ACCESS_VENDOR_NAME:
        name = calloc ((size_t) max_name_len, sizeof (char));
        if ((device->access != NULL) && (name != NULL) && fill_uint_property (device, PCI_FILL_IDENT))
        {
            property_text = pci_lookup_name (device->access, name, max_name_len, PCI_LOOKUP_VENDOR, device->vendor_id);
        }
        break;

    case GENERIC_PCI_ACCESS_DEVICE_NAME:
        name = calloc ((size_t) max_name_len, sizeof (char));
        if ((device->access != NULL) && (name != NULL) && fill_uint_property (device, PCI_FILL_IDENT))
        {
            property_text = pci_lookup_name (device->access, name, max_name_len,
                    PCI_LOOKUP_DEVICE, device->vendor_id, device->device_id);
        }
        break;

    case GENERIC_PCI_ACCESS_IOMMU_GROUP:
#ifdef PCI_FILL_IOMMU_GROUP
        property_text = fill_string_property (device, PCI_FILL_IOMMU_GROUP);
#else
        property_text = pci_sysfs_read_device_symlink_name ((uint32_t) device->domain, device->bus, device->dev, device->func, "iommu_group");
#endif
        break;

    case GENERIC_PCI_ACCESS_DRIVER:
#ifdef PCI_FILL_DRIVER
        property_text = fill_string_property (device, PCI_FILL_DRIVER);
#else
        property_text = pci_sysfs_read_device_symlink_name ((uint32_t) device->domain, device->bus, device->dev, device->func, "driver");
#endif
        break;

    case GENERIC_PCI_ACCESS_PHYSICAL_SLOT:
        if (fill_uint_property (device, PCI_FILL_PHYS_SLOT))
        {
            property_text = device->phy_slot;
        }
        break;
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
    struct pci_dev *const device = (struct pci_dev *) generic_device;
    const bool available = fill_uint_property (device, PCI_FILL_BASES);

    for (uint32_t region_index = 0; region_index < PCI_STD_NUM_BARS; region_index++)
    {
        generic_pci_access_mem_region_t *const region = &regions[region_index];

        memset (region, 0, sizeof (*region));
        if (available)
        {
            region->size = device->size[region_index];

            /* pciutils include flag bits in the base_addr */
            if (region->size > 0)
            {
                region->is_IO = (device->base_addr[region_index] & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO;
                region->is_prefetchable =
                        (!region->is_IO) && ((device->base_addr[region_index] & PCI_BASE_ADDRESS_MEM_PREFETCH) != 0);
                region->is_64 = (!region->is_IO) && ((device->base_addr[region_index] & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0);

                if (region->is_IO)
                {
                    region->base_address = device->base_addr[region_index] & PCI_BASE_ADDRESS_IO_MASK;
                }
                else
                {
                    region->base_address = device->base_addr[region_index] & PCI_BASE_ADDRESS_MEM_MASK;
                }
            }
        }
    }
}
