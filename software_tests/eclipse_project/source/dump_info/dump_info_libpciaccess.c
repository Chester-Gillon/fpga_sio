/*
 * @file dump_info_libpciaccess.c
 * @date Sep 5, 2021
 * @author Chester Gillon
 * @brief Test of using libpciaccess to dump information about a PCIe device, including some capabilities
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include <pciaccess.h>

#include "fpga_sio_pci_ids.h"


/**
 * @brief Display indentation at the start of a line of output, used to indicate a tree of PCI bridges.
 * @param[in] indent_level Amount of indentation at the start of each line of output
 */
static void display_indent (const uint32_t indent_level)
{
    for (uint32_t indent = 0; indent < indent_level; indent++)
    {
        printf (" ");
    }
}


/**
 * @brief Display PCI express capabilities, decoding the link capabilities and status.
 * @param[in] indent Amount of indentation at the start of each line of output
 * @param[in] device Device to read from
 * @param[in] capability_pointer Offset for the start of the capabilities to decode.
 * @return Returns zero on success or any other value to indicate a failure to read the capabilities
 */
static int display_pci_express_capabilities (const uint32_t indent_level, struct pci_device *const device,
                                             const uint8_t capability_pointer)
{
    int rc;
    uint16_t flags;
    uint32_t link_capabilities;
    uint16_t link_status;
    uint32_t link_capabilities2;
    uint32_t slot_capabilities;

    rc = pci_device_cfg_read_u16 (device, &flags, capability_pointer + PCI_EXP_FLAGS);
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u32 (device, &link_capabilities, capability_pointer + PCI_EXP_LNKCAP);
    }
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u16 (device, &link_status, capability_pointer + PCI_EXP_LNKSTA);
    }
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u32 (device, &link_capabilities2, capability_pointer + PCI_EXP_LNKCAP2);
    }
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u32 (device, &slot_capabilities, capability_pointer + PCI_EXP_SLTCAP);
    }

    if (rc == 0)
    {
        const uint32_t capability_version = flags & PCI_EXP_FLAGS_VERS;
        const uint32_t device_port_type = (flags & PCI_EXP_FLAGS_TYPE) >> 4;
        const uint32_t interrupt_message_number = (flags & PCI_EXP_FLAGS_IRQ) >> 9;
        const bool slot_implemented = (flags & PCI_EXP_FLAGS_SLOT) != 0;

        const uint32_t max_link_speed = link_capabilities & PCI_EXP_LNKCAP_SPEED;
        const uint32_t max_link_width = (link_capabilities & PCI_EXP_LNKCAP_WIDTH) >> 4;

        const uint32_t negotiated_link_speed = link_status & PCI_EXP_LNKSTA_SPEED;
        const uint32_t negotiated_link_width = (link_status & PCI_EXP_LNKSTA_WIDTH) >> 4;

        const uint32_t supported_link_speeds = PCI_EXP_LNKCAP2_SPEED (link_capabilities2);

        const uint32_t slot_power_limit_value = (slot_capabilities & PCI_EXP_SLTCAP_SPLV) >> 7;
        const uint32_t slot_power_limit_scale = (slot_capabilities & PCI_EXP_SLTCAP_SPLS) >> 15;
        const uint32_t physical_slot_number = (slot_capabilities & PCI_EXP_SLTCAP_PSN) >> 19;

        const char *const device_port_type_names[] =
        {
            [PCI_EXP_TYPE_ENDPOINT   ] = "Express Endpoint",
            [PCI_EXP_TYPE_LEG_END    ] = "Legacy Endpoint",
            [PCI_EXP_TYPE_ROOT_PORT  ] = "Root Port",
            [PCI_EXP_TYPE_UPSTREAM   ] = "Upstream Port",
            [PCI_EXP_TYPE_DOWNSTREAM ] = "Downstream Port",
            [PCI_EXP_TYPE_PCI_BRIDGE ] = "PCIe to PCI/PCI-X Bridge",
            [PCI_EXP_TYPE_PCIE_BRIDGE] = "PCI/PCI-X to PCIe Bridge",
            [PCI_EXP_TYPE_ROOT_INT_EP] = "Root Complex Integrated Endpoint",
            [PCI_EXP_TYPE_ROOT_EC    ] = "Root Complex Event Collector"
        };
        const size_t device_port_type_names_array_size = sizeof (device_port_type_names) / sizeof (device_port_type_names[0]);

        const char *const link_speed_names[] =
        {
            [1] = "2.5 GT/s",
            [2] = "5.0 GT/s",
            [3] = "8.0 GT/s",
            [4] = "16.0 GT/s"
        };
        const size_t link_speed_names_array_size = sizeof (link_speed_names) / sizeof (link_speed_names[0]);

        const double slot_power_limit_scales[] =
        {
            1.0,
            0.1,
            0.01,
            0.001
        };

        /* Continuation of the capability identification line from the caller */
        printf (" v%u", capability_version);
        if ((device_port_type < device_port_type_names_array_size) && (device_port_type_names[device_port_type] != NULL))
        {
            printf (" %s", device_port_type_names[device_port_type]);
        }
        else
        {
            printf (" device port type 0x%x", device_port_type);
        }
        printf (", MSI %u\n", interrupt_message_number);

        /* Display link capabilities */
        display_indent (indent_level);
        printf ("    Link capabilities: Max speed ");
        if ((max_link_speed < link_speed_names_array_size) && (link_speed_names[max_link_speed] != NULL))
        {
            printf ("%s", link_speed_names[max_link_speed]);
        }
        else
        {
            printf (" Unknown encoding 0x%x", max_link_speed);
        }
        printf (" Max width x%u\n", max_link_width);

        /* Display negotiated link status */
        display_indent (indent_level);
        printf ("    Negotiated link status: Current speed ");
        if ((max_link_speed < link_speed_names_array_size) && (link_speed_names[negotiated_link_speed] != NULL))
        {
            printf ("%s", link_speed_names[negotiated_link_speed]);
        }
        else
        {
            printf (" Unknown encoding 0x%x", negotiated_link_speed);
        }
        printf (" Width x%u\n", negotiated_link_width);

        /* Display supported link speeds */
        display_indent (indent_level);
        printf ("    Link capabilities2: ");
        if (link_capabilities2 != 0)
        {
            printf ("Supported link speeds");
            if ((supported_link_speeds & 0x1) != 0)
            {
                printf (" 2.5 GT/s");
            }
            if ((supported_link_speeds & 0x2) != 0)
            {
                printf (" 5.0 GT/s");
            }
            if ((supported_link_speeds & 0x4) != 0)
            {
                printf (" 8.0 GT/s");
            }
            if ((supported_link_speeds & 0x8) != 0)
            {
                printf (" 16.0 GT/s");
            }
        }
        else
        {
            printf ("Not implemented");
        }
        printf ("\n");

        /* Display slot capabilities */
        if (slot_implemented)
        {
            double slot_power_limit = (double) slot_power_limit_value * slot_power_limit_scales[slot_power_limit_scale];

            if ((slot_power_limit_scale == 0) && (slot_power_limit_value > 0xef))
            {
                /* Handle special case of large slot power limit values which exceed the standard encoding */
                switch (slot_power_limit_value)
                {
                case 0xF0:
                    slot_power_limit = 250;
                    break;

                case 0xF1:
                    slot_power_limit = 275;
                    break;

                default:
                    slot_power_limit = 300;
                    break;
                }
            }

            display_indent (indent_level);
            printf ("    Slot capabilities:");
            if ((slot_capabilities & PCI_EXP_SLTCAP_ABP) != 0)
            {
                printf ( "  Attention Button Present");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_PCP) != 0)
            {
                printf ("  Power Controller Present");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_MRLSP) != 0)
            {
                printf ("  MRL Sensor Present");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_AIP) != 0)
            {
                printf ("  Attention Indicator Present");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_PIP) != 0)
            {
                printf ("  Power Indicator Present");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_HPS) != 0)
            {
                printf ("  Hot-Plug Surprise");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_HPC) != 0)
            {
                printf ("  Hot-Plug Capable");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_EIP) != 0)
            {
                printf ("  Electromechanical Interlock Present");
            }
            if ((slot_capabilities & PCI_EXP_SLTCAP_NCCS) != 0)
            {
                printf ("  No Command Completed Support");
            }
            printf ("  Physical slot number %u", physical_slot_number);
            printf ("  Slot power limit %g W", slot_power_limit);
            printf ("\n");
        }
    }

    return rc;
}


/**
 * @brief Perform a partial display of PCI capabilities
 * @details Used https://astralvx.com/storage/2020/11/PCI_Express_Base_4.0_Rev0.3_February19-2014.pdf as a reference
 * @param[in] indent_level Amount of indentation at the start of each line of output
 * @param[in] device Device to read from
 */
static void display_pci_capabilities (const uint32_t indent_level, struct pci_device *const device)
{
    const char *const capability_id_names[] =
    {
        [PCI_CAP_ID_NULL   ] = "Null Capability",
        [PCI_CAP_ID_PM     ] = "Power Management",
        [PCI_CAP_ID_AGP    ] = "Accelerated Graphics Port",
        [PCI_CAP_ID_VPD    ] = "Vital Product Data",
        [PCI_CAP_ID_SLOTID ] = "Slot Identification",
        [PCI_CAP_ID_MSI    ] = "Message Signaled Interrupts",
        [PCI_CAP_ID_CHSWP  ] = "CompactPCI HotSwap",
        [PCI_CAP_ID_PCIX   ] = "PCI-X",
        [PCI_CAP_ID_HT     ] = "HyperTransport",
        [PCI_CAP_ID_VNDR   ] = "Vendor specific",
        [PCI_CAP_ID_DBG    ] = "Debug port",
        [PCI_CAP_ID_CCRC   ] = "CompactPCI Central Resource Control",
        [PCI_CAP_ID_HOTPLUG] = "PCI hot-plug",
        [PCI_CAP_ID_SSVID  ] = "Bridge subsystem vendor/device ID",
        [PCI_CAP_ID_AGP3   ] = "AGP 8x",
        [PCI_CAP_ID_SECURE ] = "Secure device (?)",
        [PCI_CAP_ID_EXP    ] = "PCI Express",
        [PCI_CAP_ID_MSIX   ] = "MSI-X",
        [PCI_CAP_ID_SATA   ] = "Serial-ATA HBA",
        [PCI_CAP_ID_AF     ] = "Advanced features of PCI devices integrated in PCIe root cplx",
        [PCI_CAP_ID_EA     ] = "Enhanced Allocation"
    };
    const size_t capability_id_names_array_size = sizeof (capability_id_names) / sizeof (capability_id_names[0]);

    bool been_hear[256] = {false};
    int rc;
    uint16_t status_register;
    errno = 0;
    rc = pci_device_cfg_read_u16 (device, &status_register, PCI_STATUS);
    uint8_t capability_pointer;
    uint8_t capability_id;

    /* Check for presence of PCI capabilities */
    if ((rc == 0) && (status_register & PCI_STATUS_CAP_LIST) != 0)
    {
        /* Iterate over all capabilities. been_hear[] used as protection against infinite loops due to malformed capability lists */
        rc = pci_device_cfg_read_u8 (device, &capability_pointer, PCI_CAPABILITY_LIST);
        while ((rc == 0) && (capability_pointer != 0) && (!been_hear[capability_pointer]))
        {
            rc = pci_device_cfg_read_u8 (device, &capability_id, capability_pointer + PCI_CAP_LIST_ID);

            if (rc == 0)
            {
                /* Display the capability identity */
                display_indent (indent_level);
                printf ("  Capabilities: [%x] ", capability_pointer);
                if ((capability_id < capability_id_names_array_size) && (capability_id_names[capability_id] != NULL))
                {
                    printf ("%s", capability_id_names[capability_id]);
                }
                else
                {
                    printf ("Capability ID 0x%x", capability_id);
                }

                /* Perform identify specific decode */
                switch (capability_id)
                {
                case PCI_CAP_ID_EXP:
                    rc = display_pci_express_capabilities (indent_level, device, capability_pointer);
                    break;

                default:
                    printf ("\n");
                    break;
                }

                if (rc == 0)
                {
                    /* Advance to next capability */
                    been_hear[capability_pointer] = true;
                    rc = pci_device_cfg_read_u8 (device, &capability_pointer, capability_pointer + PCI_CAP_LIST_NEXT);
                }
            }
        }
    }

    if (rc != 0)
    {
        display_indent (indent_level);
        printf ("  PCI configuration read failed : rc=%d %s\n", rc, strerror (rc));
    }
}


/**
 * @brief Display information for one PCI device
 * @param[in] device Device to display information for
 * @param[in] indent_level Amount of indentation at the start of each line of output
 */
static void display_pci_device (struct pci_device *const device, const uint32_t indent_level)
{
    int rc;
    uint16_t cmd;

    rc = pci_device_probe (device);
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u16 (device, &cmd, PCI_COMMAND);

        if (rc == 0)
        {
            display_indent (indent_level);
            printf ("domain=%04x bus=%02x dev=%02x func=%02x\n",
                    device->domain, device->bus, device->dev, device->func);
            display_indent (indent_level);
            printf ("  vendor_id=%04x (%s) device_id=%04x (%s) subvendor_id=%04x subdevice_id=%04x\n",
                    device->vendor_id, pci_device_get_vendor_name (device),
                    device->device_id, pci_device_get_device_name (device),
                    device->subvendor_id, device->subdevice_id);
            display_indent (indent_level);
            printf ("  control: I/O%s Mem%s BusMaster%s\n",
                    (cmd & PCI_COMMAND_IO) ? "+" : "-",
                    (cmd & PCI_COMMAND_MEMORY) ? "+" : "-",
                    (cmd & PCI_COMMAND_MASTER) ? "+" : "-");
            for (unsigned bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
            {
                const struct pci_mem_region *const region = &device->regions[bar_index];

                if (region->size > 0)
                {
                    display_indent (indent_level);
                    printf ("  bar[%u] base_addr=%" PRIx64 " size=%" PRIx64 " is_IO=%u is_prefetchable=%u is_64=%u\n",
                            bar_index, region->base_addr, region->size, region->is_IO, region->is_prefetchable, region->is_64);
                }
            }

            display_pci_capabilities (indent_level, device);
        }
    }
}


int main (int argc, char *argv[])
{
    int rc;
    uint32_t vendor_id;
    uint32_t device_id;
    char junk;

    rc = pci_system_init ();
    if (rc != 0)
    {
        fprintf (stderr, "pci_system_init failed\n");
        exit (EXIT_FAILURE);
    }

    /* Use an optional command line arguments to specify the vendor and possibly device ID to display information for */
    vendor_id = FPGA_SIO_VENDOR_ID;
    device_id = PCI_MATCH_ANY;
    if (argc > 1)
    {
        const char *const vendor_txt = argv[1];
        if (sscanf (vendor_txt, "%x%c", &vendor_id, &junk) != 1)
        {
            printf ("Error: Invalid hex vendor ID %s\n", vendor_txt);
        }
    }
    if (argc > 2)
    {
        const char *const device_txt = argv[2];
        if (sscanf (device_txt, "%x%c", &device_id, &junk) != 1)
        {
            printf ("Error: Invalid hex vendor ID %s\n", device_txt);
        }
    }

    struct pci_id_match match =
    {
        .vendor_id = vendor_id,
        .device_id = device_id,
        .subvendor_id = PCI_MATCH_ANY,
        .subdevice_id = PCI_MATCH_ANY,
        .device_class = 0,
        .device_class_mask = 0
    };

    struct pci_device_iterator *const device_iterator = pci_id_match_iterator_create (&match);
    struct pci_device *device;
    struct pci_device *parent_bridge;
    uint32_t indent_level;

    device = pci_device_next (device_iterator);
    while (device != NULL)
    {
        rc = pci_device_probe (device);
        if (rc == 0)
        {
            /* Display the device which matches the filter */
            indent_level = 0;
            display_pci_device (device, indent_level);

            /* If the device is connected to a parent bridge, display the information about the parent bridge
             * to allow correlation of the PCIe link capabilities between the two. */
            /* Display information about the tree of parent bridges, to allow correlation of the PCIe link capabilities
             * up the bridges until the root port. */
            parent_bridge = pci_device_get_parent_bridge (device);
            while (parent_bridge != NULL)
            {
                indent_level += 2;
                display_pci_device (parent_bridge, indent_level);
                parent_bridge = pci_device_get_parent_bridge (parent_bridge);
            }

            printf ("\n");
        }
        device = pci_device_next (device_iterator);
    }

    return EXIT_SUCCESS;
}
