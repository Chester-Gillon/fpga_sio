/*
 * @file vfio_access_keep_open.c
 * @date 8 Aug 2026
 * @author Chester Gillon
 * @brief Command line utility to keep VFIO devices pen
 * @details
 *   By default just opens all VFIO devices, and keeps them open until user presses return.
 *   Only command line options are optional PCIe device filters, to select a sub-set of VFIO devices to keep open.
 *
 *   This program doesn't access any of the registers in the opened devices.
 */

#include "vfio_access.h"
#include "pci_sysfs_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    if (argc > 1)
    {
        /* Process any optional device filter arguments */
        for (int arg_index = 1; arg_index < argc; arg_index++)
        {
            vfio_add_pci_device_location_filter (argv[arg_index]);
        }
    }
    else
    {
        /* With no arguments, set location filters for all PCI devices which have the vfio driver loaded.
         * That avoids the need for this program to have a list of device identities to operate on. */
        struct pci_access *pacc;
        struct pci_dev *dev;
        int known_fields;
        char device_name[64];
        const int required_fields = PCI_FILL_IDENT;

        /* Initialise PCI access using the defaults */
        pacc = pci_alloc ();
        if (pacc == NULL)
        {
            fprintf (stderr, "pci_alloc() failed\n");
            exit (EXIT_FAILURE);
        }
        pci_init (pacc);

        /* Scan the entire bus */
        pci_scan_bus (pacc);

        for (dev = pacc->devices; dev != NULL; dev = dev->next)
        {
            known_fields = pci_fill_info (dev, required_fields);
            if ((known_fields & required_fields) == required_fields)
            {
                char *const driver_name =
                        pci_sysfs_read_device_symlink_name ((uint32_t) dev->domain, dev->bus, dev->dev, dev->func, "driver");

                if (driver_name != NULL)
                {
                    if (strncmp (driver_name, "vfio", 4) == 0)
                    {
                        snprintf (device_name, sizeof (device_name), "%04x:%02x:%02x.%x", dev->domain, dev->bus, dev->dev, dev->func);
                        vfio_add_pci_device_location_filter (device_name);
                    }
                    free (driver_name);
                }
            }
        }

        pci_cleanup (pacc);
    }

    /* Open the VFIO devices selected by vfio_add_pci_device_location_filter() calls above.
     * This means the identity filter is any device. */
    const vfio_pci_device_identity_filter_t filter_any_id =
    {
        .vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
    };

    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter_any_id);

    if (vfio_devices.num_devices > 0)
    {
        printf ("%u VFIO devices opened. Press return to close the VFIO devices.\n", vfio_devices.num_devices);
        getchar ();
    }
    else
    {
        printf ("No VFIO devices opened\n");
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
