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


int main (int argc, char *const argv[])
{
    vfio_devices_t vfio_devices;

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    /* Process any NiteFury or LiteFury devices found */
    display_fury_xadc_values (&vfio_devices);

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
