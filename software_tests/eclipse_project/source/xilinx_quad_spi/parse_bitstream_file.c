/*
 * @file parse_bitstream_file.c
 * @date 28 Aug 2023
 * @author Chester Gillon
 * @brief Executable to allow a bitstream to be parsed from just a local file
 */

#include "xilinx_7_series_bitstream.h"

#include <stdlib.h>
#include <stdio.h>


int main (int argc, char *argv[])
{
    x7_bitstream_context_t bitstream_context;

    if (argc != 2)
    {
        printf ("Usage: %s <bitstream file>\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    x7_bitstream_read_from_file (&bitstream_context, argv[1]);
    x7_bitstream_summarise (&bitstream_context);
    x7_bitstream_free (&bitstream_context);

    return EXIT_SUCCESS;
}
