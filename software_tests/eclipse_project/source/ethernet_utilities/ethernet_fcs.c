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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <zlib.h>
#ifdef HAVE_INTEL_IPP
#include <ipp.h>
#endif


/* Text to display for the ethernet_fcs_implementation_t enumeration */
const char *const ethernet_fcs_implementation_names[ETHERNET_FCS_IMPLEMENTATION_ARRAY_SIZE] =
{
    [ETHERNET_FCS_IMPLEMENTATION_C] = "C",
    [ETHERNET_FCS_IMPLEMENTATION_ZLIB] = "ZLIB",
#ifdef HAVE_INTEL_IPP
    [ETHERNET_FCS_IMPLEMENTATION_IPP] = "IPP"
#endif
};


/*
 * @brief Initialise the context used in calculating an Ethernet FCS
 * @param[out] context The initialised context
 */
void ethernet_fcs_initialise (ethernet_fcs_context_t *const context)
{
    const char *const implementation_text = getenv ("ETHERNET_FCS_IMPLEMENTATION");

    /* Determine the implementation to use */
    if (implementation_text != NULL)
    {
        /* First priority is selected from the environment variable */
        if (strcmp (implementation_text, ethernet_fcs_implementation_names[ETHERNET_FCS_IMPLEMENTATION_C]) == 0)
        {
            context->implementation = ETHERNET_FCS_IMPLEMENTATION_C;
        }
        else if (strcmp (implementation_text, ethernet_fcs_implementation_names[ETHERNET_FCS_IMPLEMENTATION_ZLIB]) == 0)
        {
            context->implementation = ETHERNET_FCS_IMPLEMENTATION_ZLIB;
        }
#ifdef HAVE_INTEL_IPP
        else if (strcmp (implementation_text, ethernet_fcs_implementation_names[ETHERNET_FCS_IMPLEMENTATION_IPP]) == 0)
        {
            context->implementation = ETHERNET_FCS_IMPLEMENTATION_IPP;
        }
#endif
        else
        {
            fprintf (stderr, "ETHERNET_FCS_IMPLEMENTATION environment variable has unrecognised value \"%s\". Possible values:", implementation_text);
            for (int implementation = 0; implementation < ETHERNET_FCS_IMPLEMENTATION_ARRAY_SIZE; implementation++)
            {
                fprintf (stderr, " %s", ethernet_fcs_implementation_names[implementation]);
            }
            fprintf (stderr, "\n");
            exit (EXIT_FAILURE);
        }
    }
    else
    {
        /* Default to the highest available implementation */
        context->implementation = ETHERNET_FCS_IMPLEMENTATION_ARRAY_SIZE - 1;
    }

#ifdef HAVE_INTEL_IPP
    if (context->implementation == ETHERNET_FCS_IMPLEMENTATION_IPP)
    {
        /* Initialise IPP, checking finds a supported CPU */
        IppStatus ipp_status;

        ipp_status = ippInit ();
        if ((ipp_status != ippStsNoErr) && (ipp_status != ippStsNonIntelCpu))
        {
            fprintf (stderr, "ippInit() returned %d\n", ipp_status);
            exit (EXIT_FAILURE);
        }
    }
#endif

    /* Verify the CRC implementation is working as expected. Also triggers the zlib implementation to initialise the look-up tables */
    const char *const crc32_test_data = "123456789";
    const uint32_t expected_test_crc = 0xCBF43926;
    const uint32_t actual_crc = ethernet_fcs_calculate (context, crc32_test_data, strlen (crc32_test_data));
    if (actual_crc != expected_test_crc)
    {
        fprintf (stderr, "Incorrect CRC 0x%08X for crc32_test_data\n", actual_crc);
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Calculate the FCS for an Ethernet frame
 * @param[in,out] context The context used to control the FCS calculation
 * @param[in] frame The Ethernet frame to calculate the FCS for
 * @param[in] frame_len_bytes The length of the Ethernet frame, exclusive of the FCS
 * @return The FCS for the Ethernet frame
 */
uint32_t ethernet_fcs_calculate (ethernet_fcs_context_t *const context, const void *const frame, const size_t frame_len_bytes)
{
    const uint8_t *const frame_bytes = frame;
    uint32_t fcs;


    switch (context->implementation)
    {
    case ETHERNET_FCS_IMPLEMENTATION_C:
        fcs = UINT32_MAX;
        for (uint32_t byte_index = 0; byte_index < frame_len_bytes; byte_index++)
        {
            fcs = crc (fcs, frame_bytes[byte_index]);
        }

        /* Return the CRC, post-conditioned. */
        fcs = ~fcs;
        break;

    case ETHERNET_FCS_IMPLEMENTATION_ZLIB:
        {
            unsigned long zlib_crc;

            zlib_crc = crc32 (0, Z_NULL, 0);
            zlib_crc = crc32_z (zlib_crc, frame, frame_len_bytes);
            fcs = (uint32_t) zlib_crc;
        }
        break;

#ifdef HAVE_INTEL_IPP
    case ETHERNET_FCS_IMPLEMENTATION_IPP:
        {
            IppStatus ipp_status;

            /* Standard initialisation seed for CRC-32 */
            fcs = 0;

            ipp_status = ippsCRC32_8u (frame, (int) frame_len_bytes, &fcs);
            if (ipp_status != ippStsNoErr)
            {
                fprintf (stderr, "ippsCRC32_8u() returned %d\n", ipp_status);
                exit (EXIT_FAILURE);
            }
        }
        break;
#endif

    default:
        fprintf (stderr, "Unsupported implementation %d\n", context->implementation);
        exit (EXIT_FAILURE);
    }

    return fcs;
}
