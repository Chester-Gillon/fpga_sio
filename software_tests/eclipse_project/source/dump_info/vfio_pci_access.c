/*
 * @file vfio_pci_access.c
 * @date 2 Jun 2024
 * @author Chester Gillon
 * @brief Implements an instantiation of generic_pci_access using VFIO
 * @details
 *   The vfio_access library is used for VFIO access.
 *   As a result, only devices which have the vfio-pci driver bound are reported.
 */

#include "generic_pci_access.h"
#include "pci_sysfs_access.h"
#include "vfio_access.h"

#include <stdlib.h>
#include <string.h>


/* Defines the context for the PCI access mechanism using the vfio_access library */
typedef struct generic_pci_access_context_s
{
    /* There is no context for the overall access mechanism, since part of the iterator */
} vfio_pci_access_context_t;


/* Defines an iterator for finding matching PCI devices */
typedef struct generic_pci_access_iterator_s
{
    /* Filter used to find matching PCI devices */
    struct pci_filter filter;
    /* Contains the open VFIO devices which match the filter */
    vfio_devices_t vfio_devices;
    /* The number of opened devices in vfio_devices which have been returned by generic_pci_access_iterator_next().
     * This is used as an index to iterate through the devices and to be able to return the next one. */
    uint32_t num_devices_returned;
} vfio_pci_access_iterator_t;


/**
 * @brief Initialise the PCI access mechanism using vfio_access
 * @return The initialised context used to perform PCI access
 */
generic_pci_access_context_p generic_pci_access_initialise (void)
{
    vfio_pci_access_context_t *const context = calloc (1, sizeof (*context));

    return context;
}


/**
 * @brief Finalise the PCI access mechanism
 * @details The context has no resources which need to be freed.
 * @param[in/out] context The context to finalise. Can't be used after this function returns.
 */
void generic_pci_access_finalise (generic_pci_access_context_p const context)
{
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
    vfio_pci_access_iterator_t *const iterator = calloc (1, sizeof (*iterator));

    initialise_empty_vfio_devices (&iterator->vfio_devices);

    /* Initialise the filter used by pciutils */
    pci_filter_init (iterator->vfio_devices.pacc, &iterator->filter);
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

    /* Open devices matching the filter using VFIO */
    struct pci_dev *current_device = iterator->vfio_devices.pacc->devices;
    while (current_device != NULL)
    {
        if (pci_filter_match (&iterator->filter, current_device))
        {
            append_vfio_device (&iterator->vfio_devices, current_device, VFIO_DEVICE_DMA_CAPABILITY_NONE);
        }

        current_device = current_device->next;
    }

    /* Indicate the iterator hasn't returned any matching devices yet */
    iterator->num_devices_returned = 0;

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

    /* Return the next opened VFIO device.
     * Returns the pointer to to the underlying vfio_device_t type, as avoids the need to perform our own memory management. */
    if (iterator->num_devices_returned < iterator->vfio_devices.num_devices)
    {
        matching_device = (generic_pci_access_device_p) &iterator->vfio_devices.devices[iterator->num_devices_returned];
        iterator->num_devices_returned++;
    }

    return matching_device;
}

/**
 * @brief Destroy a device iterator, releasing its resources
 * @param[in/out] iterator The iterator to destroy
 */
void generic_pci_access_iterator_destroy (generic_pci_access_iterator_p const iterator)
{
    close_vfio_devices (&iterator->vfio_devices);
    free (iterator);
}


/**
 * @brief Obtain the parent bridge for a PCI device
 * @details
 *   This is a stub which always returns NULL, indicating has been unable to obtain the parent bridge.
 *   A stub is used since the vfio-pci driver can't be bound to a bridge.
 * @param[in] generic_device Which PCI device to obtain the parent bridge for
 * @return If non-NULL the parent bridge, or NULL if no parent bridge found.
 */
generic_pci_access_device_p generic_pci_access_get_parent_bridge (generic_pci_access_device_p const generic_device)
{
    return NULL;
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
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    return vfio_read_pci_config_u8 (vfio_device, offset, value);
}

bool generic_pci_access_cfg_read_u16 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint16_t *const value)
{
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    return vfio_read_pci_config_u16 (vfio_device, offset, value);
}

bool generic_pci_access_cfg_read_u32 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint32_t *const value)
{
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    return vfio_read_pci_config_u32 (vfio_device, offset, value);
}


/**
 * @brief Write a 8-bit, 16-bit or 32-bit configuration value for a device
 * @details The Kernel source file drivers/vfio/pci/vfio_pci_config.c may virtualise or deny write permission
 *          to some configuration fields.
 * @param[in/out] generic_device The device to perform the configuration write for.
 * @param[in] offset The byte offset for the configuration value to write
 * @param[out] value The configuration value to write.
 * @return Returns true if the configuration value was written, or false if an error
 */
bool generic_pci_access_cfg_write_u8 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, const uint8_t value)
{
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    return vfio_write_pci_config_u8 (vfio_device, offset, value);
}

bool generic_pci_access_cfg_write_u16 (generic_pci_access_device_p const generic_device,
                                       const uint32_t offset, const uint16_t value)
{
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    return vfio_write_pci_config_u16 (vfio_device, offset, value);
}

bool generic_pci_access_cfg_write_u32 (generic_pci_access_device_p const generic_device,
                                       const uint32_t offset, const uint32_t value)
{
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    return vfio_write_pci_config_u32 (vfio_device, offset, value);
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
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;
    bool available = false;

    *value = UINT32_MAX;

    switch (property)
    {
    case GENERIC_PCI_ACCESS_DOMAIN:
        available = true;
        *value = (uint32_t) vfio_device->pci_dev->domain;
        break;

    case GENERIC_PCI_ACCESS_BUS:
        available = true;
        *value = vfio_device->pci_dev->bus;
        break;

    case GENERIC_PCI_ACCESS_DEV:
        available = true;
        *value = vfio_device->pci_dev->dev;
        break;

    case GENERIC_PCI_ACCESS_FUNC:
        available = true;
        *value = vfio_device->pci_dev->func;
        break;

    case GENERIC_PCI_ACCESS_VENDOR_ID:
        available = true;
        *value = vfio_device->pci_dev->vendor_id;
        break;

    case GENERIC_PCI_ACCESS_DEVICE_ID:
        available = true;
        *value = vfio_device->pci_dev->device_id;
        break;

    case GENERIC_PCI_ACCESS_REVISION_ID:
        available = true;
        *value = vfio_device->pci_revision_id;
        break;

    case GENERIC_PCI_ACCESS_SUBVENDOR_ID:
        available = true;
        *value = vfio_device->pci_subsystem_vendor_id;
        break;

    case GENERIC_PCI_ACCESS_SUBDEVICE_ID:
        available = true;
        *value = vfio_device->pci_subsystem_device_id;
        break;
    }

    return available;
}


/**
 * @brief Fill in a string property for a device
 * @param[in/out] device Which device to fill in the property value for
 * @param[in] flag A PCI_FILL_* flag which indicates which category of property value to fill in
 * @return The value of the property when non-NULL.
 *         NULL means the property is not available
 */
#ifdef PCI_FILL_DRIVER
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
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;
    struct pci_access *const pacc = vfio_device->group->container->vfio_devices->pacc;
    const char *property_text = NULL;
    char *name;
    const int max_name_len = 256;

    switch (property)
    {
    case GENERIC_PCI_ACCESS_VENDOR_NAME:
        name = calloc ((size_t) max_name_len, sizeof (char));
        if ((pacc != NULL) && (name != NULL))
        {
            property_text = pci_lookup_name (pacc, name, max_name_len, PCI_LOOKUP_VENDOR, vfio_device->pci_dev->vendor_id);
        }
        break;

    case GENERIC_PCI_ACCESS_DEVICE_NAME:
        name = calloc ((size_t) max_name_len, sizeof (char));
        if ((pacc != NULL) && (name != NULL))
        {
            property_text = pci_lookup_name (pacc, name, max_name_len,
                    PCI_LOOKUP_DEVICE, vfio_device->pci_dev->vendor_id, vfio_device->pci_dev->device_id);
        }
        break;

    case GENERIC_PCI_ACCESS_IOMMU_GROUP:
        property_text = vfio_device->group->iommu_group_name;
        break;

    case GENERIC_PCI_ACCESS_DRIVER:
#ifdef PCI_FILL_DRIVER
        property_text = fill_string_property (vfio_device->pci_dev, PCI_FILL_DRIVER);
#else
        property_text = pci_sysfs_read_device_symlink_name ((uint32_t) vfio_device->pci_dev->domain, vfio_device->pci_dev->bus,
                vfio_device->pci_dev->dev, vfio_device->pci_dev->func, "driver");
#endif
        break;

    case GENERIC_PCI_ACCESS_PHYSICAL_SLOT:
        if (vfio_device->pci_physical_slot != NULL)
        {
            property_text = strdup (vfio_device->pci_physical_slot);
        }
        break;
    }

    return property_text;
}


/**
 * @brief Get the BARs for a device
 * @details Used VFIO, rather than pciutils, to obtain the BARs
 * @param[in/out] generic_device Which device to get the BARs for
 * @param[out] regions The definition of each region for the BARs
 */
void generic_pci_access_get_bars (generic_pci_access_device_p const generic_device,
                                  generic_pci_access_mem_region_t *const regions)
{
    vfio_device_t *const vfio_device = (vfio_device_t *) generic_device;

    uint32_t base_addr_lsw;
    uint32_t base_addr_msw;
    uint64_t raw_base_addr;

    for (uint32_t region_index = 0; region_index < PCI_STD_NUM_BARS; region_index++)
    {
        generic_pci_access_mem_region_t *const region = &regions[region_index];

        memset (region, 0, sizeof (*region));
        get_vfio_device_region (vfio_device, region_index);
        if (vfio_device->regions_info_populated[region_index])
        {
            region->size = vfio_device->regions_info[region_index].size;

            if (region->size > 0)
            {
                const uint32_t base_addr_lsw_offset = (uint32_t) (PCI_BASE_ADDRESS_0 + (region_index * sizeof (uint32_t)));

                if (generic_pci_access_cfg_read_u32 (generic_device, base_addr_lsw_offset, &base_addr_lsw))
                {
                    raw_base_addr = base_addr_lsw;
                    region->is_IO = (raw_base_addr & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO;
                    region->is_prefetchable = (!region->is_IO) && ((raw_base_addr & PCI_BASE_ADDRESS_MEM_PREFETCH) != 0);
                    region->is_64 = (!region->is_IO) && ((raw_base_addr & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0);

                    if (region->is_64 &&
                            generic_pci_access_cfg_read_u32 (generic_device, base_addr_lsw_offset + sizeof (uint32_t),
                                    &base_addr_msw))
                    {
                        raw_base_addr |= ((uint64_t) base_addr_msw) << 32;
                    }
                    region->base_address = region->is_IO ?
                            (raw_base_addr & PCI_BASE_ADDRESS_IO_MASK) : (raw_base_addr & PCI_BASE_ADDRESS_MEM_MASK);
                }
            }
        }
    }
}
