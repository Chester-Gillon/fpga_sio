/*
 * @file test_ddr.c
 * @date 29 Jan 2023
 * @author Chester Gillon
 * @brief Perform tests of NiteFury or LiteFury DMA using VFIO
 */

#include "vfio_access.h"
#include "fury_utils.h"
#include "fpga_sio_pci_ids.h"

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>


int main (int argc, char *argv[])
{
    const size_t page_size = (size_t) getpagesize ();
    vfio_devices_t vfio_devices;
    uint32_t board_version;
    fury_type_t fury_type;
    vfio_dma_mapping_t descriptors_mapping;
    vfio_dma_mapping_t h2c_mapping;
    vfio_dma_mapping_t c2h_mapping;

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

    const size_t fury_ddr_sizes_bytes[] =
    {
        [DEVICE_LITE_FURY] =  512 * 1024 * 1024,
        [DEVICE_NITE_FURY] = 1024 * 1024 * 1024
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
            const size_t ddr_size_bytes = fury_ddr_sizes_bytes[fury_type];

            printf ("Testing %s board version 0x%x with DDR size 0x%zx for PCI device %s IOMMU group %s\n",
                    fury_names[fury_type], board_version, ddr_size_bytes, vfio_device->device_name, vfio_device->iommu_group);

            /* Create read/write mapping of a single page for DMA descriptors */
            allocate_vfio_dma_mapping (&vfio_devices, &descriptors_mapping, page_size,
                    VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE);

            /* Read mapping used by device to transfer a region of host memory to the entire DDR contents */
            allocate_vfio_dma_mapping (&vfio_devices, &h2c_mapping, ddr_size_bytes, VFIO_DMA_MAP_FLAG_READ);

            /* Write mapping for a single page used by device to write one page of DDR to host memory */
            allocate_vfio_dma_mapping (&vfio_devices, &c2h_mapping, page_size, VFIO_DMA_MAP_FLAG_WRITE);

            free_vfio_dma_mapping (&vfio_devices, &c2h_mapping);
            free_vfio_dma_mapping (&vfio_devices, &h2c_mapping);
            free_vfio_dma_mapping (&vfio_devices, &descriptors_mapping);
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
