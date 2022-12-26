/*
 * @file dump_info_libpciaccess.c
 * @date Sep 5, 2021
 * @author Chester Gillon
 * @brief Simple test of using libpciaccess to dump information about a PCIe device
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include <pciaccess.h>

#include "fpga_sio_pci_ids.h"


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
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = PCI_MATCH_ANY,
        .subvendor_id = PCI_MATCH_ANY,
        .subdevice_id = PCI_MATCH_ANY,
        .device_class = 0,
        .device_class_mask = 0
    };

    struct pci_device_iterator *const device_iterator = pci_id_match_iterator_create (&match);
    struct pci_device *device;
    uint16_t cmd;

    device = pci_device_next (device_iterator);
    while (device != NULL)
    {
        rc = pci_device_probe (device);
        if (rc == 0)
        {
            rc = pci_device_cfg_read_u16 (device, &cmd, PCI_COMMAND);

            if (rc == 0)
            {
                printf ("domain=%04x bus=%02x dev=%02x func=%02x\n  vendor_id=%04x (%s) device_id=%04x (%s) subvendor_id=%04x subdevice_id=%04x\n",
                        device->domain, device->bus, device->dev, device->func,
                        device->vendor_id, pci_device_get_vendor_name (device),
                        device->device_id, pci_device_get_device_name (device),
                        device->subvendor_id, device->subdevice_id);
                printf ("  control: I/O%s Mem%s BusMaster%s\n",
                        (cmd & PCI_COMMAND_IO) ? "+" : "-",
                        (cmd & PCI_COMMAND_MEMORY) ? "+" : "-",
                        (cmd & PCI_COMMAND_MASTER) ? "+" : "-");
                for (unsigned bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
                {
                    const struct pci_mem_region *const region = &device->regions[bar_index];

                    if (region->size > 0)
                    {
                        printf ("  bar[%u] base_addr=%" PRIx64 " size=%" PRIx64 " is_IO=%u is_prefetchable=%u is_64=%u\n",
                                bar_index, region->base_addr, region->size, region->is_IO, region->is_prefetchable, region->is_64);
                    }
                }
            }
        }
        device = pci_device_next (device_iterator);
    }

    return EXIT_SUCCESS;
}
