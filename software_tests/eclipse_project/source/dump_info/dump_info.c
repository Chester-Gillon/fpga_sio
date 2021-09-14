/*
 * @file dump_info.c
 * @date Sep 5, 2021
 * @author Chester Gillon
 * @brief Simple test of using libpciaccess to dump information about a PCIe device
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include <pciaccess.h>


int main (int argc, char *argv[])
{
    int rc;

    rc = pci_system_init ();
    if (rc != 0)
    {
        fprintf (stderr, "pci_system_init failed\n");
        exit (EXIT_FAILURE);
    }

    struct pci_id_match match =
    {
        .vendor_id = 0x10ee, /* Xilinx */
        .device_id = PCI_MATCH_ANY,
        .subvendor_id = PCI_MATCH_ANY,
        .subdevice_id = PCI_MATCH_ANY,
        .device_class = 0,
        .device_class_mask = 0
    };

    struct pci_device_iterator *const device_iterator = pci_id_match_iterator_create (&match);
    struct pci_device *device;

    device = pci_device_next (device_iterator);
    while (device != NULL)
    {
        rc = pci_device_probe (device);
        if (rc == 0)
        {
            printf ("domain=%04x bus=%02x dev=%02x func=%02x\n  vendor_id=%04x (%s) device_id=%04x (%s) subvendor_id=%04x subdevice_id=%04x\n",
                    device->domain, device->bus, device->dev, device->func,
                    device->vendor_id, pci_device_get_vendor_name (device),
                    device->device_id, pci_device_get_device_name (device),
                    device->subvendor_id, device->subdevice_id);
            for (unsigned bar_index = 0; bar_index < 6; bar_index++)
            {
                const struct pci_mem_region *const region = &device->regions[bar_index];

                if (region->size > 0)
                {
                    printf ("  bar[%u] base_addr=%" PRIx64 " size=%" PRIx64 " is_IO=%u is_prefetchable=%u is_64=%u\n",
                            bar_index, region->base_addr, region->size, region->is_IO, region->is_prefetchable, region->is_64);
                }
            }
        }
        device = pci_device_next (device_iterator);
    }

    return EXIT_SUCCESS;
}
