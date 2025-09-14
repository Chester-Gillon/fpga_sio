/*
 * @file xilinx_bit_to_bin.c
 * @date 14 Jun 2025
 * @author Chester Gillon
 * @brief Convert a bitstream for a Xilinx 7-series from a .bit to .bin format
 * @details
 *  This utility was written since the Vivado Hardware Tools don't support programming a configuration memory device
 *  using a .bit file.
 */

#include "xilinx_7_series_bitstream.h"

#include <stdlib.h>
#include <stdio.h>


int main (int argc, char *argv[])
{
    x7_bitstream_context_t bitstream_context;
    int exit_status = EXIT_FAILURE;

    if (argc != 3)
    {
        printf ("Usage: %s <bit_filename> <bin_filename>\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    const char *const bit_filename = argv[1];
    const char *const bin_filename = argv[2];

    x7_bitstream_read_from_file (&bitstream_context, bit_filename);
    if ((bitstream_context.num_slrs > 0) && (bitstream_context.slrs[bitstream_context.num_slrs - 1].end_of_configuration_seen))
    {
        switch (bitstream_context.file.file_format)
        {
        case X7_BITSTREAM_FILE_FORMAT_BIT:
            {
                /* Simply need to write the data content which follows the .bit header to a .bin file */
                bool success = false;
                FILE *const bin_file = fopen (bin_filename, "wb");

                if (bin_file != NULL)
                {
                    const size_t num_written =
                            fwrite (bitstream_context.data_buffer, 1, bitstream_context.data_buffer_length, bin_file);
                    const int rc = fclose (bin_file);

                    success = (num_written == bitstream_context.data_buffer_length) && (rc == 0);
                }

                if (success)
                {
                    printf ("Wrote %u bytes to %s\n", bitstream_context.data_buffer_length, bin_filename);
                    exit_status = EXIT_SUCCESS;
                }
                else
                {
                    printf ("Failed to create %s\n", bin_filename);
                }
            }
            break;

        case X7_BITSTREAM_FILE_FORMAT_INTEL_HEX:
            printf ("%s is a Intel HEX file, no conversion needed as the Vivado Hardware Tools can program a configuration memory device from this file type\n", bit_filename);
            break;

        case X7_BITSTREAM_FILE_FORMAT_BIN:
            printf ("%s is already a .bin file, no conversion needed\n", bit_filename);
            break;
        }
    }
    else
    {
        printf ("%s is not a valid bitstream\n", bit_filename);
    }

    return exit_status;
}
