/*
 * @file ethernet_fcs.c
 * @date 8 Nov 2026
 * @author Chester Gillon
 * @brief Provides a software implementation for calculating the FCS of an Ethernet frame
 * @details
 *   Written to allow testing of Ethernet in a FPGA where optionally the FCS may be generated or checked by software, rather than
 *   by firmware.
 */

#include "ethernet_fcs.h"

#include "crc32.h"


/*
 * @brief Initialise the context used in calculating an Ethernet FCS
 * @param[out] context The initialised context
 */
void ethernet_fcs_initialise (ethernet_fcs_context_t *const context)
{
    /* Nothing to do for this implementation */
}


/**
 * @brief Calculate the FCS for an Ethernet frame
 * @param[in] frame The Ethernet frame to calculate the FCS for
 * @param[in] frame_len_bytes The length of the Ethernet frame, exclusive of the FCS
 * @return The FCS for the Ethernet frame
 */
uint32_t ethernet_fcs_calculate (const void *const frame, const size_t frame_len_bytes)
{
    const uint8_t *const frame_bytes = frame;
    uint32_t fcs = UINT32_MAX;

    for (uint32_t byte_index = 0; byte_index < frame_len_bytes; byte_index++)
    {
        fcs = crc (fcs, frame_bytes[byte_index]);
    }

    /* Return the CRC, post-conditioned. */
    fcs = ~fcs;

    return fcs;
}
