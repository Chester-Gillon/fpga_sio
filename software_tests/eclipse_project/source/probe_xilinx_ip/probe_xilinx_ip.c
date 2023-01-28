/*
 * @file probe_xilinx_ip.c
 * @date 21 Jan 2023
 * @author Chester Gillon
 * @brief Probe PCI devices with the Xilinx vendor ID for Xilinx IP
 * @details Works by mapping the BARs looking for the identity registers for Xilinx IP.
 *          Assumes reads are not destructive.
 *          Uses vifo to map the BARs.
 *
 *          This was created after looking at the Xilinx Kernel module for the DMA/Bridge Subsystem for PCI Express
 *          and seeing that probed to identify the capability of the system, with a view that was applicable to
 *          other IP. However, the limitations when trying to probe other IP was:
 *          a. In the examples used, didn't find AXI slaves which had identification registers.
 *          b. Attempting to read from from unimplemented AXI slave addresses can hang the PC, requiring the
 *             a hard power cycle to recover. See probe_nite_fury_or_lite_fury().
 */

#include "vfio_access.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fpga_sio_pci_ids.h"


/**
 * @details Check if a memory mapped BAR is that of the "PCIe to AXI Lite Master" in the
 *          https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/tree/master/Sample-Projects/Project-0/FPGA project
 *
 *          This is done by checking the fixed value used in a GPIO input register, which is set to a constant input
 *          inside the FGPA.
 * @param[in] mapped_bar Start of the memory mapped BAR to prove
 * @param[in] bar_size Size of the memory mapped BAR in bytes to probe
 * @return Returns true when the mapped BAR matches the search
 */
static bool probe_nite_fury_or_lite_fury (const uint8_t *const mapped_bar, const uint64_t bar_size)
{
    const uint64_t register_frame_size = 1 << 9;

    for (uint64_t bar_offset = 0; (bar_offset + register_frame_size) <= bar_size; bar_offset += register_frame_size)
    {
        /* pid string is a constant value fed to the GPIO input value */
        const uint32_t pid_integer = read_reg32 (mapped_bar, bar_offset + 0x0);
        const char *const pid_bytes = (const char *) &pid_integer;
        char pid_string[4];

        /* Need to reverse the bytes to get the pid string */
        pid_string[0] = pid_bytes[3];
        pid_string[1] = pid_bytes[2];
        pid_string[2] = pid_bytes[1];
        pid_string[3] = pid_bytes[0];

        if ((strncmp (pid_string, "LITE", 4) == 0) || (strncmp (pid_string, "NITE", 4) == 0))
        {
            /* board_version is a constant value fed to the GPIO2 input value */
            const uint32_t board_version = read_reg32 (mapped_bar, bar_offset + 0x8);

            printf ("Found %.4s Fury at BAR offset 0x%" PRIx64" board_version=%" PRIu32 "\n", pid_string, bar_offset, board_version);

            /* @todo Stop the probe if the Nite-Fury or Lite-Fury PID is found since:
             *       a. None of the Xilinx IP (AXI-GPIO, AXI Quad SPI, XADC Wizard) used in the BAR has any identity registers.
             *       b. Since not all address bits seem to be decoded the pid_string can be found at multiple aliases addresses.
             *       c. Attempting to read from an unimplemented offset can cause the PC to hang.
             *          Re-loading the FGPA didn't help to cause the PC to resume; had to hard power cycle.
             *          Not sure why the PCIe read doesn't fail with a completion timeout.
             */
            return true;
        }
    }

    return false;
}


/**
 * @brief probe the the registers in the DMA bridge of the Xilinx DMA/Bridge Subsystem for PCI Express
 * @details The identification registers checked for are from https://docs.xilinx.com/r/en-US/pg195-pcie-dma/Register-Space
 * @param[in] mapped_bar Start of the memory mapped BAR to prove
 * @param[in] bar_size Size of the memory mapped BAR in bytes to probe
 */
static void probe_xilinx_dma_bridge (const uint8_t *const mapped_bar, const uint64_t bar_size)
{
    const uint64_t register_frame_size = 1 << 9;
    const uint64_t dma_subsystem_identity = 0x1fc;

    /* Enumeration for the channel_target field in the channel identification register */
    enum
    {
        target_h2c_channels = 0,
        target_c2h_channels = 1,
        target_irq_block = 2,
        target_config = 3,
        target_h2c_sgdma = 4,
        target_c2h_sgdma = 5,
        target_sgdma_common = 6,
        target_msi_x = 8 /* Can't be reported as the MSI-X block doesn't have a channel_identification register */
    };

    for (uint64_t bar_offset = 0; (bar_offset + register_frame_size) <= bar_size; bar_offset += register_frame_size)
    {
        const uint32_t channel_identification = read_reg32 (mapped_bar, bar_offset + 0);
        const uint32_t subsystem_identifier = (channel_identification & 0xFFF00000) >> 20;
        const uint32_t channel_target =       (channel_identification & 0x000F0000) >> 16;
        const uint32_t stream =               (channel_identification & 0x00008000) >> 15;
        const uint32_t channel_id_target =    (channel_identification & 0x00000F00) >> 8;
        const uint32_t version =              (channel_identification & 0x000000FF);

        if (subsystem_identifier == dma_subsystem_identity)
        {
            switch (channel_target)
            {
            case target_h2c_channels:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " H2C Channels stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_c2h_channels:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " C2H Channels stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_irq_block:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " IRQ Block version=%" PRIu32 "\n",
                        bar_offset, version);
                break;

            case target_config:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " Config Block version=%" PRIu32 "\n",
                        bar_offset, version);
                break;

            case target_h2c_sgdma:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " H2C SGDMA stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_c2h_sgdma:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " C2H SGDMA stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                        bar_offset,
                        stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                        channel_id_target,
                        version);
                break;

            case target_sgdma_common:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " SGDMA Common version=%" PRIu32 "\n",
                        bar_offset, version);
                break;

            default:
                printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " unknown channel_target=0x%" PRIu32 "\n",
                        bar_offset, channel_target);
                break;
            }
        }
    }
}


/**
 * @brief Probe the memory mapped BARs of a vfio device looking for fixed identifiers for IP
 * @param[in] vfio_device The VFIO device to probe
 */
static void probe_vfio_device_for_xilinx_ip (vfio_device_t *const vfio_device)
{
    bool match;
    for (int bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        const uint8_t *const mapped_bar = vfio_device->mapped_bars[bar_index];
        const uint64_t bar_size = vfio_device->regions_info[bar_index].size;

        if (mapped_bar != NULL)
        {
            printf ("Probing BAR %d in device %s of size 0x%" PRIx64 "\n", bar_index, vfio_device->device_name, bar_size);

            /* Since the "PCIe to AXI Lite Master" in the Nite Fury or Lite Fury can hang the PC when try and
             * read an unimplemented address, only try and probe the next type if not a Nite Fury or Lite Fury. */
            match = probe_nite_fury_or_lite_fury (mapped_bar, bar_size);
            if (!match)
            {
                probe_xilinx_dma_bridge (mapped_bar, bar_size);
            }
        }
    }
}


int main (int argc, char *argv[])
{
    struct pci_access *pacc;
    struct pci_filter filter;
    struct pci_dev *dev;
    int known_fields;
    vfio_devices_t vfio_devices;

    /* Initialise using the defaults */
    pacc = pci_alloc ();
    if (pacc == NULL)
    {
        fprintf (stderr, "pci_alloc() failed\n");
        exit (EXIT_FAILURE);
    }
    pci_init (pacc);

    /* Select to filter by vendor only */
    pci_filter_init (pacc, &filter);
    filter.vendor = FPGA_SIO_VENDOR_ID;

    /* Scan the entire bus */
    pci_scan_bus (pacc);

    /* Open the FPGA devices which have an IOMMU group assigned */
    memset (&vfio_devices, 0, sizeof (vfio_devices));
    const int required_fields = PCI_FILL_IDENT | PCI_FILL_IOMMU_GROUP;
    for (dev = pacc->devices; (dev != NULL) && (vfio_devices.num_devices < MAX_VFIO_DEVICES); dev = dev->next)
    {
        if (pci_filter_match (&filter, dev))
        {
            known_fields = pci_fill_info (dev, required_fields);
            if ((known_fields & required_fields) == required_fields)
            {
                open_vfio_device (&vfio_devices, dev);
            }
        }
    }

    /* Probe the VFIO devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        probe_vfio_device_for_xilinx_ip (&vfio_devices.devices[device_index]);
    }

    close_vfio_devices (&vfio_devices);
    pci_cleanup (pacc);

    return EXIT_SUCCESS;
}
