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


/* Defines the different possible types of Ethernet FCS implementation.
 * The implementations are provided to allow evaluation of their execution time. */
typedef enum
{
    /* Unoptimised C */
    ETHERNET_FCS_IMPLEMENTATION_C,
    /* zlib optimised function */
    ETHERNET_FCS_IMPLEMENTATION_ZLIB,
#ifdef HAVE_INTEL_IPP
    /* Intel oneAPI IPP optimised function.
     * This is enabled at compile time, dependent upon if the IPP is detected as installed by cmake. */
    ETHERNET_FCS_IMPLEMENTATION_IPP,
#endif
    ETHERNET_FCS_IMPLEMENTATION_ARRAY_SIZE,
} ethernet_fcs_implementation_t;


/* Contains the context used to compute the Ethernet FCS */
typedef struct
{
    /* The selected implementation */
    ethernet_fcs_implementation_t implementation;
} ethernet_fcs_context_t;


extern const char *const ethernet_fcs_implementation_names[ETHERNET_FCS_IMPLEMENTATION_ARRAY_SIZE];


void ethernet_fcs_initialise (ethernet_fcs_context_t *const context);
uint32_t ethernet_fcs_calculate (ethernet_fcs_context_t *const context, const void *const frame, const size_t frame_len_bytes);

#endif /* ETHERNET_FCS_H_ */
