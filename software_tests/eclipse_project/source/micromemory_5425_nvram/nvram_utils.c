/*
 * @file nvram_utils.c
 * @date 26 Mar 2023
 * @author Chester Gillon
 * @brief Provides utilities for using a Micro Memory MM-5425CN NVRAM device
 * @details
 *  In the absence of a description of the device registers and DMA controller, used
 *  https://elixir.bootlin.com/linux/v4.18/source/drivers/block/umem.c as a guide.
 */

#include <string.h>
#include <stdio.h>

#include "nvram_utils.h"

#include <linux/types.h>
#include "umem.h"


/* The PCI commands used for the NVRAM DMA */
#define NVRAM_PCI_WRITE_AND_INVALIDATE 0x0F
#define NVRAM_PCI_CMDS (DMASCR_READMULTI | (NVRAM_PCI_WRITE_AND_INVALIDATE << 24))


/**
 * @brief Get the size in bytes of the NVRAM device
 * @param[in] csr Mapped to the NVRAM CSR
 * @return Returns the size in bytes decoded from a CSR register, or zero if unrecognised.
 */
size_t get_nvram_size_bytes (const uint8_t *const csr)
{
    const uint8_t memory_size_reg = read_reg8 (csr, MEMCTRLSTATUS_MEMORY);
    size_t memory_size_bytes;
    const size_t one_mb = 1024 * 1024;

    switch (memory_size_reg)
    {
    case MEM_128_MB:
        memory_size_bytes = 128 * one_mb;
        break;

    case MEM_256_MB:
        memory_size_bytes = 256 * one_mb;
        break;

    case MEM_512_MB:
        memory_size_bytes = 512 * one_mb;
        break;

    case MEM_1_GB:
        memory_size_bytes = 1024 * one_mb;
        break;

    case MEM_2_GB:
        memory_size_bytes = 2048 * one_mb;
        break;

    default:
        memory_size_bytes = 0;
    }

    return memory_size_bytes;
}


/**
 * @brief Set a LED on the NVRAM board
 * @param[in] csr Mapped to the NVRAM CSR
 * @param[in] shift Identifies which led to set the state for
 * @param[in] state The state of the led to set
 */
void set_led (uint8_t *const csr, int shift, unsigned char state)
{
    uint8_t led;

    led = read_reg8 (csr, MEMCTRLCMD_LEDCTRL);
    if (state == LED_FLIP)
    {
        led ^= (uint8_t) (1<<shift);
    }
    else
    {
        led &= (uint8_t) (~(0x03 << shift));
        led |= (uint8_t) (state << shift);
    }
    write_reg8 (csr, MEMCTRLCMD_LEDCTRL, led);
}


/**
 * @brief Perform initialisation for the NVRAM device, to be able to access the NVRAM.
 * @details Doesn't need to set "Memory Write and Invalidate" in the PCI_COMMAND register as "N/A to PCIe".
 *
 *          The original version of this function only wrote to MEMCTRLCMD_ERRCTRL if the value wasn't already EDC_STORE_CORRECT.
 *          However in that case the NVRAM device was only usable on the first boot into Linux after the PC was powered on.
 *          On the first boot could run programs multiple times which used the NVRAM device. However, if the PC was rebooted
 *          then for subsequent attempts to use the NVRAM device:
 *          a. If using DMA to access the memory region the DMA didn't complete.
 *          b. If using PIO the PC could hang when attempted to access the memory region, requiring to be power cycled.
 * @param[in] csr Mapped to the NVRAM CSR
 */
void initialise_nvram_device (uint8_t *const csr)
{
    /* Ensure ECC is enabled */
    if (read_reg8 (csr, MEMCTRLCMD_ERRCTRL) != EDC_STORE_CORRECT)
    {
        printf ("Enabled ECC for NVRAM\n");
        write_reg8 (csr, MEMCTRLCMD_ERRCTRL, EDC_STORE_CORRECT);
    }
    else
    {
        printf ("Re-enabled ECC for NVRAM\n");
        write_reg8 (csr, MEMCTRLCMD_ERRCTRL, EDC_STORE_CORRECT);
    }
}


/**
 * @brief Initialise the context for one DMA transfer for the NVRAM device
 * @param[out] context The initialised transfer context, which may be used to perform DMA.
 * @param[in/out] descriptors_mapping Used to allocate a descriptor in host memory for the DMA transfer.
 * @param[in/out] data_mapping Defines the host buffer to be used for the transfer.
 *                             Assumes to cover the entire size of the NVRAM.
 * @param[in] transfer_direction The transfer direction from the point of view of the NVRAM DMA:
 *                               - DMA_READ_FROM_HOST or DMA_WRITE_TO_HOST
 * @return Returns true if the context has been initialised, or false if an error occurred.
 *         An error occurs if can't allocate a descriptor.
 */
bool initialise_nvram_transfer_context (nvram_transfer_context_t *const context,
                                        vfio_dma_mapping_t *const descriptors_mapping,
                                        vfio_dma_mapping_t *const data_mapping,
                                        const int transfer_direction)
{
    /* Allocate a descriptor for the transfer */
    context->descriptor =
            vfio_dma_mapping_allocate_space (descriptors_mapping, sizeof (struct mm_dma_desc), &context->descriptor_iova);
    if (context->descriptor == NULL)
    {
        return false;
    }
    vfio_dma_mapping_align_space (descriptors_mapping);

    /* Set the descriptor to transfer the entire NVRAM contents */
    uint64_t data_iova;
    void *data_buffer = vfio_dma_mapping_allocate_space (data_mapping, data_mapping->buffer.size, &data_iova);

    if (data_buffer == NULL)
    {
        return false;
    }
    memset (context->descriptor, 0, sizeof (*context->descriptor));
    context->descriptor->local_addr = 0; /* Start from first NVRAM address */
    context->descriptor->pci_addr = data_iova;
    context->descriptor->transfer_size = (uint32_t) data_mapping->buffer.size;

    /* Set the semaphore address used to indicate completion to the sem_control_bits within the descriptor */
    context->descriptor->sem_addr = context->descriptor_iova + offsetof (struct mm_dma_desc, sem_control_bits);

    /* Single descriptor in the chain */
    context->descriptor->next_desc_addr = 0;

    /* Set the control bits to be used for the transfer, including the direction.
     * Since poll sem_control_bits for completion, don't set the DMASCR_DMA_COMP_EN or DMASCR_CHAIN_COMP_EN bits, since they
     * cause the NVRAM device to generate interrupts for which no handler has been installed via VFIO. */
    context->descriptor->control_bits = DMASCR_GO | DMASCR_SEM_EN | NVRAM_PCI_CMDS;
    if (transfer_direction == DMA_READ_FROM_HOST)
    {
        context->descriptor->control_bits |= DMASCR_TRANSFER_READ;
    }

    return true;
}


/**
 * @brief Start a DMA transfer in the NVRAM device
 * @param[in] csr Mapped to the NVRAM CSR
 * @param[in/out] context The transfer to start
 */
void start_nvram_dma_transfer (uint8_t *const csr, nvram_transfer_context_t *const context)
{
    /* Write the unused CSR DMA addresses as zero, since these are taken from the descriptor */
    write_split_reg64 (csr, DMA_PCI_ADDR, 0);
    write_split_reg64 (csr, DMA_LOCAL_ADDR, 0);
    write_split_reg64 (csr, DMA_TRANSFER_SIZE, 0);
    write_split_reg64 (csr, DMA_SEMAPHORE_ADDR, 0);

    /* Zero the sem_control_bits in the descriptor to indicate the transfer is not complete.
     * This gets written back by the DMA engine when the transfers completes. */
    __atomic_store_n (&context->descriptor->sem_control_bits, 0, __ATOMIC_RELEASE);

    /* Write the address of the descriptor */
    write_split_reg64 (csr, DMA_DESCRIPTOR_ADDR, context->descriptor_iova);

    /* Start the transfer */
    write_reg32 (csr, DMA_STATUS_CTRL, DMASCR_GO | DMASCR_CHAIN_EN | NVRAM_PCI_CMDS);
}


/**
 * @brief Poll for completion of a DMA transfer using the NVRAM device
 * @param[in] context The transfer to poll for completion
 * @return Returns true if the transfer has completed
 */
bool poll_nvram_dma_transfer_completion (const nvram_transfer_context_t *const context)
{
    const uint64_t sem_control_bits = __atomic_load_n (&context->descriptor->sem_control_bits, __ATOMIC_ACQUIRE);

    return (sem_control_bits & DMASCR_DMA_COMPLETE) != 0;
}
