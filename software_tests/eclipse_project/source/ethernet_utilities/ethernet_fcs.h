/*
 * @file ethernet_fcs.h
 * @date 8 Nov 2026
 * @author Chester Gillon
 * @brief Defines an interface for calculating the FCS of an Ethernet frame
 */

#ifndef ETHERNET_FCS_H_
#define ETHERNET_FCS_H_

#include <stddef.h>
#include <stdint.h>


/* Contains the context used to compute the Ethernet FCS */
typedef struct
{
    /* This implementation doesn't use a context - placeholder for future implementations which do (e.g. if use a LUT) */
    uint32_t spare;
} ethernet_fcs_context_t;


void ethernet_fcs_initialise (ethernet_fcs_context_t *const context);
uint32_t ethernet_fcs_calculate (const void *const frame, const size_t frame_len_bytes);

#endif /* ETHERNET_FCS_H_ */
