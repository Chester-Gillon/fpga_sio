/*
 * @file pex8311.c
 * @date 8 Apr 2023
 * @author Chester Gillon
 * @brief Contains functions to support using a PEX8311 PCI Express-to-Generic Local Bus Bridge for DMA
 * @details Used https://docs.broadcom.com/doc/pex8311-detailed-technical-spec-data_Book-V1Dec2009 as a reference.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pex8311.h"


/* Used to define the list of PEX8311 Local Configuration Space Registers for the purpose of dumping values for debugging.
 * Taken by scraping the table of registers from the PEX8311 databook. */
typedef struct
{
    /* A positive value indicates a 32-bit register, and a negative value indicates a 8-bit register */
    int32_t offset;
    const char *description;
} pex_register_definition_t;

static const pex_register_definition_t pex_lcs_registers[] =
{
    /* Local Configuration Registers */
    {0x00, "LCS_LAS0RR Direct Slave Local Address Space 0 Range"},
    {0x04, "LCS_LAS0BA Direct Slave Local Address Space 0 Local Base Address (Remap)"},
    {0x08, "LCS_MARBR Mode/DMA Arbitration"},
    {-0x0C, "LCS_BIGEND Big/Little Endian Descriptor"},
    {-0x0D, "LCS_LMISC1 Local Miscellaneous Control 1"},
    {-0x0E, "LCS_PROT_AREA Serial EEPROM Write-Protected Address Boundary"},
    {-0x0F, "LCS_LMISC2 Local Miscellaneous Control 2"},
    {0x10, "LCS_EROMRR Direct Slave Expansion ROM Range"},
    {0x14, "LCS_EROMBA Direct Slave Expansion ROM Local Base Address (Remap) and BREQo Control"},
    {0x18, "LCS_LBRD0 Local Address Space 0/Expansion ROM Bus Region Descriptor"},
    {0x1C, "LCS_DMRR Local Range for Direct Master-to-PCI Express"},
    {0x20, "LCS_DMLBAM Local Base Address for Direct Master-to-PCI Express Memory"},
    {0x24, "LCS_DMLBAI Local Base Address for Direct Master-to-PCI Express I/O Configuration"},
    {0x28, "LCS_DMPBAM PCI Express Base Address (Remap) for Direct Master-to-PCI Express Memory"},
    {0x2C, "LCS_DMCFGA PCI Configuration Address for Direct Master-to-PCI Express I/O Configuration"},
    {0xF0, "LCS_LAS1RR Direct Slave Local Address Space 1 Range"},
    {0xF4, "LCS_LAS1BA Direct Slave Local Address Space 1 Local Base Address (Remap)"},
    {0xF8, "LCS_LBRD1 Local Address Space 1 Bus Region Descriptor"},
    {0xFC, "LCS_DMDAC Direct Master PCI Express Dual Address Cycles Upper Address"},
    {0x100, "LCS_PCIARB Internal Arbiter Control"},
    {0x104, "LCS_PABTADR PCI Abort Address"},
    /* Runtime Registers */
    {0x40, "LCS_MBOX0 Mailbox 0"},
    {0x44, "LCS_MBOX1 Mailbox 1"},
    {0x48, "LCS_MBOX2 Mailbox 2"},
    {0x4C, "LCS_MBOX3 Mailbox 3"},
    {0x50, "LCS_MBOX4 Mailbox 4"},
    {0x54, "LCS_MBOX5 Mailbox 5"},
    {0x58, "LCS_MBOX6 Mailbox 6"},
    {0x5C, "LCS_MBOX7 Mailbox 7"},
    {0x60, "LCS_P2LDBELL PCI Express-to-Local Doorbell"},
    {0x64, "LCS_L2PDBELL Local-to-PCI Express Doorbell"},
    {0x68, "LCS_INTCSR Interrupt Control/Status"},
    {0x6C, "LCS_CNTRL Serial EEPROM Control, PCI Command Codes, User I/O Control, and Init Control"},
    {0x70, "LCS_PCIHIDR PCI Hardwired Configuration ID"},
    {0x74, "LCS_PCIHREV PCI Hardwired Revision ID"},
    /* DMA Registers.
     * Used the order for when Ring Management Valid Mode Enable is not set, for debugging block mode DMA.
     * When DMA Ring Mode is used the LCS_DMASIZx, LCS_DMAPADRx and LCS_DMALADRx registers are not updated by DMA transfers  */
    {0x80, "LCS_DMAMODE0 DMA Channel 0 Mode"},
    {0x84, "LCS_DMAPADR0 DMA Channel 0 PCI Express Address"},
    {0x88, "LCS_DMALADR0 DMA Channel 0 Local Address"},
    {0x8C, "LCS_DMASIZ0 DMA Channel 0 Transfer Size (Bytes)"},
    {0x90, "LCS_DMADPR0 DMA Channel 0 Descriptor Pointer"},
    {0x94, "LCS_DMAMODE1 DMA Channel 1 Mode"},
    {0x98, "LCS_DMAPADR1 DMA Channel 1 PCI Express Address"},
    {0x9C, "LCS_DMALADR1 DMA Channel 1 Local Address"},
    {0xA0, "LCS_DMASIZ1 DMA Channel 1 Transfer Size (Bytes)"},
    {0xA4, "LCS_DMADPR1 DMA Channel 1 Descriptor Pointer"},
    {-0xA8, "LCS_DMACSR0 DMA Channel 0 Command/Status"},
    {-0xA9, "LCS_DMACSR1 DMA Channel 1 Command/Status"},
    {0xAC, "LCS_DMAARB DMA Arbitration"},
    {0xB0, "LCS_DMATHR DMA Threshold"},
    {0xB4, "LCS_DMADAC0 DMA Channel 0 PCI Express Dual Address Cycle Upper Address"},
    {0xB8, "LCS_DMADAC1 DMA Channel 1 PCI Express Dual Address Cycle Upper Address"},
    /*  Messaging Queue (I 2 O) Registers */
    {0x30, "LCS_OPQIS Outbound Post Queue Interrupt Status"},
    {0x34, "LCS_OPQIM Outbound Post Queue Interrupt Mask"},
    {0x40, "LCS_IQP Inbound Queue Port"},
    {0x44, "LCS_OQP Outbound Queue Port"},
    {0xC0, "LCS_MQCR Messaging Queue Configuration"},
    {0xC4, "LCS_QBAR Queue Base Address"},
    {0xC8, "LCS_IFHPR Inbound Free Head Pointer"},
    {0xCC, "LCS_IFTPR Inbound Free Tail Pointer"},
    {0xD0, "LCS_IPHPR Inbound Post Head Pointer"},
    {0xD4, "LCS_IPTPR Inbound Post Tail Pointer"},
    {0xD8, "LCS_OFHPR Outbound Free Head Pointer"},
    {0xDC, "LCS_OFTPR Outbound Free Tail Pointer"},
    {0xE0, "LCS_OPHPR Outbound Post Head Pointer"},
    {0xE4, "LCS_OPTPR Outbound Post Tail Pointer"},
    {0xE8, "LCS_QSR Queue Status/Control"}
};

#define PEX_NUM_LCS_REGISTERS (sizeof (pex_lcs_registers) / sizeof (pex_lcs_registers[0]))

/* Used to save the LCS register values and report changes */
static uint32_t pex_current_lcs_register_values[PEX_NUM_LCS_REGISTERS];
static uint32_t pex_previous_lcs_register_values[PEX_NUM_LCS_REGISTERS];
static bool pex_previous_lcs_registers_valid;


/**
 * @brief Can be called to dump the PEX8311 LCS register values for debugging.
 * @details On the first call dumps all register values. On subsequent calls only dumps registers which have changed.
 * @param[in] lcs Mapped to the PCI Express Base Address of the PEX8311 Local Configuration Space registers
 * @param[in] point_of_dump Describes the point at which the register dump is being made
 */
void pex_dump_lcs_registers (const uint8_t *const lcs, const char *const point_of_dump)
{
    uint32_t register_index;

    /* Save the current register values, only reading those defined in the pex_lcs_registers[] array.
     * I.e. avoids trying to read undefined registers. */
    for (register_index = 0; register_index < PEX_NUM_LCS_REGISTERS; register_index++)
    {
        if (pex_lcs_registers[register_index].offset < 0)
        {
            pex_current_lcs_register_values[register_index] = read_reg8 (lcs, -pex_lcs_registers[register_index].offset);
        }
        else
        {
            pex_current_lcs_register_values[register_index] = read_reg32 (lcs, pex_lcs_registers[register_index].offset);
        }
    }

    if (pex_previous_lcs_registers_valid)
    {
        /* Report only registers whose values have changed */
        printf ("PEX8311 LCS changed values following %s:\n", point_of_dump);
        for (register_index = 0; register_index < PEX_NUM_LCS_REGISTERS; register_index++)
        {
            if (pex_current_lcs_register_values[register_index] != pex_previous_lcs_register_values[register_index])
            {
                if (pex_lcs_registers[register_index].offset < 0)
                {
                    printf ("      %02x ->       %02x %s\n",
                            pex_previous_lcs_register_values[register_index], pex_current_lcs_register_values[register_index],
                            pex_lcs_registers[register_index].description);
                }
                else
                {
                    printf ("%08x -> %08x %s\n",
                            pex_previous_lcs_register_values[register_index], pex_current_lcs_register_values[register_index],
                            pex_lcs_registers[register_index].description);
                }
            }
        }
    }
    else
    {
        /* Report all register values */
        printf ("PEX8311 LCS initial register values:\n");
        printf ("  Value  Offset Description\n");
        for (register_index = 0; register_index < PEX_NUM_LCS_REGISTERS; register_index++)
        {
            if (pex_lcs_registers[register_index].offset < 0)
            {
                printf ("      %02x   %03x   %s\n",
                        pex_current_lcs_register_values[register_index],
                        -pex_lcs_registers[register_index].offset, pex_lcs_registers[register_index].description);
            }
            else
            {
                printf ("%08x   %03x   %s\n",
                        pex_current_lcs_register_values[register_index],
                        pex_lcs_registers[register_index].offset, pex_lcs_registers[register_index].description);
            }
        }
    }
    printf ("\n");

    /* Copy current values to previous for use in next call */
    memcpy (pex_previous_lcs_register_values, pex_current_lcs_register_values, sizeof (pex_previous_lcs_register_values));
    pex_previous_lcs_registers_valid = true;
}

/*
 * @brief Check that VFIO DMA constraints for use with the PEX8311 are satisfied.
 * @details This file has been written to only program the PEX8311 to support DMA access below the 4-GB Address Boundary space.
 *
 *          Scatter/Gather DMA using Ring Management DMA (Valid Mode) is used to minimise access to device registers to
 *          start DMA / check for completion.
 *
 *          While the descriptors in host memory can be configured to address memory above the 4-GB Address Boundary space,
 *          section "9.5.5.1 Scatter/Gather DMA PCI Express Long Address Format" of the databook says
 *             "Ensure that descriptor blocks reside below the 4-GB Address Boundary space."
 *
 *          Changing to use DMA Block Mode, which doesn't use descriptors in host memory, would avoid this constraint.
 * @param[in] mapping the VFIO DMA mapping to validate
 */
void pex_check_iova_constraints (const vfio_dma_mapping_t *const mapping)
{
    const uint64_t iova_end_address = mapping->iova + mapping->buffer.size - 1;
    const uint64_t pex8311_max_iova = 0x100000000ULL;

    if (iova_end_address >= pex8311_max_iova)
    {
        printf ("To support PEX8311 DMA IOVA must be below the 4-GB Address Boundary space\n");
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Get the bus parameters to be used for DMA
 * @details DMA uses the same local bus parameters as in use for memory mapped access.
 *          Where the PEX8311 EEPROM initialises LCS_LBRD0 and LCS_LBRD1, but not the LCS_DMAMODE0 nor LCS_DMAMODE1 registers.
 *
 *          Since the assumption is that either DMA channel can be used to address either local address space, check
 *          both address spaces have the same bus parameters.
 * @param[in] lcs Mapped to the PCI Express Base Address of the PEX8311 Local Configuration Space registers
 * @return The bus parameters to be used for DMA
 */
static uint32_t pex_get_dma_bus_parameters (const uint8_t *const lcs)
{
    const uint32_t local_address_space0_bus_parameters = read_reg32 (lcs, PEX_LCS_LBRD0) & PEX_LCS_LBRDx_BUS_PARAMETERS_MASK;
    const uint32_t local_address_space1_bus_parameters = read_reg32 (lcs, PEX_LCS_LBRD1) & PEX_LCS_LBRDx_BUS_PARAMETERS_MASK;
    if (local_address_space0_bus_parameters != local_address_space1_bus_parameters)
    {
        printf ("To support PEX8311 both local address spaces must have the same bus parameters, got 0x%02x 0x%02x\n",
                local_address_space0_bus_parameters, local_address_space1_bus_parameters);
        exit (EXIT_FAILURE);
    }

    return local_address_space0_bus_parameters;
}


/**
 * @brief Initialise one DMA channel of the the PEX8311
 * @details This function doesn't provide flexibility for the arguments to select all possible PEX8311 DMA options,
 *          but has been written around a specific use case as described in the comments.
 * @param[out] ring The initialised ring
 * @param[in/out] lcs Mapped to the PCI Express Base Address of the PEX8311 Local Configuration Space registers
 * @param[in] dma_channel Which DMA channel (0 or 1) to initialise the ring for
 * @param[in] num_descriptors The number of descriptors to create in the ring
 * @param[in/out] mapping Used to allocate space for the descriptors
 */
void pex_initialise_dma_ring (pex_dma_ring_context_t *const ring,
                              uint8_t *const lcs,
                              const uint32_t dma_channel,
                              const uint32_t num_descriptors,
                              vfio_dma_mapping_t *const mapping)
{
    uint64_t descriptors_start_iova;

    vfio_dma_mapping_align_space (mapping);

    /* Abort any transfer left over from previous use. Will be enabled when the DMA is actually started. */
    ring->lcs = lcs;
    ring->dmacsr_offset = (dma_channel == 0) ? PEX_LCS_DMACSR0 : PEX_LCS_DMACSR1;
    write_reg8 (ring->lcs, ring->dmacsr_offset, PEX_LCS_DMACSRx_ABORT);

    /* Allocate an array of descriptors and link them into a ring.
     *
     * PEX_DESC_PTR_END_OF_CHAIN is not set as the PEX8311 never see the end of chain, since are using a continuous ring
     * and starting part of the ring of descriptors using the valid mode flag.
     *
     * PEX_DESC_PTR_INTERRUPT_AFTER_TERMINAL_COUNT is not set as poll for completion rather than use interrupts.
     */
    ring->num_descriptors = num_descriptors;
    ring->descriptors = vfio_dma_mapping_allocate_space (mapping,
            ring->num_descriptors * sizeof (pex_ring_dma_descriptor_short_format_t),
            &descriptors_start_iova);
    if (ring->descriptors == NULL)
    {
        exit (EXIT_FAILURE);
    }
    for (uint32_t descriptor_index = 0; descriptor_index < num_descriptors; descriptor_index++)
    {
        pex_ring_dma_descriptor_short_format_t *const descriptor = &ring->descriptors[descriptor_index];
        const uint32_t next_descriptor_index = (descriptor_index + 1) % ring->num_descriptors;
        const uint32_t next_descriptor_iova =
                (uint32_t) (descriptors_start_iova + (next_descriptor_index * sizeof (pex_ring_dma_descriptor_short_format_t)));

        /* The following fields are populated later when the descriptor is actually used */
        descriptor->transfer_size_bytes = 0;
        descriptor->pci_express_address_low = 0;
        descriptor->first_local_address = 0;

        /* Populate the next descriptor address an mark as in PCIe address space.
         * The PEX_DESC_PTR_DIRECTION_MASK may be changed when the descriptor is actually used. */
        descriptor->next_descriptor_address = next_descriptor_iova | PEX_LCS_DMADPRx_LOCATION_PCI_EXPRESS_ADDRESS_SPACE;
    }

    /* The ring is initially empty */
    ring->host_descriptor_index = 0;
    ring->dma_descriptor_index = 0;
    ring->num_in_use_descriptors = 0;

    const uint32_t dma_bus_parameters = pex_get_dma_bus_parameters (ring->lcs);

    /* The rational for the DMA mode control is:
     * - Copy the bus parameter fields from that used by memory mapping.
     * - Bursting, demand mode, EOT are not enabled as are performing single byte transfers.
     * - Scatter/gather (descriptors in host memory) is used rather than block mode (only uses DMA registers).
     * - Interrupts are not enabled, as perform polling to check for completion.
     * - The local address is held constant since performing multiple transfers to the same local register in any single
     *   descriptor.
     * - Clear count mode is enabled to allow the "Ring Management Valid bit" to clear at the completion of each descriptor.
     * - DAC chain mode is not enabled as are using 32-bit addresses.
     * - Valid Mode is enabled so the DMA only processes descriptors with the Valid bit set.
     * - The scatter/gather controller is set to stop polling when reaches a Valid bit clear, to avoid generating continuous
     *   bus transfers.
     */
    const uint32_t dma_mode = dma_bus_parameters |
            PEX_LCS_DMAMODEx_SCATTER_GATHER_MODE |
            PEX_LCS_DMAMODEx_LOCAL_ADDRESSING_MODE_CONSTANT |
            PEX_LCS_DMAMODEx_CLEAR_COUNT_MODE |
            PEX_LCS_DMAMODEx_RING_MANAGEMENT_VALID_MODE_ENABLE |
            PEX_LCS_DMAMODEx_RING_MANAGEMENT_VALID_STOP_CONTROL_STOPS;

    write_reg32 (ring->lcs, (dma_channel == 0) ? PEX_LCS_DMAMODE0 : PEX_LCS_DMAMODE1, dma_mode);

    /* Set the address of the first descriptor in the ring */
    write_reg32 (ring->lcs, (dma_channel == 0) ? PEX_LCS_DMADPR0 : PEX_LCS_DMADPR1,
            (uint32_t) descriptors_start_iova | PEX_LCS_DMADPRx_LOCATION_PCI_EXPRESS_ADDRESS_SPACE);
}


/*
 * @brief Update the next host descriptor in a DMA ring for a channel, for a transfer which will be started later.
 * @details Assumes called when no DMA is in progress for the channel, so no need to control the order in which fields are changed.
 * @param[in/out] ring The DMA ring to update the descriptor in.
 * @param[in] transfer_size_bytes The transfer size in bytes
 * @param[in] pci_express_address_low The starting IOVA address as seen by the DMA device for the host memory for the transfer.
 * @param[in] first_local_address The starting local bus address for the transfer
 * @param[in] direction PEX_LCS_DMADPRx_DIRECTION_PCI_TO_LOCAL or PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI
 */
void pex_update_descriptor_in_ring (pex_dma_ring_context_t *const ring,
                                    const uint32_t transfer_size_bytes,
                                    const uint32_t pci_express_address_low,
                                    const uint32_t first_local_address,
                                    const uint32_t direction)
{
    pex_ring_dma_descriptor_short_format_t *const descriptor = &ring->descriptors[ring->host_descriptor_index];

    descriptor->transfer_size_bytes = transfer_size_bytes | PEX_XFER_SIZE_RING_MANAGEMENT_VALID;
    descriptor->pci_express_address_low = pci_express_address_low;
    descriptor->first_local_address = first_local_address;
    descriptor->next_descriptor_address &= ~PEX_LCS_DMADPRx_DIRECTION_MASK;
    descriptor->next_descriptor_address |= direction;

    ring->host_descriptor_index = (ring->host_descriptor_index + 1) % ring->num_descriptors;
    ring->num_in_use_descriptors++;
}


/**
 * @brief Start the DMA ring transferring the descriptors updated by proceeding calls to pex_update_descriptor_in_ring()
 * @param[in/out] ring The DMA ring to start transfers for
 */
void pex_start_dma_ring (pex_dma_ring_context_t *const ring)
{
    write_reg32 (ring->lcs, ring->dmacsr_offset, PEX_LCS_DMACSRx_ENABLE | PEX_LCS_DMACSRx_START);
}


/**
 * @brief Poll a DMA ring to see if the transfer started by a call to pex_start_dma_ring() has completed
 * @details The poll is done by looking at the descriptors in host memory, rather than DMA channel registers
 * @param[in/out] ring The DMA ring to poll for completion for
 * @return Returns true if the transfer has completed, or false if in progress
 */
bool pex_poll_dma_ring_completion (pex_dma_ring_context_t *const ring)
{
    uint32_t transfer_size_bytes;
    bool descriptor_complete;

    if (ring->num_in_use_descriptors > 0)
    {
        /* Look for descriptors which have completed, as indicated by the DMA channel clearing the Valid flag in the
         * transfer size field. */
        do
        {
            transfer_size_bytes =
                    __atomic_load_n (&ring->descriptors[ring->dma_descriptor_index].transfer_size_bytes, __ATOMIC_ACQUIRE);
            descriptor_complete = (transfer_size_bytes & PEX_XFER_SIZE_RING_MANAGEMENT_VALID) == 0;
            if (descriptor_complete)
            {
                ring->dma_descriptor_index = (ring->dma_descriptor_index + 1) % ring->num_descriptors;
                ring->num_in_use_descriptors--;
            }
        } while (descriptor_complete && (ring->num_in_use_descriptors > 0));
    }

    return ring->num_in_use_descriptors == 0;
}


void pex_initialise_dma_block (pex_dma_block_context_t *const block,
                               uint8_t *const lcs,
                               const uint32_t dma_channel)
{
    block->lcs = lcs;
    if (dma_channel == 0)
    {
        block->dmacsr_offset = PEX_LCS_DMACSR0;
        block->dmapadr_offset = PEX_LCS_DMAPADR0;
        block->dmaladr_offset = PEX_LCS_DMALADR0;
        block->dmasiz_offset = PEX_LCS_DMASIZ0;
        block->dmadac_offset = PEX_LCS_DMADAC0;
        block->dmadpr_offset = PEX_LCS_DMADPR0;
    }
    else
    {
        block->dmacsr_offset = PEX_LCS_DMACSR1;
        block->dmapadr_offset = PEX_LCS_DMAPADR1;
        block->dmaladr_offset = PEX_LCS_DMALADR1;
        block->dmasiz_offset = PEX_LCS_DMASIZ1;
        block->dmadac_offset = PEX_LCS_DMADAC1;
        block->dmadpr_offset = PEX_LCS_DMADPR1;
    }

    /* Abort any transfer left over from previous use. Will be enabled when the DMA is actually started. */
    write_reg8 (block->lcs, block->dmacsr_offset, PEX_LCS_DMACSRx_ABORT);

    /* Set the DMA mode. Rationale is:
     * - Copy the bus parameter fields from that used by memory mapping.
     * - Bursting, demand mode, EOT are not enabled as are performing single byte transfers.
     * - Interrupts are not enabled, as perform polling to check for completion.
     * - The local address is held constant since performing multiple transfers to the same local register in any single
     *   descriptor.
     * - Scatter/gather descriptors are not enabled, since using block mode.
     */
    const uint32_t dma_bus_parameters = pex_get_dma_bus_parameters (block->lcs);
    const uint32_t dma_mode = dma_bus_parameters |
            PEX_LCS_DMAMODEx_LOCAL_ADDRESSING_MODE_CONSTANT;
    write_reg32 (block->lcs, (dma_channel == 0) ? PEX_LCS_DMAMODE0 : PEX_LCS_DMAMODE1, dma_mode);
}


void pex_start_dma_block (pex_dma_block_context_t *const block,
                          const uint32_t transfer_size_bytes,
                          const uint64_t pci_express_address,
                          const uint32_t first_local_address,
                          const uint32_t direction)
{
    const uint32_t pci_express_address_low = (uint32_t) (pci_express_address & 0x00000000FFFFFFFFULL);
    const uint32_t pci_express_address_high = (uint32_t) (pci_express_address >> 32);

    write_reg32 (block->lcs, block->dmasiz_offset, transfer_size_bytes);
    write_reg32 (block->lcs, block->dmapadr_offset, pci_express_address_low);
    write_reg32 (block->lcs, block->dmadac_offset, pci_express_address_high);
    write_reg32 (block->lcs, block->dmaladr_offset, first_local_address);
    write_reg32 (block->lcs, block->dmadpr_offset, direction);

    write_reg32 (block->lcs, block->dmacsr_offset, PEX_LCS_DMACSRx_ENABLE | PEX_LCS_DMACSRx_START);
}


bool pex_poll_dma_block_completion (pex_dma_block_context_t *const block)
{
    const uint8_t csr_value = read_reg8 (block->lcs, block->dmacsr_offset);

    return (csr_value & PEX_LCS_DMACSRx_DONE) != 0;
}
