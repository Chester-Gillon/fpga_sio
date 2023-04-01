/*
 * @file test_nvram_csr_access.c
 * @date 12 Mar 2023
 * @author Chester Gillon
 * @brief Test access to the CSR registers in a Micro Memory MM-5425CN NVRAM device using VFIO
 */

#include <stdlib.h>
#include <stdio.h>

#include "vfio_access.h"
#include "nvram_utils.h"


/**
 * @brief Sequence the tests on the NVRAM card CSR registers
 * @param[in/out] vfio_device The open VFIO device to which the NVRAM registers are mapped
 * @param[in] prompt If true prompt before each LED change, so the LED state can be checked
 */
static void perform_nvram_csr_tests (vfio_device_t *const vfio_device, const bool prompt)
{
    map_vfio_device_bar_before_use (vfio_device, NVRAM_CSR_BAR_INDEX);
    if (vfio_device->mapped_bars[NVRAM_CSR_BAR_INDEX] != NULL)
    {
        uint8_t *const csr = vfio_device->mapped_bars[NVRAM_CSR_BAR_INDEX];
        const uint8_t memory = read_reg8 (csr, MEMCTRLSTATUS_MEMORY);
        const uint8_t battery_status = read_reg8 (csr, MEMCTRLSTATUS_BATTERY);

        printf ("MEMCTRLSTATUS_MAGIC=0x%x\n", read_reg8 (csr, MEMCTRLSTATUS_MAGIC));
        printf ("MEMCTRLSTATUS_MEMORY=0x%x size ", memory);
        switch (memory)
        {
        case MEM_128_MB: printf ("128 MB\n"); break;
        case MEM_256_MB: printf ("256 MB\n"); break;
        case MEM_512_MB: printf ("512 MB\n"); break;
        case MEM_1_GB:   printf ("1GB\n"); break;
        case MEM_2_GB:   printf ("2GB\n"); break;
        default:         printf ("unknown\n"); break;
        }
        printf ("memctrlstatus_battery=0x%x Battery 1 %s (%s), Battery 2 %s (%s)\n",
                battery_status,
                battery_status & BATTERY_1_DISABLED ? "Disabled" : "Enabled",
                !(battery_status & BATTERY_1_FAILURE) ? "OK" : "FAILURE",
                battery_status & BATTERY_2_DISABLED ? "Disabled" : "Enabled",
                !(battery_status & BATTERY_2_FAILURE) ? "OK" : "FAILURE");
        printf ("MEMCTRLCMD_LEDCTRL=0x%x\n", read_reg8 (csr, MEMCTRLCMD_LEDCTRL));
        printf ("MEMCTRLCMD_ERRCTRL=0x%x\n", read_reg8 (csr, MEMCTRLCMD_ERRCTRL));

        set_led (csr, LED_FAULT, LED_ON);
        if (prompt)
        {
            printf ("LED_FAULT=LED_ON (press return to continue)");
            getchar ();
        }
        set_led (csr, LED_FAULT, LED_FLASH_7_0);
        if (prompt)
        {
            printf ("LED_FAULT=LED_FLASH_7_0 (press return to continue)");
            getchar ();
        }
        set_led (csr, LED_FAULT, LED_FLASH_3_5);
        if (prompt)
        {
            printf ("LED_FAULT=LED_FLASH_3_5 (press return to continue)");
            getchar ();
        }
        set_led (csr, LED_FAULT, LED_OFF);
        if (prompt)
        {
            printf ("LED_FAULT=LED_OFF (press return to continue)");
            getchar ();
        }
    }
}


int main (int argc, char *argv[])
{
    const bool prompt = argc >= 2;
    vfio_devices_t vfio_devices;

    const vfio_pci_device_filter_t filter =
    {
        .vendor_id = NVRAM_VENDOR_ID,
        .device_id = NVRAM_DEVICE_ID,
        .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .enable_bus_master = false
    };

    /* Open the Micro Memory devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* Process any Micro Memory devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        perform_nvram_csr_tests (&vfio_devices.devices[device_index], prompt);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
