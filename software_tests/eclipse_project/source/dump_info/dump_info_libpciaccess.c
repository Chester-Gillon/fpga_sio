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


#define NELEMENTS(array) (sizeof(array) / sizeof(array[0]))


/**
 * @brief Display one PCIe flag (a single bit) in a similar format to lspci
 * @param[in] field_name The name of the field displayed
 * @param[in] register_value The register value
 * @param[in] field_mask The mask containing the single bit for the field
 */
static inline void display_flag (const char *const field_name, const uint32_t register_value, const uint32_t field_mask)
{
    printf (" %s%s", field_name, ((register_value & field_mask) != 0) ? "+" : "-");
}


/**
 * @brief Extract a field which spans multiple consecutive bits
 * @param[in] register_value The register containing the field
 * @param[in] field_mask The mask for the field to extract
 * @return The extracted field value, shifted to the least significant bits
 */
static inline uint32_t extract_field (const uint32_t register_value, const uint32_t field_mask)
{
    const int field_shift = ffs ((int) field_mask) - 1; /* ffs returns the least significant bit as zero */

    return (register_value & field_mask) >> field_shift;
}


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
 * @brief Display an enumeration
 * @param[in] enums_array_size The size of the enumeration array
 * @param[in] enum_names Contains the names of the enumeration. Values which are not valid are NULL.
 * @param[in] value The enumeration value to display the name for
 */
static void display_enumeration (const size_t enums_array_size, const char *const enum_names[const enums_array_size],
                                 const uint32_t value)
{
    if ((value < enums_array_size) && (enum_names[value] != NULL))
    {
        printf ("%s", enum_names[value]);
    }
    else
    {
        printf ("Unknown encoding 0x%x", value);
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
    uint16_t device_control;
    uint16_t device_status;
    uint32_t link_capabilities;
    uint16_t link_control;
    uint16_t link_status;
    uint32_t link_capabilities2;
    uint32_t slot_capabilities;

    rc = pci_device_cfg_read_u16 (device, &flags, capability_pointer + PCI_EXP_FLAGS);
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u16 (device, &device_control, capability_pointer + PCI_EXP_DEVCTL);
    }
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u16 (device, &device_status, capability_pointer + PCI_EXP_DEVSTA);
    }
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u32 (device, &link_capabilities, capability_pointer + PCI_EXP_LNKCAP);
    }
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u16 (device, &link_control, capability_pointer + PCI_EXP_LNKCTL);
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
        const uint32_t capability_version = extract_field (flags, PCI_EXP_FLAGS_VERS);
        const uint32_t device_port_type = extract_field (flags, PCI_EXP_FLAGS_TYPE);
        const uint32_t interrupt_message_number =  extract_field (flags, PCI_EXP_FLAGS_IRQ);
        const bool slot_implemented = (flags & PCI_EXP_FLAGS_SLOT) != 0;

        const uint32_t max_link_speed = extract_field (link_capabilities, PCI_EXP_LNKCAP_SPEED);
        const uint32_t max_link_width = extract_field (link_capabilities, PCI_EXP_LNKCAP_WIDTH);

        const uint32_t negotiated_link_speed = extract_field (link_status, PCI_EXP_LNKSTA_SPEED);
        const uint32_t negotiated_link_width = extract_field (link_status, PCI_EXP_LNKSTA_WIDTH);

        const uint32_t supported_link_speeds = PCI_EXP_LNKCAP2_SPEED (link_capabilities2);

        const uint32_t slot_power_limit_value = extract_field (slot_capabilities, PCI_EXP_SLTCAP_SPLV);
        const uint32_t slot_power_limit_scale = extract_field (slot_capabilities, PCI_EXP_SLTCAP_SPLS);
        const uint32_t physical_slot_number = extract_field (slot_capabilities, PCI_EXP_SLTCAP_PSN);

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

        const char *const link_speed_names[] =
        {
            [1] = "2.5 GT/s",
            [2] = "5.0 GT/s",
            [3] = "8.0 GT/s",
            [4] = "16.0 GT/s"
        };

        const double slot_power_limit_scales[] =
        {
            1.0,
            0.1,
            0.01,
            0.001
        };

        const char *const aspm_names[] =
        {
            [0] = "not supported",
            [1] = "L0s",
            [2] = "L1",
            [3] = "L0s and L1"
        };

        const char *const l0s_exit_latency_names[] =
        {
            [0] = "Less than 64 ns",
            [1] = "64 ns to less than 128 ns",
            [2] = "128 ns to less than 256 ns",
            [3] = "256 ns to less than 512 ns",
            [4] = "512 ns to less than 1 μs",
            [5] = "1 μs to less than 2 μs",
            [6] = "2 μs to 4 μs",
            [7] = "More than 4 μs"
        };

        const char *const l1_exit_latency_names[] =
        {
            [0] = "Less than 1 μs",
            [1] = "1 μs to less than 2 μs",
            [2] = "2 μs to less than 4 μs",
            [3] = "4 μs to less than 8 μs",
            [4] = "8 μs to less than 16 μs",
            [5] = "16 μs to less than 32 μs",
            [6] = "32 μs to 64 μs",
            [7] = "More than 64 μs"
        };

        const char *const aspm_control_names[] =
        {
            [0] = "Disabled",
            [1] = "L0s Entry Enabled",
            [2] = "L1 Entry Enabled",
            [3] = "L0s and L1 Entry Enabled"
        };

        /* Continuation of the capability identification line from the caller */
        printf (" v%u ", capability_version);
        display_enumeration (NELEMENTS (device_port_type_names), device_port_type_names, device_port_type);
        printf (", MSI %u\n", interrupt_message_number);

        /* Display link capabilities */
        display_indent (indent_level);
        printf ("    Link capabilities: Max speed ");
        display_enumeration (NELEMENTS (link_speed_names), link_speed_names, max_link_speed);
        printf (" Max width x%u\n", max_link_width);

        /* Display negotiated link status */
        display_indent (indent_level);
        printf ("    Negotiated link status: Current speed ");
        display_enumeration (NELEMENTS (link_speed_names), link_speed_names, negotiated_link_speed);
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

        /* Display device control */
        display_indent (indent_level);
        printf ("    DevCtl:");
        display_flag ("CorrErr", device_control, PCI_EXP_DEVCTL_CERE);
        display_flag ("NonFatalErr", device_control, PCI_EXP_DEVCTL_NFERE);
        display_flag ("FatalErr", device_control, PCI_EXP_DEVCTL_FERE);
        display_flag ("UnsupReq", device_control, PCI_EXP_DEVCTL_URRE);
        printf ("\n");
        display_indent (indent_level);
        printf ("           ");
        display_flag ("RlxdOrd", device_control, PCI_EXP_DEVCTL_RELAX_EN);
        display_flag ("ExtTag", device_control, PCI_EXP_DEVCTL_EXT_TAG);
        display_flag ("PhantFunc", device_control, PCI_EXP_DEVCTL_PHANTOM);
        display_flag ("AuxPwr", device_control, PCI_EXP_DEVCTL_AUX_PME);
        display_flag ("NoSnoop", device_control, PCI_EXP_DEVCTL_NOSNOOP_EN);
        printf ("\n");

        /* Display device status */
        display_indent (indent_level);
        printf ("    DevSta:");
        display_flag ("CorrErr", device_status, PCI_EXP_DEVSTA_CED);
        display_flag ("NonFatalErr", device_status, PCI_EXP_DEVSTA_NFED);
        display_flag ("FatalErr", device_status, PCI_EXP_DEVSTA_FED);
        display_flag ("UnsupReq", device_status, PCI_EXP_DEVSTA_URD);
        display_flag ("AuxPwr", device_status, PCI_EXP_DEVSTA_AUXPD);
        display_flag ("TransPend", device_status, PCI_EXP_DEVSTA_TRPND);
        printf ("\n");

        /* Display link capabilities (excluding width and speed displayed above) */
        display_indent (indent_level);
        printf ("    LnkCap:");
        printf (" Port # %u", extract_field (link_capabilities, PCI_EXP_LNKCAP_PN));
        printf (" ASPM ");
        display_enumeration (NELEMENTS (aspm_names), aspm_names, extract_field (link_capabilities, PCI_EXP_LNKCAP_ASPMS));
        printf ("\n");
        display_indent (indent_level);
        printf ("            L0s Exit Latency ");
        display_enumeration (NELEMENTS (l0s_exit_latency_names), l0s_exit_latency_names,
                extract_field (link_capabilities, PCI_EXP_LNKCAP_L0SEL));
        printf ("\n");
        display_indent (indent_level);
        printf ("            L1 Exit Latency ");
        display_enumeration (NELEMENTS (l1_exit_latency_names), l1_exit_latency_names,
                extract_field (link_capabilities, PCI_EXP_LNKCAP_L1EL));
        printf ("\n");
        display_indent (indent_level);
        printf ("           ");
        display_flag ("ClockPM", link_capabilities, PCI_EXP_LNKCAP_CLKPM);
        display_flag ("Surprise", link_capabilities, PCI_EXP_LNKCAP_SDERC);
        display_flag ("LLActRep", link_capabilities, PCI_EXP_LNKCAP_DLLLARC);
        display_flag ("BwNot", link_capabilities, PCI_EXP_LNKCAP_LBNC);
        /* No macro defined for the "ASPM Optionality Compliance" bit.
         * The PCIe v4 spec says:
         *   "This bit must be set to 1b in all Functions. Components implemented against certain earlier
         *    versions of this specification will have this bit set to 0b." */
        display_flag ("ASPMOptComp", link_capabilities, 1u << 22);
        printf ("\n");

        /* Display link control */
        display_indent (indent_level);
        printf ("    LnkCtl:");
        printf (" ASPM ");
        display_enumeration (NELEMENTS (aspm_control_names), aspm_control_names,
                extract_field (link_control, PCI_EXP_LNKCTL_ASPMC));
        printf (" RCB %u bytes", (link_control & PCI_EXP_LNKCTL_RCB) != 0 ? 128 : 64);
        display_flag ("Disabled", link_control, PCI_EXP_LNKCTL_LD);
        display_flag ("CommClk", link_control, PCI_EXP_LNKCTL_CCC);
        printf ("\n");
        printf ("           ");
        display_flag ("ExtSynch", link_control, PCI_EXP_LNKCTL_ES);
        display_flag ("ClockPM", link_control, PCI_EXP_LNKCTL_CLKREQ_EN);
        display_flag ("AutWidDis", link_control, PCI_EXP_LNKCTL_HAWD);
        display_flag ("BWInt", link_control, PCI_EXP_LNKCTL_LBMIE);
        display_flag ("ABWMgmt", link_control, PCI_EXP_LNKCTL_LABIE);
        printf ("\n");

        /* Display link status (excluding width and speed displayed above) */
        display_indent (indent_level);
        printf ("    LnkSta:");
        display_flag ("TrErr", link_status, 1u << 10); /* PCIe v4 spec says this is now reserved */
        display_flag ("Train", link_status, PCI_EXP_LNKSTA_LT);
        display_flag ("SlotClk", link_status, PCI_EXP_LNKSTA_SLC);
        display_flag ("DLActive", link_status, PCI_EXP_LNKSTA_DLLLA);
        display_flag ("BWMgmt", link_status, PCI_EXP_LNKSTA_LBMS);
        display_flag ("ABWMgmt", link_status, PCI_EXP_LNKSTA_LABS);
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
            printf ("    SltCap:");
            display_flag ("AttnBtn", slot_capabilities, PCI_EXP_SLTCAP_ABP);
            display_flag ("PwrCtrl", slot_capabilities, PCI_EXP_SLTCAP_PCP);
            display_flag ("MRL", slot_capabilities, PCI_EXP_SLTCAP_MRLSP);
            display_flag ("AttnInd", slot_capabilities, PCI_EXP_SLTCAP_AIP);
            display_flag ("PwrInd", slot_capabilities, PCI_EXP_SLTCAP_PIP);
            display_flag ("HotPlug", slot_capabilities, PCI_EXP_SLTCAP_HPC);
            display_flag ("Surprise", slot_capabilities, PCI_EXP_SLTCAP_HPS);
            printf ("\n");
            display_indent (indent_level);
            printf ("            ");
            printf ("Slot #%u", physical_slot_number);
            printf (" PowerLimit %.3fW;", slot_power_limit);
            display_flag ("Interlock", slot_capabilities, PCI_EXP_SLTCAP_EIP);
            display_flag ("NoCompl", slot_capabilities, PCI_EXP_SLTCAP_NCCS);
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
                display_enumeration (NELEMENTS (capability_id_names), capability_id_names, capability_id);

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
    uint16_t status;

    rc = pci_device_probe (device);
    if (rc == 0)
    {
        rc = pci_device_cfg_read_u16 (device, &cmd, PCI_COMMAND);
        if (rc == 0)
        {
            rc = pci_device_cfg_read_u16 (device, &status, PCI_STATUS);
        }

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
            printf ("  control:");
            display_flag ("I/O", cmd, PCI_COMMAND_IO);
            display_flag ("Mem", cmd, PCI_COMMAND_MEMORY);
            display_flag ("BusMaster", cmd, PCI_COMMAND_MASTER);
            display_flag ("ParErr", cmd, PCI_COMMAND_PARITY);
            display_flag ("SERR", cmd, PCI_COMMAND_SERR);
            display_flag ("DisINTx", cmd, PCI_COMMAND_INTX_DISABLE);
            printf ("\n");

            display_indent (indent_level);
            printf ("  status:");
            display_flag ("INTx", status, PCI_STATUS_INTERRUPT);
            display_flag ("<ParErr", status, PCI_STATUS_PARITY);
            display_flag (">TAbort", status, PCI_STATUS_SIG_TARGET_ABORT);
            display_flag ("<TAbort", status, PCI_STATUS_REC_TARGET_ABORT);
            display_flag ("<MAbort", status, PCI_STATUS_REC_MASTER_ABORT);
            display_flag (">SERR", status, PCI_STATUS_SIG_SYSTEM_ERROR);
            display_flag ("DetParErr", status, PCI_STATUS_DETECTED_PARITY);
            printf ("\n");

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


/**
 * @brief Display information about all PCI devices which match an identity
 * @param[in] vendor_id The vendor identity to match, or PCI_MATCH_ANY
 * @param[in] device_id The device identity to match, or PCI_MATCH_ANY
 */
static void display_pci_devices_by_id (const uint32_t vendor_id, const uint32_t device_id)
{
    int rc;
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

            /* Display information about the tree of parent bridges to allow correlation of:
             * a. The PCIe link capabilities up the bridges until the root port.
             * b. Error reporting up the bridges until the root port. */
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

    pci_iterator_destroy (device_iterator);
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

    if (argc > 1)
    {
        /* Each command line argument is the <vendor_id> or <vendor_id>:<device_id> of PCI devices to display information for */
        for (int arg_index = 1; arg_index < argc; arg_index++)
        {
            const char *const match_text = argv[arg_index];

            if (sscanf (match_text, "%x:%x%c", &vendor_id, &device_id, &junk) == 2)
            {
                display_pci_devices_by_id (vendor_id, device_id);
            }
            else if (sscanf (match_text, "%x%c", &vendor_id, &junk) == 1)
            {
                display_pci_devices_by_id (vendor_id, PCI_MATCH_ANY);
            }
            else
            {
                printf ("Invalid PCI device ID %s\n", match_text);
                exit (EXIT_FAILURE);
            }
        }
    }
    else
    {
        /* With no arguments display all Xilinx devices */
        display_pci_devices_by_id (FPGA_SIO_VENDOR_ID, PCI_MATCH_ANY);
    }

    pci_system_cleanup ();

    return EXIT_SUCCESS;
}
