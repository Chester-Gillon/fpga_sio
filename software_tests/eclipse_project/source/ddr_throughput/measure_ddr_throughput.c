/*
 * @file measure_ddr_throughput.c
 * @date 12 Jan 2025
 * @author Chester Gillon
 * @brief Measure the DDR throughput which can be achieved using the TEF1001_ddr3_throughput design
 * @details
 *   https://docs.amd.com/r/en-US/pg021_axi_dma describes the AXI DMA controller used for the testing.
 *   which has been configured in:
 *   a. Direct Register Mode (Simple DMA)
 *   b. No support for unaligned transfers
 *
 *   The TEF1001_ddr3_throughput FGPA design and this program were created to investigate the now deleted
 *   https://electronics.stackexchange.com/questions/734984/how-axi-dma-ip-in-xilinx-fpga-works
 *
 *   The following output is produced:
 *     Direction MM2S : MM2S timing for 1 transfers of 8589934592 bytes:
 *       Mean = 5054.658820 (Mbytes/sec)
 *     Direction S2MM : Not tested
 *
 *     Direction MM2S : Not tested
 *     Direction S2MM : DMA failed with DMASR=0x00005011 at start_address=0x0
 *
 *     Direction MM2S : DMA failed with DMASR=0x00001001 at start_address=0x3ffffc0
 *     Direction S2MM : DMA failed with DMASR=0x00005011 at start_address=0x0
 *
 *   I.e. the only test which passes is only when just the MM2S direction is tested.
 *
 *   Of the failure cases:
 *   1. Attempting to test the S2MM causes the DMAIntErr bit to be set in the S2MM_DMASR register. Description of this bit contains:
 *        "When Scatter Gather is disabled, this error is flagged if any error occurs during Memory write or if the incoming packet
 *        is bigger than what is specified in the DMA length register."
 *
 *      The FPGA design uses axi_stream_source_fixed_data which always de-asserts TLAST and will there appear as a packet which is
 *      always bigger than what is specified in the DMA length register.
 *
 *      S2MM fails to complete the first transfer.
 *
 *   2. Not sure why the MM2S direction fails when attempted to be tested at the same time as the S2MM direction.
 *      The halted bit is set without any specific DMA error bit being set. Possibly a bug in how attempt to set both directions
 *      active at the same time, but would first need to stop the S2MM DMA error first to confirm.
 *
 *      MM2S completes only one transfer.
 */

#include "fpga_sio_pci_ids.h"
#include "vfio_access.h"
#include "transfer_timing.h"
#include "axi_dma_interface.h"

#include <stdlib.h>
#include <stdio.h>


/* Total size of the DDR memory to measure the throughput for */
#define DDR_MEMORY_SIZE_BYTES (1ull << 33)

/* Number of bits configured in the AXI DMA length registers, which sets the maximum length of one transfer */
#define AXI_DMA_LENGTH_WIDTH_BITS 26

/* The configured data width of the AXI DMA, which sets the aligned transfer size */
#define AXI_DMA_DATA_WIDTH_BYTES (512 / 8)

/* The maximum number of bytes in one AXI DMA transfer, allowing for the configured size of the length register and alignment constraints */
#define AXI_DMA_MAX_ALIGNED_TRANSFER_SIZE_BYTES ((1u << AXI_DMA_LENGTH_WIDTH_BITS) - AXI_DMA_DATA_WIDTH_BYTES)


/* The context used to perform DMA transfers in one direction */
typedef struct
{
    /* When non-NULL the base of the AXI DMA registers to control the transfers for one direction */
    uint8_t *axi_dma_x2x_regs;
    /* Collects the statistics on the overall transfer throughput */
    transfer_timing_t timing;
    /* The number of remaining bytes to transfer */
    uint64_t remaining_bytes;
    /* When true a transfer has been started, and are waiting for it to complete */
    bool transfer_active;
    /* The start address for the current transfer */
    uint64_t transfer_start_address;
    /* The length of the current transfer */
    uint32_t transfer_length;
    /* Set true when transfers have been abandoned due to a DMA error */
    bool dma_error;
    /* The status register value which caused dma_error to be set */
    uint32_t dma_error_sr;
} axi_dma_x2x_transfer_context_t;


/* The AXI DMA directions which can be tested */
typedef enum
{
    AXI_DMA_DIRECTION_MM2S,
    AXI_DMA_DIRECTION_S2MM,
    AXI_DMA_DIRECTION_ARRAY_SIZE
} axi_dma_direction_t;

static const size_t axi_dma_direction_base_offsets[AXI_DMA_DIRECTION_ARRAY_SIZE] =
{
    [AXI_DMA_DIRECTION_MM2S] = AXI_DMA_MM2S_BASE_OFFSET,
    [AXI_DMA_DIRECTION_S2MM] = AXI_DMA_S2MM_BASE_OFFSET
};

static const char *const axi_dma_direction_names[AXI_DMA_DIRECTION_ARRAY_SIZE] =
{
    [AXI_DMA_DIRECTION_MM2S] = "MM2S",
    [AXI_DMA_DIRECTION_S2MM] = "S2MM"
};


/**
 * @brief Issue a soft-reset of the AXI DMA, and wait for the reset to complete
 * @param[in,out] transfer Transfer direction to issue the soft-reset for
 */
static void reset_axi_dma (axi_dma_x2x_transfer_context_t *const transfer)
{
    uint32_t dmacr;
    uint32_t dmasr;

    dmacr = read_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMACR_OFFSET);
    dmacr |= AXI_DMA_X2X_DMACR_RESET;
    write_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMACR_OFFSET, dmacr);
    do
    {
        dmacr = read_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMACR_OFFSET);
        dmasr = read_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMASR_OFFSET);
    } while (((dmacr & AXI_DMA_X2X_DMACR_RESET) != 0) || ((dmasr & AXI_DMA_X2X_DMASR_HALTED) == 0));
}


/**
 * @brief Sequence the measurement of DDR throughput using one or both DMA directions.
 * @details
 *   As Simple DMA is used, the throughput can be impacted by the delay in the software polling for completion of the
 *   maximum length transfer and starting the next transfer.
 * @param[in,out] Base of the AXI DMA registers
 * @param[in] tested_directions Which direction(s) to test.
 */
static void measure_ddr_throughput (uint8_t *const axi_dma_regs, const bool tested_directions[const AXI_DMA_DIRECTION_ARRAY_SIZE])
{
    axi_dma_direction_t direction;
    axi_dma_x2x_transfer_context_t transfers[AXI_DMA_DIRECTION_ARRAY_SIZE] = {0};
    uint32_t num_tested_directions = 0;
    uint32_t num_completed_directions;
    uint32_t dmasr;
    uint32_t dmacr;

    /* Initialise the directions to be tested */
    for (direction = 0; direction < AXI_DMA_DIRECTION_ARRAY_SIZE; direction++)
    {
        axi_dma_x2x_transfer_context_t *const transfer = &transfers[direction];

        if (tested_directions[direction])
        {
            transfer->axi_dma_x2x_regs = &axi_dma_regs[axi_dma_direction_base_offsets[direction]];
            transfer->remaining_bytes = DDR_MEMORY_SIZE_BYTES;
            transfer->transfer_start_address = 0;
            transfer->transfer_active = false;
            transfer->dma_error = false;
            initialise_transfer_timing (&transfer->timing, axi_dma_direction_names[direction], DDR_MEMORY_SIZE_BYTES);
            reset_axi_dma (transfer);
            num_tested_directions++;
        }
        else
        {
            transfer->axi_dma_x2x_regs = NULL;
        }
    }

    /* Run the transfers for the directions to be tested, timing each direction independently */
    do
    {
        num_completed_directions = 0;
        for (direction = 0; direction < AXI_DMA_DIRECTION_ARRAY_SIZE; direction++)
        {
            axi_dma_x2x_transfer_context_t *const transfer = &transfers[direction];

            if (transfer->axi_dma_x2x_regs != NULL)
            {
                if ((transfer->remaining_bytes == 0) || transfer->dma_error)
                {
                    /* All transfers in this direction have completed or abandoned */
                    num_completed_directions++;
                }
                else if (transfer->transfer_active)
                {
                    /* Poll for completion of an in progress transfer */
                    dmasr = read_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMASR_OFFSET);
                    if ((dmasr & AXI_DMA_X2X_DMASR_IDLE) != 0)
                    {
                        /* The transfer has completed */
                        transfer->transfer_start_address += transfer->transfer_length;
                        transfer->remaining_bytes -= transfer->transfer_length;
                        transfer->transfer_active = false;

                        if (transfer->remaining_bytes == 0)
                        {
                            transfer_time_stop (&transfer->timing);
                        }
                    }
                    else if (((dmasr & AXI_DMA_X2X_DMASR_HALTED) != 0) &&
                             ((dmasr * (AXI_DMA_X2X_DMASR_DMAINTERR | AXI_DMA_X2X_DMASR_DMASLVERR | AXI_DMA_X2X_DMASR_DMADECERR)) != 0))
                    {
                        /* DMA has failed if halted bit and zero or more error bits set */
                        transfer->dma_error = true;
                        transfer->dma_error_sr = dmasr;
                    }
                }
                else
                {
                    /* Start the next transfer */
                    if (transfer->remaining_bytes == DDR_MEMORY_SIZE_BYTES)
                    {
                        transfer_time_start (&transfer->timing);
                    }

                    /* Calculate the number of bytes for the transfer */
                    transfer->transfer_length = (uint32_t) ((transfer->remaining_bytes < AXI_DMA_MAX_ALIGNED_TRANSFER_SIZE_BYTES) ?
                            transfer->remaining_bytes : AXI_DMA_MAX_ALIGNED_TRANSFER_SIZE_BYTES);

                    /* Set the run bit to start DMA operations */
                    dmacr = read_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMACR_OFFSET);
                    dmacr |= AXI_DMA_X2X_DMACR_RS;
                    write_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_DMACR_OFFSET, dmacr);

                    /* Set the starting memory address */
                    write_split_reg64 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_SA_OFFSET, transfer->transfer_start_address);

                    /* Write the transfer register last, which starts the transfer */
                    write_reg32 (transfer->axi_dma_x2x_regs, AXI_DMA_X2X_LENGTH_OFFSET, transfer->transfer_length);
                    transfer->transfer_active = true;
                }
            }
        }
    }
    while (num_completed_directions < num_tested_directions);

    /* Report test results */
    for (direction = 0; direction < AXI_DMA_DIRECTION_ARRAY_SIZE; direction++)
    {
        axi_dma_x2x_transfer_context_t *const transfer = &transfers[direction];

        printf ("Direction %s : ", axi_dma_direction_names[direction]);
        if (transfer->axi_dma_x2x_regs == NULL)
        {
            printf ("Not tested\n");
        }
        else if (transfer->dma_error)
        {
            printf ("DMA failed with DMASR=0x%08x at start_address=0x%lx\n", transfer->dma_error_sr ,transfer->transfer_start_address);
        }
        else
        {
            display_transfer_timing_statistics (&transfer->timing);
        }
    }

    printf ("\n");
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    /* Filters for the FPGA devices tested */
    const vfio_pci_device_identity_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_TEF1001_DDR3_THROUGHPUT,
            .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    /* Open PCI devices supported by the test */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];
        const uint32_t bar_index = 0;
        const size_t axi_dma_offset = 0x2000;
        const size_t axi_dma_frame_size = 0x2000;
        uint8_t *const axi_dma_regs = map_vfio_registers_block (vfio_device, bar_index, axi_dma_offset, axi_dma_frame_size);

        if (axi_dma_regs != NULL)
        {
            bool tested_directions[AXI_DMA_DIRECTION_ARRAY_SIZE];

            printf ("Testing DDR throughput of device %s\n", vfio_device->device_name);

            tested_directions[AXI_DMA_DIRECTION_MM2S] = true;
            tested_directions[AXI_DMA_DIRECTION_S2MM] = false;
            measure_ddr_throughput (axi_dma_regs, tested_directions);

            tested_directions[AXI_DMA_DIRECTION_MM2S] = false;
            tested_directions[AXI_DMA_DIRECTION_S2MM] = true;
            measure_ddr_throughput (axi_dma_regs, tested_directions);

            tested_directions[AXI_DMA_DIRECTION_MM2S] = true;
            tested_directions[AXI_DMA_DIRECTION_S2MM] = true;
            measure_ddr_throughput (axi_dma_regs, tested_directions);
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
