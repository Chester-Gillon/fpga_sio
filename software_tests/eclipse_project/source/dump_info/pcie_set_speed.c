/*
 * @file pcie_set_speed.c
 * @date 11 Aug 2024
 * @author Chester Gillon
 * @brief Change a device's target link speed
 * @details
 *   This is based upon the pcie_set_speed.sh script at https://alexforencich.com/wiki/en/pcie/set-speed, which is a bash
 *   script which uses the setpci command.
 */

#include "generic_pci_access.h"
#include "vfio_bitops.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>
#include <pci/pci.h>
#include <linux/pci_regs.h> /* For PCI_EXP_LNKCTL2_TLS */


/* Structure used to read or write the PCIe express capability configuration registers for a device */
typedef struct
{
    /* Overall success. Set false on the first error attempting to access the configuration registers */
    bool success;
    /* The PCIe device for which are accessing the PCIe express capability for */
    generic_pci_access_device_p device;
    /* The PCIe capability pointer, used as the offset for the start of the configuration registers */
    uint8_t capability_pointer;
} exp_cap_access_t;


/**
 * @brief Read a PCIe capability configuration register for a device
 * @details Only attempts the read if the overall status is currently successful.
 *          Reports a diagnostic error on the first failure, including the errno from the underlying configuration read.
 * @param[in/out] access The access mechanism, updated if the read fails.
 * @param[in] offset The offset into the PCIe express capability configuration registers to read
 * @param[out] value The configuration register value read.
 */
static void exp_cap_read_u16 (exp_cap_access_t *const access, const uint32_t offset, uint16_t *const value)
{
    if (access->success)
    {
        errno = 0;
        if (!generic_pci_access_cfg_read_u16 (access->device, access->capability_pointer + offset, value))
        {
            printf ("PCIe capability U16 read for register 0x%x failed : %s\n", offset, strerror (errno));
            access->success = false;
        }
    }
}

static void exp_cap_read_u32 (exp_cap_access_t *const access, const uint32_t offset, uint32_t *const value)
{
    if (access->success)
    {
        errno = 0;
        if (!generic_pci_access_cfg_read_u32 (access->device, access->capability_pointer + offset, value))
        {
            printf ("PCIe capability U32 read for register 0x%x failed : %s\n", offset, strerror (errno));
            access->success = false;
        }
    }
}


/**
 * @brief Write a PCIe capability configuration register for a device
 * @details Only attempts the write if the overall status is currently successful.
 *          Reports a diagnostic error on the first failure, including the errno from the underlying configuration write.
 * @param[in/out] access The access mechanism, updated if the write fails.
 * @param[in] offset The offset into the PCIe express capability configuration registers to write
 * @param[in] value The configuration register value to write
 */
static void exp_cap_write_u16 (exp_cap_access_t *const access, const uint32_t offset, const uint16_t value)
{
    if (access->success)
    {
        errno = 0;
        if (!generic_pci_access_cfg_write_u16 (access->device, access->capability_pointer + offset, value))
        {
            printf ("PCIe capability U16 write for register 0x%x failed : %s\n", offset, strerror (errno));
            access->success = false;
        }
    }
}


/**
 * @brief Obtain the PCIe capability pointer for a device, to be able to access the PCIe express capability configuration registers.
 * @param[out] access The initialised access mechanism.
 * @param[in/out] device The device to obtain the capability pointer for
 */
static void obtain_pcie_capability_pointer (exp_cap_access_t *const access, generic_pci_access_device_p const device)
{
    bool found_capability_pointer = false;
    uint16_t status_register;
    bool been_hear[256] = {false};
    uint8_t capability_id;

    errno = 0;
    access->device = device;
    access->success = generic_pci_access_cfg_read_u16 (access->device, PCI_STATUS, &status_register);

    /* Check for presence of PCI capabilities */
    if (access->success && (status_register & PCI_STATUS_CAP_LIST) != 0)
    {
        /* Iterate over all capabilities, looking for the PCIe capability.
         * been_hear[] used as protection against infinite loops due to malformed capability lists */
        access->success = generic_pci_access_cfg_read_u8 (access->device, PCI_CAPABILITY_LIST, &access->capability_pointer);
        while (access->success && (!found_capability_pointer) &&
               (access->capability_pointer != 0) && (!been_hear[access->capability_pointer]))
        {
            access->success = generic_pci_access_cfg_read_u8 (device, (access->capability_pointer) + PCI_CAP_LIST_ID, &capability_id);

            if (access->success)
            {
                if (capability_id == PCI_CAP_ID_EXP)
                {
                    found_capability_pointer = true;
                }
                else
                {
                    /* Advance to next capability */
                    been_hear[access->capability_pointer] = true;
                    access->success = generic_pci_access_cfg_read_u8 (device, (access->capability_pointer) + PCI_CAP_LIST_NEXT,
                            &access->capability_pointer);
                }
            }
        }
    }

    if (!access->success)
    {
        printf ("Failed to read capability pointer : %s\n", strerror (errno));
    }
}


int main (int argc, char *argv[])
{
    char junk;
    generic_pci_access_filter_t filter;
    int exit_status = EXIT_FAILURE;

    const char *const link_speed_names[] =
    {
        [1] = "2.5 GT/s",
        [2] = "5 GT/s",
        [3] = "8 GT/s",
        [4] = "16 GT/s",
        [5] = "32 GT/s",
        [6] = "64 GT/s"
    };

    /* Check number of command line arguments */
    if ((argc < 2) || (argc > 3))
    {
        fprintf (stderr, "Usage: %s <domain>:<bus>:<dev>.<func> [<new_speed>]\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    /* Create a filter of the device to operate on, from the PCI bus location of the device specified on the command line */
    const char *const location_text = argv[1];
    memset (&filter, 0, sizeof (filter));
    if (sscanf (location_text, "%x:%" SCNx8 ":%" SCNx8 ".%" SCNx8 "%c",
            &filter.domain, &filter.bus, &filter.func, &filter.dev, &junk) == 4)
    {
        filter.filter_type = GENERIC_PCI_ACCESS_FILTER_LOCATION;
    }
    else
    {
        fprintf (stderr, "Invalid PCI device location %s\n", location_text);
        exit (EXIT_FAILURE);
    }

    /* Extract optional command line argument for the target link speed */
    uint32_t new_target_link_speed;
    bool target_link_speed_specified = false;
    if (argc >= 3)
    {
        const char *const target_link_speed_text = argv[2];

        if ((sscanf (target_link_speed_text, "%" SCNu32 "%c", &new_target_link_speed, &junk) == 1) &&
            (new_target_link_speed >= PCI_EXP_LNKCTL2_TLS_2_5GT) && (new_target_link_speed <= PCI_EXP_LNKCTL2_TLS_64_0GT))
        {
            target_link_speed_specified = true;
        }
        else
        {
            fprintf (stderr, "Invalid target link speed %s\n", target_link_speed_text);
            exit (EXIT_FAILURE);
        }
    }

    /* Open the device specified on the command line  */
    generic_pci_access_context_p const access_context = generic_pci_access_initialise ();
    generic_pci_access_iterator_p const device_iterator = generic_pci_access_iterator_create (access_context, &filter);
    generic_pci_access_device_p device = generic_pci_access_iterator_next (device_iterator);
    exp_cap_access_t access;
    if (device != NULL)
    {
        uint16_t flags;

        obtain_pcie_capability_pointer (&access, device);

        /* PCIe endpoints and upstream ports don't support the Retrain Link bit which is required to set the PCIe speed.
         * Therefore, if such a PCIe device has been specified need to operate on the parent bridge for the PCIe device */
        exp_cap_read_u16 (&access, PCI_EXP_FLAGS, &flags);
        if (access.success)
        {
            const uint32_t device_port_type = vfio_extract_field_u32 (flags, PCI_EXP_FLAGS_TYPE);

            if ((device_port_type == PCI_EXP_TYPE_ENDPOINT) ||
                (device_port_type == PCI_EXP_TYPE_LEG_END ) ||
                (device_port_type == PCI_EXP_TYPE_UPSTREAM))
            {
                device = generic_pci_access_get_parent_bridge (device);
                access.success = access.success && (device != NULL);
                if (access.success)
                {
                    obtain_pcie_capability_pointer (&access, device);
                }
                else
                {
                    printf ("Failed to get parent bridge for target device\n");
                }
            }
        }

        /* Display information on the device which are going to operate on */
        uint32_t domain;
        uint32_t bus;
        uint32_t dev;
        uint32_t func;
        uint32_t vendor_id;
        uint32_t device_id;
        uint32_t revision_id;
        uint32_t link_capabilities;
        uint16_t link_status;
        uint16_t link_control2;
        uint32_t current_link_speed;
        exp_cap_read_u32 (&access, PCI_EXP_LNKCAP, &link_capabilities);
        exp_cap_read_u16 (&access, PCI_EXP_LNKSTA, &link_status);
        exp_cap_read_u16 (&access, PCI_EXP_LNKCTL2, &link_control2);

        current_link_speed = vfio_extract_field_u32 (link_status, PCI_EXP_LNKSTA_SPEED);
        const uint32_t max_link_speed = vfio_extract_field_u32 (link_capabilities, PCI_EXP_LNKCAP_SPEED);
        const uint32_t max_link_width = vfio_extract_field_u32 (link_capabilities, PCI_EXP_LNKCAP_WIDTH);
        const uint32_t original_target_link_speed = vfio_extract_field_u32 (link_control2, PCI_EXP_LNKCTL2_TLS);
        if (access.success)
        {
            access.success = access.success &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_DOMAIN, &domain) &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_BUS, &bus) &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_DEV, &dev) &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_FUNC, &func) &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_VENDOR_ID, &vendor_id) &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_DEVICE_ID, &device_id) &&
                    generic_pci_access_uint_property (access.device, GENERIC_PCI_ACCESS_REVISION_ID, &revision_id);
            if (access.success)
            {
                printf ("Operating on device %04x:%02x:%02x.%x vendor_id=%04x (%s) device_id=%04x (%s) revision_id=%02x\n",
                        domain, bus, dev, func,
                        vendor_id, generic_pci_access_text_property (access.device, GENERIC_PCI_ACCESS_VENDOR_NAME),
                        device_id, generic_pci_access_text_property (access.device, GENERIC_PCI_ACCESS_DEVICE_NAME),
                        revision_id);
                printf ("Link capabilities: %08X Max link speed %s max link width x%u\n",
                        link_capabilities, link_speed_names[max_link_speed], max_link_width);
                printf ("Link status: %04X\n", link_status);
                printf ("Current link speed: %s\n", link_speed_names[current_link_speed]);
                printf ("Original link control 2: %04X\n", link_control2);
                printf ("Original target link speed: %u (%s)\n",
                        original_target_link_speed, link_speed_names[original_target_link_speed]);
            }
            else
            {
                printf ("Failed to get device properties\n");
            }
        }

        if (access.success)
        {
            if (target_link_speed_specified)
            {
                /* The new target link speed was specified on the command line. Limit to the maximum support speed */
                if (new_target_link_speed > max_link_speed)
                {
                    new_target_link_speed = max_link_speed;
                }
            }
            else
            {
                /* Default to selecting the maximum link speed */
                new_target_link_speed = max_link_speed;
            }

            /* Write link control 2 with the new target link speed */
            link_control2 = (uint16_t) (link_control2 & ~PCI_EXP_LNKCTL2_TLS);
            link_control2 = (uint16_t) (link_control2 | new_target_link_speed);
            printf ("New target link speed: %u (%s)\n",
                    new_target_link_speed, link_speed_names[new_target_link_speed]);
            printf ("New link control 2: %04X\n", link_control2);
            exp_cap_write_u16 (&access, PCI_EXP_LNKCTL2, link_control2);
        }

        /* Trigger link retraining */
        uint16_t original_link_control;
        exp_cap_read_u16 (&access, PCI_EXP_LNKCTL, &original_link_control);
        if (access.success)
        {
            const uint16_t new_link_control = original_link_control | PCI_EXP_LNKCTL_RETRAIN;

            printf ("Triggering link retraining by changing link control %04X -> %04X\n", original_link_control, new_link_control);
            exp_cap_write_u16 (&access, PCI_EXP_LNKCTL, new_link_control);
        }

        if (access.success)
        {
            /* Wait to link training to complete */
            sleep (1);

            /* Read link status to check effect */
            exp_cap_read_u16 (&access, PCI_EXP_LNKSTA, &link_status);
            if (access.success)
            {
                current_link_speed = vfio_extract_field_u32 (link_status, PCI_EXP_LNKSTA_SPEED);
                printf ("Link status: %04X\n", link_status);
                printf ("Current link speed: %s\n", link_speed_names[current_link_speed]);
            }
        }

        exit_status = (access.success) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else
    {
        printf ("Failed to open device at location %s\n", location_text);
    }

    generic_pci_access_iterator_destroy (device_iterator);
    generic_pci_access_finalise (access_context);

    return exit_status;
}
