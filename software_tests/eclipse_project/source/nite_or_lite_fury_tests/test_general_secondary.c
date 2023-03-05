/*
 * @file test_general_secondary.c
 * @date 5 Mar 2023
 * @author Chester Gillon
 * @brief The secondary process for testing multi-process VFIO access without using DMA
 */

#include "vfio_access.h"
#include "fury_utils.h"

#include <stdlib.h>
#include <stdio.h>

int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    display_open_fds ("test_general_secondary");

    /* Open the FPGA devices which have an IOMMU group assigned, obtaining container and group FDs from the primary process */
    open_vfio_devices_matching_filter (&vfio_devices, fury_num_pci_device_filters, fury_pci_device_filters);

    /* Process any NiteFury or LiteFury devices found */
    display_fury_xadc_values (&vfio_devices);

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
