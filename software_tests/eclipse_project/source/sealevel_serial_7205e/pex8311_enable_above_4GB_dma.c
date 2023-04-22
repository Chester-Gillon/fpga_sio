/*
 * @file pex8311_enable_above_4GB_dma.c
 * @date 13 Apr 2023
 * @author Chester Gillon
 * @brief Program to investigate changing the PEX8311 PECS_PREBASE Prefetchable Memory Base to enable 64-bit capability.
 * @details Was written to try the modification in a non-volatile way prior to try changing the EEPROM configuration.
 *          Requires that:
 *          a. The PEX8311 EEPROM configuration already enables the "PEX 8111 PCI Express-to-PCI Bridge" to memory map
 *             the PCES registers as linked in the header of time_pex8311_shared_memory_libpciaccess.c
 *          b. Secure boot is disabled to allow libpciaccess to map the bridge BAR as writable; since vfio-pci doesn't
 *             support being bound to PCI devices with a bridge header type.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <pciaccess.h>

#include "pex8311.h"


int main (int argc, char *argv[])
{
    int rc;
    uint16_t prefetchable_capability_initial_cfg;
    uint16_t prefetchable_capability_final_cfg;
    uint16_t prefetchable_capability_mm;

    /* The vendor and device ID of the "PEX 8111 PCI Express-to-PCI Bridge" */
    struct pci_id_match match =
    {
        .vendor_id = 0x10b5,
        .device_id = 0x8111,
        .subvendor_id = PCI_MATCH_ANY,
        .subdevice_id = PCI_MATCH_ANY,
        .device_class = 0,
        .device_class_mask = 0
    };

    rc = pci_system_init ();
    if (rc != 0)
    {
        fprintf (stderr, "pci_system_init failed\n");
        exit (EXIT_FAILURE);
    }

    /* Process any PLX devices found */
    struct pci_device_iterator *const device_iterator = pci_id_match_iterator_create (&match);
    struct pci_device *device;

    device = pci_device_next (device_iterator);
    while (device != NULL)
    {
        rc = pci_device_probe (device);
        if (rc == 0)
        {
            /* Read the capability from PCI configuration space, which doesn't need to map shared memory, to see if any
             * action is required. */
            rc = pci_device_cfg_read_u16 (device, &prefetchable_capability_initial_cfg, PEX_PECS_PREBASE);
            if (rc != 0)
            {
                printf ("pci_device_cfg_read_u16() failed\n");
                exit (EXIT_FAILURE);
            }

            if ((prefetchable_capability_initial_cfg & PEX_PECS_PREBASE_CAPABILITY_MASK) == PEX_PECS_PREBASE_CAPABILITY_64_BIT)
            {
                printf ("PEX8311 already reports 64-bit capability in PCI configuration space\n");
            }
            else if (device->regions[PEX8311_SHARED_MEMORY_BAR_INDEX].size < PEX8311_SHARED_MEMORY_START_OFFSET)
            {
                printf ("PEX8311 memory mapped BAR not present, unable to enable 64-bit capability\n");
            }
            else
            {
                /* Memory map to gain access to modify the capability */
                void *addr;

                rc = pci_device_map_range (device,
                        device->regions[PEX8311_SHARED_MEMORY_BAR_INDEX].base_addr, PEX8311_SHARED_MEMORY_START_OFFSET,
                        PCI_DEV_MAP_FLAG_WRITABLE, &addr);
                if (rc != 0)
                {
                    fprintf (stderr, "pci_device_map_region for PEX8311_SHARED_MEMORY_BAR_INDEX failed:\n%s", strerror (rc));
                    exit (EXIT_FAILURE);
                }

                /* Used memory mapped access to set 64-bit capability */
                uint8_t *const pcs = addr;
                prefetchable_capability_mm = read_reg16 (pcs, PEX_PECS_PREBASE);
                prefetchable_capability_mm &= (uint16_t) (~PEX_PECS_PREBASE_CAPABILITY_MASK);
                prefetchable_capability_mm |= PEX_PECS_PREBASE_CAPABILITY_64_BIT;
                write_reg16 (pcs, PEX_PECS_PREBASE, prefetchable_capability_mm);


                /* Unmap the BAR */
                rc = pci_device_unmap_range (device, addr, PEX8311_SHARED_MEMORY_START_OFFSET);
                if (rc != 0)
                {
                    fprintf (stderr, "pci_device_unmap_range failed:\n%s", strerror (rc));
                    exit (EXIT_FAILURE);
                }

                /* Readback the capability from PCI configuration space to check has taken effect */
                rc = pci_device_cfg_read_u16 (device, &prefetchable_capability_final_cfg, PEX_PECS_PREBASE);
                if (rc != 0)
                {
                    printf ("pci_device_cfg_read_u16() failed\n");
                    exit (EXIT_FAILURE);
                }

                if (prefetchable_capability_final_cfg == prefetchable_capability_mm)
                {
                    printf ("Enabled 64-bit capability by changing PECS_PREBASE 0x%04x -> 0x%04x\n",
                            prefetchable_capability_initial_cfg, prefetchable_capability_final_cfg);
                }
                else
                {
                    printf ("Failed to enable 64-bit capability. Expected PECS_PREBASE 0x%04x actual 0x%04x\n",
                            prefetchable_capability_mm, prefetchable_capability_final_cfg);
                }
            }
        }

        device = pci_device_next (device_iterator);
    }

    return EXIT_SUCCESS;
}
