/*
 * @file display_physical_slots.c
 * @date 27 Dec 2022
 * @author Chester Gillon
 * @brief Using pciutils display any PCI devices which have a physical slot specified.
 */

#include <stdlib.h>
#include <stdio.h>

#include <pci/pci.h>

#include <pciaccess.h>

int main (int argc, char *argv[])
{
    struct pci_access *pacc;
    struct pci_dev *dev;
    int known_fields;
    char vendor_name[256];
    char device_name[256];

    /* Initialise using the defaults */
    pacc = pci_alloc ();
    if (pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (pacc);
    printf ("Access method : %s\n", pci_get_method_name (pacc->method));

    /* Scan the entire bus */
    pci_scan_bus (pacc);

    /* Iterate over all devices, displaying those which have a physical slot specified */
    for (dev = pacc->devices; dev != NULL; dev = dev->next)
    {
        known_fields = pci_fill_info (dev, PCI_FILL_IDENT | PCI_FILL_PHYS_SLOT);
        if (((known_fields & PCI_FILL_PHYS_SLOT) != 0) && (dev->phy_slot != NULL))
        {
            printf ("domain=%04x bus=%02x dev=%02x func=%02x\n  vendor_id=%04x (%s) device_id=%04x (%s)\n",
                    dev->domain, dev->bus, dev->dev, dev->func,
                    dev->vendor_id,
                    pci_lookup_name (pacc, vendor_name, sizeof (vendor_name), PCI_LOOKUP_VENDOR, dev->vendor_id),
                    dev->device_id,
                    pci_lookup_name (pacc, device_name, sizeof (device_name), PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id));
            printf ("  physical slot: %s\n", dev->phy_slot);
        }
    }

    pci_cleanup (pacc);

    return EXIT_SUCCESS;
}
