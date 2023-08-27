/*
 * @file xilinx_7_series_bitstream.c
 * @date  20 Aug 2023
 * @author Chester Gillon
 * @brief Implements a mechanism for handling the bitstreams of Xilinx 7-series devices
 * @details
 * This provides a mechanism for sanity checking the bitstreams, either in SPI flash or in a file.
 *
 * The following was used as a guide for the bitstream layout:
 *    https://docs.xilinx.com/r/en-US/ug470_7Series_Config
 * @todo This initial version only looks at a SPI flash.
 */

#include "xilinx_7_series_bitstream.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/* The Sync Word which marks the start of the configuration frames in Xilinx 7-series devices */
#define X7_BITSTREAM_SYNC_WORD 0xAA995566


/* Lookup table giving names for x7_packet_opcode_t */
static const char *const x7_packet_opcode_names[] =
{
    [X7_PACKET_OPCODE_NOP     ] = "NOP",
    [X7_PACKET_OPCODE_READ    ] = "read",
    [X7_PACKET_OPCODE_WRITE   ] = "write",
    [X7_PACKET_OPCODE_RESERVED] = "reserved"
};

/* Lookup table giving names for x7_packet_type_1_register_t.
 * As there are gaps in the enumerations, the undefined enumerations have NULL for the name */
static const char *const x7_packet_type_1_register_names[] =
{
    [X7_PACKET_TYPE_1_REG_CRC     ] = "CRC",
    [X7_PACKET_TYPE_1_REG_FAR     ] = "FAR",
    [X7_PACKET_TYPE_1_REG_FDRI    ] = "FDRI",
    [X7_PACKET_TYPE_1_REG_FDRO    ] = "FDRO",
    [X7_PACKET_TYPE_1_REG_CMD     ] = "CMD",
    [X7_PACKET_TYPE_1_REG_CTL0    ] = "CTL0",
    [X7_PACKET_TYPE_1_REG_MASK    ] = "MASK",
    [X7_PACKET_TYPE_1_REG_STAT    ] = "STAT",
    [X7_PACKET_TYPE_1_REG_LOUT    ] = "LOUT",
    [X7_PACKET_TYPE_1_REG_COR0    ] = "COR0",
    [X7_PACKET_TYPE_1_REG_MFWR    ] = "MFWR",
    [X7_PACKET_TYPE_1_REG_CBC     ] = "CBC",
    [X7_PACKET_TYPE_1_REG_IDCODE  ] = "IDCODE",
    [X7_PACKET_TYPE_1_REG_AXSS    ] = "AXSS",
    [X7_PACKET_TYPE_1_REG_COR1    ] = "COR1",
    [X7_PACKET_TYPE_1_REG_WBSTAR  ] = "WBSTAR",
    [X7_PACKET_TYPE_1_REG_TIMER   ] = "TIMER",
    [X7_PACKET_TYPE_1_REG_RBCRC_SW] = "RBCRC_SW",
    [X7_PACKET_TYPE_1_REG_BOOTSTS ] = "BOOTSTS",
    [X7_PACKET_TYPE_1_REG_CTL1    ] = "CTL1",
    [X7_PACKET_TYPE_1_REG_BSPI    ] = "BSPI"
};

/* Lookup table giving names for x7_command_register_code_t
 * As there are gaps in the enumerations, the undefined enumerations have NULL for the name */
static const char *const x7_command_register_code_names[] =
{
    [X7_COMMAND_NULL       ] = "NULL",
    [X7_COMMAND_WCFG       ] = "WCFG",
    [X7_COMMAND_MFW        ] = "MFW",
    [X7_COMMAND_DGHIGH_LFRM] = "DGHIGH_LFRM",
    [X7_COMMAND_RCFG       ] = "RCFG",
    [X7_COMMAND_START      ] = "START",
    [X7_COMMAND_RCAP       ] = "RCAP",
    [X7_COMMAND_RCRC       ] = "RCRC",
    [X7_COMMAND_AGHIGH     ] = "AGHIGH",
    [X7_COMMAND_SWITCH     ] = "SWITCH",
    [X7_COMMAND_GRESTORE   ] = "GRESTORE",
    [X7_COMMAND_SHUTDOWN   ] = "SHUTDOWN",
    [X7_COMMAND_GCAPTURE   ] = "GCAPTURE",
    [X7_COMMAND_DESYNC     ] = "DESYNC",
    [X7_COMMAND_RESERVED   ] = "RESERVED",
    [X7_COMMAND_IPROG      ] = "IPROG",
    [X7_COMMAND_CRCC       ] = "CRCC",
    [X7_COMMAND_LTIMER     ] = "LTIMER",
    [X7_COMMAND_BSPI_READ  ] = "BSPI_READ",
    [X7_COMMAND_FALL_EDGE  ] = "FALL_EDGE"
};


/**
 * @brief Get a type 1 packet register name, handling unknown registers
 * @param[in] register_address The register to get the name for
 * @return The name of the register.
 *         If an unknown register returns a pointer to a static buffer valid until the next call to this function,
 */
static const char *x7_bitstream_get_register_name (const x7_packet_type_1_register_t register_address)
{
    const size_t registers_array_len = sizeof (x7_packet_type_1_register_names) / sizeof (x7_packet_type_1_register_names[0]);
    static char unknown[64];

    if ((register_address >= 0) && (register_address < registers_array_len) &&
        (x7_packet_type_1_register_names[register_address] != NULL))
    {
        return x7_packet_type_1_register_names[register_address];
    }
    else
    {
        snprintf (unknown, sizeof (unknown), "unknown (0x%0x)", register_address);

        return unknown;
    }
}


/**
 * @brief Get a command name for a X7_PACKET_TYPE_1_REG_CMD, handling unknown commands
 * @param[in] command_code The command to get the name for
 * @return The name of the command code.
 *         If an unknown command returns a pointer to a static buffer valid until the next call to this function.
 */
static const char *x7_bitstream_get_command_name (const x7_command_register_code_t command_code)
{
    const size_t commands_array_len = sizeof (x7_command_register_code_names) / sizeof (x7_command_register_code_names[0]);
    static char unknown[64];

    if ((command_code >= 0) && (command_code < commands_array_len) &&
        (x7_command_register_code_names[command_code] != NULL))
    {
        return x7_command_register_code_names[command_code];
    }
    else
    {
        snprintf (unknown, sizeof (unknown), "unknown (0x%x)", command_code);

        return unknown;
    }
}


/**
 * @brief Unpack a big-endian 32-bit words from the bitstream
 * @param[in] context The context used for reading the bitstream
 * @param[in] word_index The byte index for the word to unpack.
 * @return The unpacked word
 */
uint32_t x7_bitstream_unpack_word (const x7_bitstream_context_t *const context, const uint32_t word_index)
{
    return (((uint32_t) context->data_buffer[word_index    ]) << 24) |
           (((uint32_t) context->data_buffer[word_index + 1]) << 16) |
           (((uint32_t) context->data_buffer[word_index + 2]) <<  8) |
           ( (uint32_t) context->data_buffer[word_index + 3]       );
}


/**
 * @brief Get the next bitstream configuration word
 * @param[in/out] context The context used for reading the bitstream
 * @param[out] word The configuration word which has been read
 * @return Returns true if read a word, or false if reached the end of the data
 */
static bool x7_bitstream_get_next_word (x7_bitstream_context_t *const context, uint32_t *const word)
{
    /* Check if another word is available in the data buffer */
    if ((context->next_word_index + sizeof (uint32_t)) > context->data_buffer_length)
    {
        if (context->controller != NULL)
        {
            if (context->data_buffer_length == context->controller->flash_size_bytes)
            {
                /* Have read the entire flash, so another word isn't available */
                return false;
            }

            /* Expand the data_buffer by reading another chunk from the flash */
            const uint32_t flash_read_address = context->flash_start_address + context->data_buffer_length;
            const uint32_t remaining_bytes_in_flash = context->controller->flash_size_bytes - flash_read_address;
            const uint32_t chunk_size = 32768;
            const uint32_t bytes_to_read = (chunk_size < remaining_bytes_in_flash) ? chunk_size : remaining_bytes_in_flash;
            const uint32_t new_data_buffer_length = context->data_buffer_length + bytes_to_read;

            context->data_buffer = realloc (context->data_buffer, new_data_buffer_length);
            if (context->data_buffer == NULL)
            {
                snprintf (context->error, sizeof (context->error), "Failed to allocate data_buffer of %u bytes",
                        new_data_buffer_length);
                return false;
            }

            if (!quad_spi_read_flash (context->controller, flash_read_address,
                    bytes_to_read, &context->data_buffer[context->data_buffer_length]))
            {
                return false;
            }
            context->data_buffer_length = new_data_buffer_length;
        }
        else
        {
            /* When reading from a file the entire file is read once, so another word isn't available */
            return false;
        }
    }

    /* Extract the next bit-endian 32-bit configuration word */
    *word = x7_bitstream_unpack_word (context, context->next_word_index);
    context->next_word_index += (uint32_t) sizeof (uint32_t);

    return true;
}


/**
 * @brief Read the data words for a configuration packet, and append the description of the packet.
 * @param[in/out] context The context used for reading the bitstream
 * @param[in] new_packet Defines the packet which has its data words to be read and then appended
 * @return Returns true if read the data words, or false if an error occurred.
 */
static bool x7_bitstream_read_packet_data (x7_bitstream_context_t *const context, const x7_packet_record_t *const new_packet)
{
    uint32_t data_word;

    /* Read the packet data, to ensure the data_buffer is populated and check can read the expected number of words
     * before no more data can be read */
    for (uint32_t word_index = 0; word_index < new_packet->word_count; word_index++)
    {
        if (!x7_bitstream_get_next_word (context, &data_word))
        {
            snprintf (context->error, sizeof (context->error),
                    "Only %u out of %u data words available for packet header_type=%u opcode=%u data_words_offset=%u",
                    word_index, new_packet->word_count, new_packet->header_type, new_packet->opcode, new_packet->data_words_offset);
            return false;
        }
    }

    /* Append the description of the packet, dynamically growing the array as required */
    const uint32_t grow_increment = 64;
    if (context->num_packets == context->packets_allocated_length)
    {
        context->packets_allocated_length += grow_increment;
        context->packets = realloc (context->packets, sizeof (context->packets[0]) * context->packets_allocated_length);
        if (context->packets == NULL)
        {
            snprintf (context->error, sizeof (context->error), "Failed to allocate packets array of %zu bytes",
                    sizeof (context->packets[0]) * context->packets_allocated_length);
            return false;
        }
    }

    context->packets[context->num_packets] = *new_packet;
    context->num_packets++;

    /* Update the bitstream length to include the data just read */
    context->bitstream_length_bytes = context->next_word_index;

    return true;
}


/**
 * @brief Parse a bitstream, by finding the Sync word and reading configuration packets until the end of configuration.
 * @details context->end_of_configuration_seen will be true to indicate the bitstream has been parsed successfully.
 * @param[in/out] context The context used for reading the bitstream
 */
static void x7_bitstream_parse (x7_bitstream_context_t *const context)
{
    /* Initialise to an empty bitstream */
    context->packets = NULL;
    context->num_packets = 0;
    context->packets_allocated_length = 0;
    context->end_of_configuration_seen = false;
    context->bitstream_length_bytes = 0;

    /* Search for the Sync word which marks the start of the configuration frames.
     * This advances a byte at a time, to match the description of the configuration logic which searches for
     * alignment to a 32-bit word boundary.
     *
     * In the SPI flash configuration options there is no description about the number of dummy cycles to be used
     * for a specific flash. Presumably dummy cycles are not an issue due to:
     * a. The bitstream starts with dummy pad words.
     * b. The configuration logic hunts for the sync word, skipping over dummy bytes.
     *
     * Not sure how changes to the SPI data width are handled, perhaps just reads from the start again. */
    uint32_t candidate_sync_word;
    context->sync_word_byte_index = 0;
    context->sync_word_found = false;
    do
    {
        context->next_word_index = context->sync_word_byte_index;
        if (!x7_bitstream_get_next_word (context, &candidate_sync_word))
        {
            snprintf (context->error, sizeof (context->error), "No Sync word found");
            return;
        }
        if (candidate_sync_word == X7_BITSTREAM_SYNC_WORD)
        {
            context->sync_word_found = true;
        }
        else
        {
            context->sync_word_byte_index++;
        }
    } while (!context->sync_word_found);

    /* Parse the bitstream configuration words until see the end of configuration, or the end of the data buffer */
    bool previous_packet_was_type_1 = false;
    uint32_t header_word_index;
    uint32_t configuration_header_word;
    bool parse_complete = false;
    do
    {
        header_word_index = context->next_word_index;
        if (x7_bitstream_get_next_word (context, &configuration_header_word))
        {
            /* Decode the common packet header fields */
            x7_packet_record_t new_packet =
            {
                .header_type = (configuration_header_word & 0xE0000000) >> 29,
                .opcode = (configuration_header_word & 0x18000000) >> 27,
                .data_words_offset = context->next_word_index
            };

            if (context->end_of_configuration_seen)
            {
                /* Once have seen the end of configuration, store any padding NOP's until read a configuration word which
                 * isn't a a NOP. Doesn't validate a configuration word which isn't a NOP since when reading a SPI flash
                 * will likely find an erased word after the NOPs. */
                if ((new_packet.header_type == X7_TYPE_1_PACKET) && (new_packet.opcode == X7_PACKET_OPCODE_NOP) &&
                    (new_packet.word_count == 0))
                {
                    if (!x7_bitstream_read_packet_data (context, &new_packet))
                    {
                        return;
                    }
                }
                else
                {
                    parse_complete = true;
                }
            }
            else
            {
                switch (new_packet.header_type)
                {
                case X7_TYPE_1_PACKET:
                    /* Read the data words */
                    new_packet.register_address = (configuration_header_word & 0x07FFE000) >> 13;
                    new_packet.word_count = (configuration_header_word & 0x000007FF);
                    if (!x7_bitstream_read_packet_data (context, &new_packet))
                    {
                        return;
                    }

                    /* Look for the write of the DESYNC command which indicates the end of the configuration */
                    if ((new_packet.opcode == X7_PACKET_OPCODE_WRITE) && (new_packet.word_count == 1))
                    {
                        const uint32_t reg_data = x7_bitstream_unpack_word (context, new_packet.data_words_offset);

                        if ((new_packet.register_address == X7_PACKET_TYPE_1_REG_CMD) && (reg_data == X7_COMMAND_DESYNC))
                        {
                            context->end_of_configuration_seen = true;
                        }
                    }
                    break;

                case X7_TYPE_2_PACKET:
                    /* UG470 says the Type 2 packet must follow a Type 1 packet */
                    if (!previous_packet_was_type_1)
                    {
                        snprintf (context->error, sizeof (context->error),
                                "Type 2 packet header word %08X at index %u didn't follow a type 1 packet header",
                                configuration_header_word, header_word_index);
                        return;
                    }

                    /* For a Type 2 packet just read the data words.
                     * This library doesn't inspect the contents of the configuration frames */
                    new_packet.word_count = configuration_header_word & 0x07FFFFFF;
                    if (!x7_bitstream_read_packet_data (context, &new_packet))
                    {
                        return;
                    }
                    break;

                default:
                    snprintf (context->error, sizeof (context->error),
                            "Unknown packet type %u in header word %08X at index %u",
                            new_packet.header_type, configuration_header_word, header_word_index);
                    return;
                    break;
                }
            }

            previous_packet_was_type_1 = new_packet.header_type == X7_TYPE_1_PACKET;
        }
        else
        {
            /* Reached the end of the data buffer */
            parse_complete = true;
        }
    } while (!parse_complete);
}


/**
 * @brief Read a bitstream from a SPI flash
 * @param[out] context The context which contains the bitstream, including flags which indicate if successfully read the bitstream.
 * @param[in/out] controller The Quad SPI controller used to read from the flash
 * @param[in] flash_start_address The address to start reading the bitstream from. May be non-zero if reading a multiboot image.
 */
void x7_bitstream_read_from_spi_flash (x7_bitstream_context_t *const context, quad_spi_controller_context_t *const controller,
                                       const uint32_t flash_start_address)
{
    memset (context, 0, sizeof (*context));
    context->controller = controller;
    context->data_buffer = NULL;
    context->data_buffer_length = 0;
    context->flash_start_address = flash_start_address;

    x7_bitstream_parse (context);
}


void x7_bitstream_summarise (const x7_bitstream_context_t *const context)
{
    if (context->end_of_configuration_seen)
    {
        printf ("Successfully parsed bitstream of length %u bytes with %u configuration packets\n",
                context->bitstream_length_bytes, context->num_packets);
    }
    else
    {
        printf ("Error parsing bitstream: %s\n", context->error);
    }

    if (context->controller != NULL)
    {
        printf ("Read %u bytes from SPI flash starting at address %u\n", context->data_buffer_length, context->flash_start_address);
    }

    if (context->sync_word_found)
    {
        printf ("Sync word at byte index 0x%X\n", context->sync_word_byte_index);
    }

    uint32_t packet_index = 0;
    uint32_t num_consecutive_nops = 0;
    while (packet_index < context->num_packets)
    {
        /* Count consecutive NOPs */
        /*
        while ((packet_index < context->num_packets) &&
               (context->packets[packet_index].header_type == X7_TYPE_1_PACKET) &&
               (context->packets[packet_index].opcode == X7_PACKET_OPCODE_NOP))
        {
            num_consecutive_nops++;
            packet_index++;
        }*/

        /* @todo just report raw values, rather than a compressed summary */
        const x7_packet_record_t *const packet = &context->packets[packet_index];

        switch (packet->header_type)
        {
        case X7_TYPE_1_PACKET:
            printf ("  Type 1 packet opcode %s", x7_packet_opcode_names[packet->opcode]);
            if (packet->opcode != X7_PACKET_OPCODE_NOP)
            {
                printf (" register %s",
                        x7_bitstream_get_register_name (packet->register_address));
                if ((packet->opcode == X7_PACKET_OPCODE_WRITE) && (packet->register_address == X7_PACKET_TYPE_1_REG_CMD) &&
                    (packet->word_count == 1))
                {
                    /* Decode the name of the command written */
                    printf (" command %s\n",
                            x7_bitstream_get_command_name (x7_bitstream_unpack_word (context, packet->data_words_offset)));
                }
                else
                {
                    /* Display the raw data words */
                    printf (" words");
                    for (uint32_t word_index = 0; word_index < packet->word_count; word_index++)
                    {
                        printf (" %08X", x7_bitstream_unpack_word (context, packet->data_words_offset + word_index));
                    }
                    printf ("\n");
                }
            }
            else
            {
                printf ("\n");
            }
            break;

        case X7_TYPE_2_PACKET:
            printf ("  Type 2 packet opcode %s word_count %u\n", x7_packet_opcode_names[packet->opcode], packet->word_count);
            break;
        }

        packet_index++;
    }

    if (num_consecutive_nops > 0)
    {
        printf ("  %u trailing NOPs\n", num_consecutive_nops);
    }
}
