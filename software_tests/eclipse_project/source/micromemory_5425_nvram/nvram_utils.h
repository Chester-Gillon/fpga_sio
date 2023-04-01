/*
 * @file nvram_utils.h
 * @date 26 Mar 2023
 * @author Chester Gillon
 * @brief Provides utilities for using a Micro Memory MM-5425CN NVRAM device
 */

#ifndef NVRAM_UTILS_H_
#define NVRAM_UTILS_H_

#include <stdint.h>
#include <stddef.h>

#include <linux/types.h>
#include "umem.h"

#include "vfio_access.h"


/* The vendor and device if of the Micro Memory MM-5425CN NVRAM device */
#define NVRAM_VENDOR_ID 0x1332
#define NVRAM_DEVICE_ID 0x5425


/* BAR indices on the Micro Memory MM-5425CN NVRAM device */
#define NVRAM_CSR_BAR_INDEX           0
#define NVRAM_MEMORY_WINDOW_BAR_INDEX 2


/* Contains once DMA transfer for the NVRAM device */
typedef struct
{
    /* The allocated descriptor in the host virtual address space */
    struct mm_dma_desc *descriptor;
    /* The IOVA of the descriptor to pass to the NVRAM device DMA engine */
    uint64_t descriptor_iova;
} nvram_transfer_context_t;


size_t get_nvram_size_bytes (const uint8_t *const csr);
void set_led (uint8_t *const csr, int shift, unsigned char state);
void initialise_nvram_device (uint8_t *const csr);
bool initialise_nvram_transfer_context (nvram_transfer_context_t *const context,
                                        vfio_dma_mapping_t *const descriptors_mapping,
                                        vfio_dma_mapping_t *const data_mapping,
                                        const int transfer_direction);
void start_nvram_dma_transfer (uint8_t *const csr, nvram_transfer_context_t *const context);
bool poll_nvram_dma_transfer_completion (const nvram_transfer_context_t *const context);

#endif /* NVRAM_UTILS_H_ */
