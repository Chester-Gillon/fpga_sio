/*
 * @file display_physical_slots.c
 * @date 27 Dec 2022
 * @author Chester Gillon
 * @brief Using the generic PCI access mechanism display any PCI devices which have a physical slot specified.
 * @details
 *  Tries and reports either:
 *  a. The slot reported by sysfs for a device.
 *  b. The slot in connected bridges.
 *
 *  The vfio generic access mechanism can only operate on endpoints to which the vfio-pci driver is bound,
 *  so that version of the program will generate less output.
 */

#include <stdlib.h>
#include <stdio.h>

#include <pci/pci.h>

#include "generic_pci_access.h"


/**
 * @brief Read the PCIe capability pointer for a device
 * @param[in/out] device The device to read the capability pointer for
 * @param[out] capability_pointer The capability pointer read from the PCI configuration space of the device
 * @return Returns true when have read the capability pointer.
 */
static bool get_pcie_capability_pointer (generic_pci_access_device_p const device, uint8_t *const capability_pointer)
{
    bool been_hear[256] = {false};
    bool success;
    bool read_capability_pointer = false;
    uint16_t status_register;
    success = generic_pci_access_cfg_read_u16 (device, PCI_STATUS, &status_register);
    uint8_t capability_id;

    /* Check for presence of PCI capabilities */
    if (success && (status_register & PCI_STATUS_CAP_LIST) != 0)
    {
        /* Iterate over all capabilities. been_hear[] used as protection against infinite loops due to malformed capability lists */
        success = generic_pci_access_cfg_read_u8 (device, PCI_CAPABILITY_LIST, capability_pointer);
        while (success && (!read_capability_pointer) && (*capability_pointer != 0) && (!been_hear[*capability_pointer]))
        {
            success = generic_pci_access_cfg_read_u8 (device, *capability_pointer + PCI_CAP_LIST_ID, &capability_id);

            if (success)
            {
                if (capability_id == PCI_CAP_ID_EXP)
                {
                    read_capability_pointer = true;
                }
                else
                {
                    /* Advance to next capability */
                    been_hear[*capability_pointer] = true;
                    success = generic_pci_access_cfg_read_u8 (device, *capability_pointer + PCI_CAP_LIST_NEXT, capability_pointer);
                }
            }
        }
    }

    return read_capability_pointer;
}


/**
 * @brief Attempt to get the physical slot for a device, by searching for connected bridges.
 * @param[in/out] device The device to attempt to get the physical slot for.
 * @param[out] physical_slot_number The slot number read from a connected bridge.
 * @return Returns true if got the physical slot, or false otherwise.
 */
static bool get_physical_slot_from_bridge (generic_pci_access_device_p const device, uint32_t *const physical_slot_number)
{
    generic_pci_access_device_p candidate_bridge = device;
    bool got_physical_slot_from_bridge = false;
    uint8_t capability_pointer;
    uint16_t flags;
    uint32_t slot_capabilities;

    *physical_slot_number = 0;
    while ((!got_physical_slot_from_bridge) && (candidate_bridge != NULL))
    {
        if (get_pcie_capability_pointer (candidate_bridge, &capability_pointer))
        {
            if (generic_pci_access_cfg_read_u16 (candidate_bridge, capability_pointer + PCI_EXP_FLAGS, &flags) &&
                generic_pci_access_cfg_read_u32 (candidate_bridge, capability_pointer + PCI_EXP_SLTCAP, &slot_capabilities))
            {
                const bool slot_implemented = (flags & PCI_EXP_FLAGS_SLOT) != 0;

                if (slot_implemented)
                {
                    *physical_slot_number = generic_pci_access_extract_field (slot_capabilities, PCI_EXP_SLTCAP_PSN);
                    if (*physical_slot_number != 0)
                    {
                        got_physical_slot_from_bridge = true;
                    }
                }
            }
        }

        if (!got_physical_slot_from_bridge)
        {
            candidate_bridge = generic_pci_access_get_parent_bridge (candidate_bridge);
        }
    }

    return got_physical_slot_from_bridge;
}


int main (int argc, char *argv[])
{
    generic_pci_access_device_p device;
    uint32_t domain;
    uint32_t bus;
    uint32_t dev;
    uint32_t func;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t subvendor_id;
    uint32_t subdevice_id;
    uint32_t revision_id;
    uint32_t physical_slot_number;

    const generic_pci_access_filter_t any_device =
    {
        .filter_type = GENERIC_PCI_ACCESS_FILTER_ID,
        .vendor_id = GENERIC_PCI_MATCH_ANY,
        .device_id = GENERIC_PCI_MATCH_ANY
    };

    generic_pci_access_context_p const access_context = generic_pci_access_initialise ();
    generic_pci_access_iterator_p const device_iterator = generic_pci_access_iterator_create (access_context, &any_device);

    /* Iterate over all devices, displaying those which have a physical slot specified */
    device = generic_pci_access_iterator_next (device_iterator);
    while (device != NULL)
    {
        if (generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_DOMAIN, &domain) &&
            generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_BUS, &bus) &&
            generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_DEV, &dev) &&
            generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_FUNC, &func) &&
            generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_VENDOR_ID, &vendor_id) &&
            generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_DEVICE_ID, &device_id) &&
            generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_REVISION_ID, &revision_id))
        {
            const char *const physical_slot_from_sysfs = generic_pci_access_text_property (device, GENERIC_PCI_ACCESS_PHYSICAL_SLOT);
            const bool got_physical_slot_from_bridge = get_physical_slot_from_bridge (device, &physical_slot_number);

            if ((physical_slot_from_sysfs != NULL) || got_physical_slot_from_bridge)
            {
                printf ("domain=%04x bus=%02x dev=%02x func=%02x rev=%02x\n",
                        domain, bus, dev, func, revision_id);
                printf ("  vendor_id=%04x (%s) device_id=%04x (%s)",
                        vendor_id, generic_pci_access_text_property (device, GENERIC_PCI_ACCESS_VENDOR_NAME),
                        device_id, generic_pci_access_text_property (device, GENERIC_PCI_ACCESS_DEVICE_NAME));
                if (generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_SUBVENDOR_ID, &subvendor_id) &&
                    generic_pci_access_uint_property (device, GENERIC_PCI_ACCESS_SUBDEVICE_ID, &subdevice_id))
                {
                    /* Only defined for normal header type (i.e. not for a bridge) */
                    printf (" subvendor_id=%04x subdevice_id=%04x",
                            subvendor_id, subdevice_id);
                }
                printf ("\n");
                if (physical_slot_from_sysfs != NULL)
                {
                    printf ("    physical slot from sysfs: %s\n", physical_slot_from_sysfs);
                }
                if (got_physical_slot_from_bridge)
                {
                    printf ("    physical slot from bridge: #%u\n", physical_slot_number);
                }

                printf ("\n");
            }
        }

        device = generic_pci_access_iterator_next (device_iterator);
    }

    generic_pci_access_finalise (access_context);

    return EXIT_SUCCESS;
}
