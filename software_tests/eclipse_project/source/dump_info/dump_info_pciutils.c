/*
 * @file dump_info_pciutils.c
 * @date 26 Dec 2022
 * @author Chester Gillon
 * @brief Simple test of using pciutils to dump information about a PCIe device
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include <pci/pci.h>

#include "fpga_sio_pci_ids.h"

int main (int argc, char *argv[])
{
    struct pci_access *pacc;
    struct pci_filter filter;
    struct pci_dev *dev;
    int requested_fields;
    int known_fields;
    char vendor_name[256];
    char device_name[256];
    u16 subvendor_id;
    u16 subdevice_id;
    u16 command;
    int vendor;
    char junk;
    u8 header_type;
    char header_type_name[64];

    /* Initialise using the defaults */
    pacc = pci_alloc ();
    if (pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (pacc);
    printf ("Access method : %s\n", pci_get_method_name (pacc->method));

    /* Use an option command line argument to specify the vendor ID to display information for */
    vendor = FPGA_SIO_VENDOR_ID;
    if (argc > 1)
    {
        const char *const vendor_txt = argv[1];
        if (sscanf (vendor_txt, "%x%c", &vendor, &junk) != 1)
        {
            printf ("Error: Invalid hex vendor ID %s\n", vendor_txt);
        }
    }

    /* Select to filter by vendor only */
    pci_filter_init (pacc, &filter);
    filter.vendor = vendor;

    /* Scan the entire bus */
    pci_scan_bus (pacc);

    /* Iterate over all devices, reporting information on those which match the filter */
    for (dev = pacc->devices; dev != NULL; dev = dev->next)
    {
        if (pci_filter_match (&filter, dev))
        {
            requested_fields = PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_SIZES | PCI_FILL_PHYS_SLOT;
#ifdef PCI_FILL_IOMMU_GROUP
            requested_fields |= PCI_FILL_IOMMU_GROUP;
#endif
#ifdef PCI_FILL_DRIVER
            requested_fields |= PCI_FILL_DRIVER;
#endif
            known_fields = pci_fill_info (dev, requested_fields);
            if ((known_fields & PCI_FILL_IDENT) != 0)
            {
                subvendor_id = pci_read_word (dev, PCI_SUBSYSTEM_VENDOR_ID);
                subdevice_id = pci_read_word (dev, PCI_SUBSYSTEM_ID);
                header_type = pci_read_byte (dev, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK;
                switch (header_type)
                {
                case PCI_HEADER_TYPE_NORMAL:
                    snprintf (header_type_name, sizeof (header_type_name), "%s", "NORMAL");
                    break;
                case PCI_HEADER_TYPE_BRIDGE:
                    snprintf (header_type_name, sizeof (header_type_name), "%s", "BRIDGE");
                    break;
                case PCI_HEADER_TYPE_CARDBUS:
                    snprintf (header_type_name, sizeof (header_type_name), "%s", "CARDBUS");
                    break;
                default:
                    snprintf (header_type_name, sizeof (header_type_name), "Unknown (0x%x)", header_type);
                }
                printf ("domain=%04x bus=%02x dev=%02x func=%02x\n  vendor_id=%04x (%s) device_id=%04x (%s) subvendor_id=%04x subdevice_id=%04x header_type=%s\n",
                        dev->domain, dev->bus, dev->dev, dev->func,
                        dev->vendor_id,
                        pci_lookup_name (pacc, vendor_name, sizeof (vendor_name), PCI_LOOKUP_VENDOR, dev->vendor_id),
                        dev->device_id,
                        pci_lookup_name (pacc, device_name, sizeof (device_name), PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id),
                        subvendor_id, subdevice_id, header_type_name);

                command = pci_read_word (dev, PCI_COMMAND);
                printf ("  control: I/O%s Mem%s BusMaster%s\n",
                        (command & PCI_COMMAND_IO) ? "+" : "-",
                        (command & PCI_COMMAND_MEMORY) ? "+" : "-",
                        (command & PCI_COMMAND_MASTER) ? "+" : "-");

                if (((known_fields & PCI_FILL_PHYS_SLOT) != 0) && (dev->phy_slot != NULL))
                {
                    printf ("  physical slot: %s\n", dev->phy_slot);
                }

#ifdef PCI_FILL_IOMMU_GROUP
                char *iommu_group;
                if ((known_fields & PCI_FILL_IOMMU_GROUP) != 0)
                {
                    iommu_group = pci_get_string_property (dev, PCI_FILL_IOMMU_GROUP);
                    if (iommu_group != NULL)
                    {
                        printf ("  IOMMU group: %s\n", iommu_group);
                    }
                }
#endif

#ifdef PCI_FILL_DRIVER
                if ((known_fields & PCI_FILL_DRIVER) != 0)
                {
                    iommu_group = pci_get_string_property (dev, PCI_FILL_DRIVER);
                    if (iommu_group != NULL)
                    {
                        printf ("  Driver: %s\n", iommu_group);
                    }
                }
#endif

                if (((known_fields & PCI_FILL_BASES) != 0) && ((known_fields & PCI_FILL_SIZES) != 0))
                {
                    for (unsigned bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
                    {
                        if (dev->size[bar_index] > 0)
                        {
                            const pciaddr_t raw_base_addr = dev->base_addr[bar_index];
                            const u32 is_IO = (raw_base_addr & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO;
                            const u32 is_prefetchable = (!is_IO) && ((raw_base_addr & PCI_BASE_ADDRESS_MEM_PREFETCH) != 0);
                            const u32 is_64 = (!is_IO) && ((raw_base_addr & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0);
                            const pciaddr_t base_addr = is_IO ?
                                    (raw_base_addr & PCI_BASE_ADDRESS_IO_MASK) : (raw_base_addr & PCI_BASE_ADDRESS_MEM_MASK);

                            printf ("  bar[%u] base_addr=%" PRIx64 " size=%" PRIx64 " is_IO=%u is_prefetchable=%u is_64=%u\n",
                                    bar_index, base_addr, dev->size[bar_index], is_IO, is_prefetchable, is_64);
                        }
                    }
                }
            }
        }
    }

    pci_cleanup (pacc);

    return EXIT_SUCCESS;
}
