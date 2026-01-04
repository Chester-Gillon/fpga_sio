/*
 * @file generic_pci_access.h
 * @date 4 May 2024
 * @author Chester Gillon
 * @brief Defines an interface for a generic PCI access library.
 * @details
 *  Different implementations can be provided, which use a specific PCI access library.
 *  One executable which uses this interface links a different implementation.
 *  I.e. a link time selection, rather than run time selection, which avoids the need for a dispatching layer.
 */

#ifndef SOURCE_DUMP_INFO_GENERIC_PCI_ACCESS_H_
#define SOURCE_DUMP_INFO_GENERIC_PCI_ACCESS_H_

#include <stdint.h>
#include <stdbool.h>
#include <strings.h>


/* Used to match any value in a PCI filter */
#define GENERIC_PCI_MATCH_ANY UINT32_MAX


/* Opaque context to support PCI access.
 * The function which creates the content is provided in a library which is specific to PCI access mechanism */
typedef struct generic_pci_access_context_s *generic_pci_access_context_p;

/* Opaque iterator to search for PCI devices which match a filter */
typedef struct generic_pci_access_iterator_s *generic_pci_access_iterator_p;

/* Opaque PCI device which can be searched for using a filter */
typedef struct generic_pci_access_device_s *generic_pci_access_device_p;


/* The type of filter to match PCI devices */
typedef enum
{
    /* Match using the device identity */
    GENERIC_PCI_ACCESS_FILTER_ID,
    /* Match using the device location on the PCI bus */
    GENERIC_PCI_ACCESS_FILTER_LOCATION
} generic_pci_access_filter_type_t;


/* Filter to match PCI devices */
typedef struct
{
    /* How the filter is applied */
    generic_pci_access_filter_type_t filter_type;
    /* Used for GENERIC_PCI_ACCESS_FILTER_ID. Either field can be be GENERIC_PCI_MATCH_ANY */
    uint32_t vendor_id;
    uint32_t device_id;
    /* Used for GENERIC_PCI_ACCESS_FILTER_LOCATION */
    uint32_t domain;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
} generic_pci_access_filter_t;

/* The possible unsigned integer property values which can be obtained for a device */
typedef enum
{
    /* Available for all devices */
    GENERIC_PCI_ACCESS_DOMAIN,
    GENERIC_PCI_ACCESS_BUS,
    GENERIC_PCI_ACCESS_DEV,
    GENERIC_PCI_ACCESS_FUNC,
    GENERIC_PCI_ACCESS_VENDOR_ID,
    GENERIC_PCI_ACCESS_DEVICE_ID,
    GENERIC_PCI_ACCESS_REVISION_ID,

    /* Only available for endpoints (type 0 header aka "normal") */
    GENERIC_PCI_ACCESS_SUBVENDOR_ID,
    GENERIC_PCI_ACCESS_SUBDEVICE_ID
} generic_pci_access_device_uint_property_t;

/* The possible textual property values which can be obtained for a device */
typedef enum
{
    GENERIC_PCI_ACCESS_VENDOR_NAME,
    GENERIC_PCI_ACCESS_DEVICE_NAME,
    GENERIC_PCI_ACCESS_IOMMU_GROUP,
    GENERIC_PCI_ACCESS_DRIVER,
    GENERIC_PCI_ACCESS_PHYSICAL_SLOT,

    /* This obtains the single module from the driver.
     * Whereas lspci uses kmod_module_new_from_lookup() and other functions from libkmod to find all modules which have a match
     * for the module_alias */
    GENERIC_PCI_ACCESS_MODULE
} generic_pci_access_device_text_property_t;

/* One BAR description for a PCI device */
typedef struct
{
    /* The physical base address */
    uint64_t base_address;
    /* The size of the BAR in bytes. Zero if the BAR isn't defined */
    uint64_t size;
    /* When true IO, when false memory mapped */
    bool is_IO;
    /* When true, the memory is prefetchable */
    bool is_prefetchable;
    /* When true the memory uses 64-bit addressing, when false 32-bit addressing */
    bool is_64;
} generic_pci_access_mem_region_t;


generic_pci_access_context_p generic_pci_access_initialise (void);
void generic_pci_access_finalise (generic_pci_access_context_p const context);
generic_pci_access_iterator_p generic_pci_access_iterator_create (generic_pci_access_context_p const context,
                                                                  const generic_pci_access_filter_t *const filter);
generic_pci_access_device_p generic_pci_access_iterator_next (generic_pci_access_iterator_p const iterator);
void generic_pci_access_iterator_destroy (generic_pci_access_iterator_p const iterator);
generic_pci_access_device_p generic_pci_access_get_parent_bridge (generic_pci_access_device_p const generic_device);

bool generic_pci_access_cfg_read_u8 (generic_pci_access_device_p const generic_device,
                                     const uint32_t offset, uint8_t *const value);
bool generic_pci_access_cfg_read_u16 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint16_t *const value);
bool generic_pci_access_cfg_read_u32 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, uint32_t *const value);

bool generic_pci_access_cfg_write_u8 (generic_pci_access_device_p const generic_device,
                                      const uint32_t offset, const uint8_t value);
bool generic_pci_access_cfg_write_u16 (generic_pci_access_device_p const generic_device,
                                       const uint32_t offset, const uint16_t value);
bool generic_pci_access_cfg_write_u32 (generic_pci_access_device_p const generic_device,
                                       const uint32_t offset, const uint32_t value);

bool generic_pci_access_uint_property (generic_pci_access_device_p const generic_device,
                                       const generic_pci_access_device_uint_property_t property, uint32_t *const value);
const char *generic_pci_access_text_property (generic_pci_access_device_p const generic_device,
                                              const generic_pci_access_device_text_property_t property);

void generic_pci_access_get_bars (generic_pci_access_device_p const generic_device,
                                  generic_pci_access_mem_region_t *const regions);

#endif /* SOURCE_DUMP_INFO_GENERIC_PCI_ACCESS_H_ */
