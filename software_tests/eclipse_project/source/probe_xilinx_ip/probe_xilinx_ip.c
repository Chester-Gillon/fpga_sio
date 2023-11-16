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
#include <inttypes.h>

#include <unistd.h>

#include "fpga_sio_pci_ids.h"


/* Optional command line argument to probe only a specific BAR. Default to all BARs */
static uint32_t arg_bar_to_probe;
static bool arg_bar_to_probe_specified;

/* Optional command line argument to specify the start offset for probing a BAR. Defaults to zero */
static uint64_t arg_bar_start_offset;

/* Optional command line argument to specify the end offset for probing a BAR. Defaults to the BAR size */
static uint64_t arg_bar_end_offset;
static bool arg_bar_end_offset_specified;


/**
 * @brief Parse the command line arguments
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "d:b:s:e:";
    int option;
    char junk;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case 'b':
            if ((sscanf (optarg, "%" SCNu32 "%c", &arg_bar_to_probe, &junk) != 1) || (arg_bar_to_probe >= PCI_STD_NUM_BARS))
            {
                printf ("Invalid BAR %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_bar_to_probe_specified = true;
            break;

        case 's':
            if (sscanf (optarg, "%" SCNi64 "%c", &arg_bar_start_offset, &junk) != 1)
            {
                printf ("Invalid BAR start offset %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'e':
            if (sscanf (optarg, "%" SCNi64 "%c", &arg_bar_end_offset, &junk) != 1)
            {
                printf ("Invalid BAR end offset %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_bar_end_offset_specified = true;
            break;

        case '?':
        default:
            printf ("Usage %s [-d <pci_device_location>] [-b <bar_to_probe>] [-s <bar_start_offset>] [-e <bar_end_offset>]\n", argv[0]);
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


/**
 * @details Check if a memory mapped BAR is that of the "PCIe to AXI Lite Master" in the
 *          https://github.com/RHSResearchLLC/NiteFury-and-LiteFury/tree/master/Sample-Projects/Project-0/FPGA project
 *
 *          This is done by checking the fixed value used in a GPIO input register, which is set to a constant input
 *          inside the FPGA.
 * @param[in] mapped_bar Start of the memory mapped BAR to prove
 * @param[in] bar_end_offset Exclusive end offset the memory mapped BAR in bytes to probe
 * @return Returns true when the mapped BAR matches the search
 */
static bool probe_nite_fury_or_lite_fury (const uint8_t *const mapped_bar, const uint64_t bar_end_offset)
{
    const uint64_t register_frame_size = 1 << 9;

    for (uint64_t bar_offset = arg_bar_start_offset;
            (bar_offset + register_frame_size) <= bar_end_offset;
            bar_offset += register_frame_size)
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
             *          Re-loading the FPGA didn't help to cause the PC to resume; had to hard power cycle.
             *          Not sure why the PCIe read doesn't fail with a completion timeout.
             */
            return true;
        }
    }

    return false;
}


/**
 * @brief Write a test value to a 32-bit register and check that can readback the value
 * @param[in/out] The mapped BAR containing the register
 * @param[in] reg_offset The offset to the register to test
 * @param[in] test_value Test value to write to the register
 * @return Returns true if the test value was successfully read back from the register
 */
static bool test_reg32 (uint8_t *const mapped_bar, const uint64_t reg_offset, const uint32_t test_value)
{
    uint32_t readback_value;
    bool success;

    write_reg32 (mapped_bar, reg_offset, test_value);
    readback_value = read_reg32 (mapped_bar, reg_offset);
    success = readback_value == test_value;

    if (!success)
    {
        printf ("reg32 test failed. Wrote 0x%08" PRIx32 " read 0x%08" PRIx32 "\n", test_value, readback_value);
    }

    return success;
}


/**
 * @brief Write a pattern of values to a 32-bit register, checking that can readback the values written
 * @details The PASS/FAIL result is written to the console.
 *          Also displays the register value before starting the test and the final value after the test has completed.
 *          The reason for displaying the test values is to see if the final value is preserved or not when the
 *          program is re-run.
 * @param[in/out] The mapped BAR containing the register
 * @param[in] reg_offset The offset to the register to test
 */
static void test_reg32_pattern (uint8_t *const mapped_bar, const uint64_t reg_offset)
{
    bool success;
    uint32_t bit;
    const uint32_t initial_reg_value = read_reg32 (mapped_bar, reg_offset);

    /* Test all zeros and all ones */
    success =
            test_reg32 (mapped_bar, reg_offset, 0x00000000) &&
            test_reg32 (mapped_bar, reg_offset, 0xffffffff);

    /* Test AA and 55 patterns */
    success = success &&
            test_reg32 (mapped_bar, reg_offset, 0xaaaaaaaa) &&
            test_reg32 (mapped_bar, reg_offset, 0x55555555);

    /* Test walking ones */
    for (bit = 0; success && (bit < 32); bit++)
    {
        success = test_reg32 (mapped_bar, reg_offset, 1U << bit);
    }

    /* Test walking zeros */
    for (bit = 0; success && (bit < 32); bit++)
    {
        success = test_reg32 (mapped_bar, reg_offset, UINT32_MAX ^ (1U << bit));
    }

    const uint32_t final_reg_value = read_reg32 (mapped_bar, reg_offset);

    printf ("  Test of reg32 at offset 0x%" PRIx64 " %s : initial=0x%08" PRIx32 " final=0x%08" PRIx32 "\n",
            reg_offset, success ? "PASS" : "FAIL", initial_reg_value, final_reg_value);
}


/**
 * @brief Write a test value to a 64-bit register and check that can readback the value
 * @param[in/out] The mapped BAR containing the register
 * @param[in] reg_offset The offset to the register to test
 * @param[in] test_value Test value to write to the register
 * @return Returns true if the test value was successfully read back from the register
 */
static bool test_reg64 (uint8_t *const mapped_bar, const uint64_t reg_offset, const uint64_t test_value)
{
    uint64_t readback_value;
    bool success;

    write_split_reg64 (mapped_bar, reg_offset, test_value);
    readback_value = read_split_reg64 (mapped_bar, reg_offset);
    success = readback_value == test_value;

    if (!success)
    {
        printf ("reg64 test failed. Wrote 0x%016" PRIx64 " read 0x%016" PRIx64 "\n", test_value, readback_value);
    }

    return success;
}


/**
 * @brief Write a pattern of values to a 64-bit register, checking that can readback the values written
 * @details The PASS/FAIL result is written to the console.
 *          Also displays the register value before starting the test and the final value after the test has completed.
 *          The reason for displaying the test values is to see if the final value is preserved or not when the
 *          program is re-run.
 * @param[in/out] The mapped BAR containing the register
 * @param[in] reg_offset The offset to the register to test
 */
static void test_reg64_pattern (uint8_t *const mapped_bar, const uint64_t reg_offset)
{
    bool success;
    uint64_t bit;
    const uint64_t initial_reg_value = read_split_reg64 (mapped_bar, reg_offset);

    /* Test all zeros and all ones */
    success =
            test_reg64 (mapped_bar, reg_offset, 0x0000000000000000ULL) &&
            test_reg64 (mapped_bar, reg_offset, 0xffffffffffffffffULL);

    /* Test AA and 55 patterns */
    success = success &&
            test_reg64 (mapped_bar, reg_offset, 0xaaaaaaaaaaaaaaaaULL) &&
            test_reg64 (mapped_bar, reg_offset, 0x5555555555555555ULL);

    /* Test walking ones */
    for (bit = 0; success && (bit < 64); bit++)
    {
        success = test_reg64 (mapped_bar, reg_offset, 1ULL << bit);
    }

    /* Test walking zeros */
    for (bit = 0; success && (bit < 64); bit++)
    {
        success = test_reg64 (mapped_bar, reg_offset, UINT64_MAX ^ (1ULL << bit));
    }

    const uint64_t final_reg_value = read_split_reg64 (mapped_bar, reg_offset);

    printf ("  Test of reg64 at offset 0x%" PRIx64 " %s : initial=0x%016" PRIx64 " final=0x%016" PRIx64 "\n",
            reg_offset, success ? "PASS" : "FAIL", initial_reg_value, final_reg_value);
}


/**
 * @brief probe the the registers in the DMA bridge of the Xilinx DMA/Bridge Subsystem for PCI Express
 * @details The identification registers checked for are from https://docs.xilinx.com/r/en-US/pg195-pcie-dma/Register-Space.
 *          Also performs write/read tests on some registers.
 * @param[in/out] mapped_bar Start of the memory mapped BAR to probe. out as performs a read/write for known registers
 * @param[in] bar_end_offset Exclusive end offset the memory mapped BAR in bytes to probe
 */
static void probe_xilinx_dma_bridge (uint8_t *const mapped_bar, const uint64_t bar_end_offset)
{
    const uint64_t register_frame_size = 1 << 8;
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

    for (uint64_t bar_offset = arg_bar_start_offset;
            (bar_offset + register_frame_size) <= bar_end_offset;
            bar_offset += register_frame_size)
    {
        const uint32_t channel_identification = read_reg32 (mapped_bar, bar_offset + 0);
        const uint32_t subsystem_identifier = (channel_identification & 0xFFF00000) >> 20;
        const uint32_t channel_target =       (channel_identification & 0x000F0000) >> 16;
        const uint32_t stream =               (channel_identification & 0x00008000) >> 15;
        const uint32_t channel_id_target =    (channel_identification & 0x00000F00) >> 8;
        const uint32_t version =              (channel_identification & 0x000000FF);

        const uint32_t channel_alignments = read_reg32 (mapped_bar, bar_offset + 0x4c);
        const uint32_t addr_alignment =
                (channel_alignments & 0x00FF0000) >> 16; /* The byte alignment that the source and destination addresses must
                                                            align to. This value is dependent on configuration parameters.*/
        const uint32_t len_granularity =
                (channel_alignments & 0x0000FF00) >> 8; /* The minimum granularity of DMA transfers in bytes. */
        const uint32_t address_bits =
                (channel_alignments & 0x000000FF); /* The number of address bits configured. */

        if (subsystem_identifier == dma_subsystem_identity)
        {
            const uint32_t channel_addr_bits = (bar_offset & 0x00000F00) >> 8;
            const bool channel_addr_bits_used =
                    (channel_target == target_h2c_channels) || (channel_target == target_c2h_channels) ||
                    (channel_target == target_h2c_sgdma   ) || (channel_target == target_c2h_sgdma   );

            if ((!channel_addr_bits_used) && (channel_addr_bits != 0))
            {
                /* Skip this channel target which is an alias due to it not decoding the channel address bits,
                 * since isn't per-channel */
                continue;
            }

            switch (channel_target)
            {
            case target_h2c_channels:
            case target_c2h_channels:
                {
                    printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " %s Channels stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                            bar_offset,
                            (channel_target == target_h2c_channels) ? "H2C" : "C2H",
                            stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                            channel_id_target,
                            version);
                    printf ("  addr_alignment=%" PRIu32 " len_granularity=%" PRIu32 " address_bits=%" PRIu32 "\n",
                            addr_alignment, len_granularity, address_bits);
                    test_reg32_pattern (mapped_bar, bar_offset + 0x88); /* poll_mode_write_back_address LSB */
                    test_reg64_pattern (mapped_bar, bar_offset + 0x88); /* poll_mode_write_back_address */
                }
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
            case target_c2h_sgdma:
                {
                    printf ("Xilinx DMA bridge at BAR offset 0x%" PRIx64 " %s SGDMA stream=%s channel_id_target=%" PRIu32 " version=%" PRIu32 "\n",
                            bar_offset,
                            (channel_target == target_h2c_sgdma) ? "H2C" : "C2H",
                            stream ? "AXI4-Stream Interface" : "AXI4 Memory Mapped Interface",
                            channel_id_target,
                            version);
                    test_reg32_pattern (mapped_bar, bar_offset + 0x80); /* descriptor_address LSB */
                    test_reg64_pattern (mapped_bar, bar_offset + 0x80); /* descriptor_address */
                }
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
 * @param[in/out] vfio_device The VFIO device to probe
 */
static void probe_vfio_device_for_xilinx_ip (vfio_device_t *const vfio_device)
{
    bool match;
    for (uint32_t bar_index = 0; bar_index < PCI_STD_NUM_BARS; bar_index++)
    {
        if ((!arg_bar_to_probe_specified) || (bar_index == arg_bar_to_probe))
        {
            map_vfio_device_bar_before_use (vfio_device, bar_index);

            uint8_t *const mapped_bar = vfio_device->mapped_bars[bar_index];
            const uint64_t bar_size = vfio_device->regions_info[bar_index].size;
            const bool limit_bar_end_offset = arg_bar_end_offset_specified && (arg_bar_end_offset < bar_size);
            const uint64_t bar_end_offset = limit_bar_end_offset ? arg_bar_end_offset : bar_size;

            if (mapped_bar != NULL)
            {
                if ((arg_bar_start_offset != 0) || (bar_end_offset != bar_size))
                {
                    printf ("Probing part of BAR %d in device %s over range 0x%" PRIx64 "..0x%" PRIx64 "\n",
                            bar_index, vfio_device->device_name, arg_bar_start_offset, bar_end_offset);
                }
                else
                {
                    printf ("Probing BAR %d in device %s of size 0x%" PRIx64 "\n", bar_index, vfio_device->device_name, bar_size);
                }

                /* Since the "PCIe to AXI Lite Master" in the Nite Fury or Lite Fury can hang the PC when try and
                 * read an unimplemented address, only try and probe the next type if not a Nite Fury or Lite Fury. */
                match = probe_nite_fury_or_lite_fury (mapped_bar, bar_end_offset);
                if (!match)
                {
                    probe_xilinx_dma_bridge (mapped_bar, bar_end_offset);
                }
            }
        }
    }
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    parse_command_line_arguments (argc, argv);

    /* Select to filter by vendor only */
    const vfio_pci_device_identity_filter_t filter =
    {
        .vendor_id = FPGA_SIO_VENDOR_ID,
        .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY,
        .enable_bus_master = false
    };

    /* Open the FPGA devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* Probe the VFIO devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        probe_vfio_device_for_xilinx_ip (&vfio_devices.devices[device_index]);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
