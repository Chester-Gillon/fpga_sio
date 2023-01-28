/*
 * @file test_general.c
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @details A version of https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/blob/master/Sample-Projects/Project-0/Host/test-general.py
 *          which is:
 *          - Written in C rather than Python.
 *          - Uses the vfio_access library to use memory mapped BARs in a user space application, rather a Kernel driver.
 */

#include "vfio_access.h"
#include "fury_utils.h"

#include <stdlib.h>
#include <stdio.h>

#include "fpga_sio_pci_ids.h"


int main (int argc, char *const argv[])
{
    vfio_devices_t vfio_devices;
    uint32_t board_version;
    fury_type_t fury_type;
    uint32_t xadc_register_value;

    /* Select to filter by vendor only */
    const vfio_pci_device_filter_t filter =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY
    };

    const char *const fury_names[] =
    {
        [DEVICE_LITE_FURY] = "LiteFury",
        [DEVICE_NITE_FURY] = "NiteFury"
    };

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* Process any NiteFury or LiteFury devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        const vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        fury_type = identify_fury (vfio_device, &board_version);
        if (fury_type != DEVICE_OTHER)
        {
            printf ("Found %s board version 0x%x for PCI device %s IOMMU group %s\n",
                    fury_names[fury_type], board_version, vfio_device->device_name, vfio_device->iommu_group);

            /* Read and convert XADC register values.
             * The scaling is defined in
             * https://www.xilinx.com/content/dam/xilinx/support/documents/user_guides/ug480_7Series_XADC.pdf
             *
             * The reported values were sanity checked against that shown by the XADC System Monitor values
             * reported over JTAG by the Vivado Hardware Manager. */
            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3200);
            printf ("Temp C=%.1f\n", ((double) (xadc_register_value >> 4) * 503.975 / 4096.0) - 273.15);

            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3204);
            printf ("VCCInt=%.2f\n", (double) (xadc_register_value >> 4) * 3.0 / 4096.0);

            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3208);
            printf ("vccaux=%.2f\n", (double) (xadc_register_value >> 4) * 3.0 / 4096.0);

            xadc_register_value = read_reg32 (vfio_device->mapped_bars[FURY_AXI_PERIPHERALS_BAR], 0x3218);
            printf ("vbram=%.2f\n", (double) (xadc_register_value >> 4) * 3.0 / 4096.0);
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
