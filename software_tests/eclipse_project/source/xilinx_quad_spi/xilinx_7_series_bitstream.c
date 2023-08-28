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
 */

#include "xilinx_7_series_bitstream.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


/* The Sync Word which marks the start of the configuration frames in Xilinx 7-series devices */
#define X7_BITSTREAM_SYNC_WORD 0xAA995566


/* The fixed header at the start of a Xilinx .bit file.
 * Couldn't find an official documentation, taken from a hex dump. */
static const uint8_t x7_bit_file_fixed_header[] =
{
    0x00, 0x09, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x00, 0x00, 0x01
};


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


/**
 * @brief Get some bytes from the header of a .bit format file, checking don't try and read off the end of the file
 * @param[in/out] context Contains the header of the .bit format file
 * @param[in/out] raw_file_offset Offset in the file to advance through the header
 * @param[in] num_bytes The number of bytes to get
 * @return The pointer to the bytes from the header, or NULL if attempt to read off the end of the file
 */
static const void *x7_bitstream_get_bit_header_bytes (x7_bitstream_context_t *const context,
                                                      uint32_t *const raw_file_offset,
                                                      const uint32_t num_bytes)
{
    if (((*raw_file_offset) + num_bytes) > context->file.raw_length)
    {
        snprintf (context->error, sizeof (context->error), "Attempt to read bit header off end of file %s",
                context->file.pathname);
        return NULL;
    }

    const void *const header_bytes = &context->file.raw_contents[*raw_file_offset];
    (*raw_file_offset) += num_bytes;

    return header_bytes;
}


/**
 * @brief Read one field ID from the .bit format file, checking the expected field
 * @param[in/out] context Contains the header of the .bit format file
 * @param[in/out] raw_file_offset Offset in the file to advance through the header
 * @param[in] expected_field_id The expected field identifier character
 * @return Returns true if read expected_field_id, or false if an error.
 */
static bool x7_bitstream_bit_header_field_id (x7_bitstream_context_t *const context,
                                              uint32_t *const raw_file_offset,
                                              const char expected_field_id)
{
    /* Read the character which defines the field identity */
    const char *const actual_field_id = x7_bitstream_get_bit_header_bytes (context, raw_file_offset, 1);
    if (actual_field_id == NULL)
    {
        return false;
    }
    if (*actual_field_id != expected_field_id)
    {
        snprintf (context->error, sizeof (context->error),
                "Read 0x%0x rather than expected field id %c from bit header in %s",
                *actual_field_id, expected_field_id, context->file.pathname);
        return false;
    }

    return true;
}


/**
 * @brief Extract one variable length string field from the header of a .bit format file.
 * @param[in/out] context Contains the header of the .bit format file
 * @param[in/out] raw_file_offset Offset in the file to advance through the header
 * @param[in] expected_field_id The expected field identifier character
 * @param[out] header_string Set to point at the string in the file header
 * @return Returns true if read the string, or false if an error.
 */
static bool x7_bitstream_extract_bit_header_string (x7_bitstream_context_t *const context,
                                                    uint32_t *const raw_file_offset,
                                                    const char expected_field_id,
                                                    const char **const header_string)
{
    if (!x7_bitstream_bit_header_field_id (context, raw_file_offset, expected_field_id))
    {
        return false;
    }

    /* Read the big-endian 16-bit string length */
    const uint8_t *const string_len_bytes = x7_bitstream_get_bit_header_bytes (context, raw_file_offset, sizeof (uint16_t));
    if (string_len_bytes == NULL)
    {
        return false;
    }
    const uint32_t string_len = ((uint32_t) string_len_bytes[0] << 8) | string_len_bytes[1];

    /* Read the header string which should be null terminated */
    if (string_len == 0)
    {
        snprintf (context->error, sizeof (context->error), "Empty string for field_id %c in bit header in %s",
                expected_field_id, context->file.pathname);
        return false;
    }
    *header_string = x7_bitstream_get_bit_header_bytes (context, raw_file_offset, string_len);
    if ((*header_string)[string_len - 1] != '\0')
    {
        snprintf (context->error, sizeof (context->error),
                "String for field_id %c in bit header is not null-terminated in %s",
                expected_field_id, context->file.pathname);
        return false;
    }

    return true;
}


/**
 * @brief Parse the header information from a .bit format file, and if successful setup the data_buffer to parse the bitstream.
 * @details Having been unable to locate Xilinx documentation for the header format in a .bit file,
 *          used http://www.fpga-faq.com/FAQ_Pages/0026_Tell_me_about_bit_files.htm as a guide.
 *
 *          The assumption is that the fields are always present, rather than being optional and having to scan the
 *          available field IDs.
 * @param[in/out] context Contains the header of the .bit format file
 * @return Returns true parsed the header, or false if an error.
 */
static bool x7_bitstream_parse_bit_file_header (x7_bitstream_context_t *const context)
{
    bool header_valid;
    uint32_t raw_file_offset;

    /* Caller has already verified the fixed header, so skip it */
    raw_file_offset = sizeof (x7_bit_file_fixed_header);

    /* Read the strings from the file header */
    header_valid =
            x7_bitstream_extract_bit_header_string (context, &raw_file_offset, 'a', &context->file.design_name) &&
            x7_bitstream_extract_bit_header_string (context, &raw_file_offset, 'b', &context->file.part_name) &&
            x7_bitstream_extract_bit_header_string (context, &raw_file_offset, 'c', &context->file.date) &&
            x7_bitstream_extract_bit_header_string (context, &raw_file_offset, 'd', &context->file.time);

    header_valid = header_valid && x7_bitstream_bit_header_field_id (context, &raw_file_offset, 'e');
    if (header_valid)
    {
        const uint8_t *const bitstream_length_bytes =
                x7_bitstream_get_bit_header_bytes (context, &raw_file_offset, sizeof (uint32_t));
        header_valid = bitstream_length_bytes != NULL;

        if (header_valid)
        {
            const uint32_t bitstream_length_from_file_size = context->file.raw_length - raw_file_offset;
            const uint32_t bitstream_length_from_header =
                    (((uint32_t) bitstream_length_bytes[0]) << 24) |
                    (((uint32_t) bitstream_length_bytes[1]) << 16) |
                    (((uint32_t) bitstream_length_bytes[2]) <<  8) |
                    ( (uint32_t) bitstream_length_bytes[3]       );
            header_valid = bitstream_length_from_file_size == bitstream_length_from_header;
            if (header_valid)
            {
                /* The bitstream follows the bit header in the file */
                context->data_buffer = &context->file.raw_contents[raw_file_offset];
                context->data_buffer_length = bitstream_length_from_header;
            }
            else
            {
                snprintf (context->error, sizeof (context->error),
                        "Bitstream length in bit header is %u bytes, but expected %u bytes of file size for %s",
                        bitstream_length_from_header, bitstream_length_from_file_size, context->file.pathname);
            }
        }
    }

    return header_valid;
}


/**
 * @brief Read a bitstream from a local file on the host
 * @details Handles .bit or .bin format files created by the Xilinx Vivado tools
 * @param[out] context The context which contains the bitstream, including flags which indicate if successfully read the bitstream.
 * @param[in] bitstream_pathname The bitstream file to read.
 */
void x7_bitstream_read_from_file (x7_bitstream_context_t *const context, const char *const bitstream_pathname)
{
    struct stat statbuf;
    int rc;
    FILE *bitstream_file;

    memset (context, 0, sizeof (*context));
    snprintf (context->file.pathname, sizeof (context->file.pathname), "%s", bitstream_pathname);
    context->controller = NULL;

    /* Read the entire contents of the bitstream file into memory.
     * Reject a file which is >= 4GB as too large for a bitstream as for a SPI flash can only support 32-bit addressing. */
    rc = stat (context->file.pathname, &statbuf);
    if (rc != 0)
    {
        snprintf (context->error, sizeof (context->error), "Unable to stat() %s : %s",
                context->file.pathname, strerror (errno));
        return;
    }

    if (statbuf.st_size >= 0x100000000)
    {
        snprintf (context->error, sizeof (context->error), "File size exceeds 32 bit addressing");
        return;
    }
    context->file.raw_length = (uint32_t) statbuf.st_size;

    context->file.raw_contents = malloc (context->file.raw_length);
    if (context->file.raw_contents == NULL)
    {
        snprintf (context->error, sizeof (context->error),
                "Failed to allocate bitstream_file_contents of %u bytes", context->file.raw_length);
        return;
    }

    bitstream_file = fopen (context->file.pathname, "r");
    if (bitstream_file == NULL)
    {
        snprintf (context->error, sizeof (context->error), "Unable to open %s : %s",
                context->file.pathname, strerror (errno));
        return;
    }

    const size_t bytes_read = fread (context->file.raw_contents, 1, context->file.raw_length, bitstream_file);
    if (bytes_read != context->file.raw_length)
    {
        snprintf (context->error, sizeof (context->error), "Only read %zu out of %u bytes from %s : %s",
                bytes_read, context->file.raw_length, context->file.pathname, strerror (errno));
        return;
    }
    (void) fclose (bitstream_file);

    /* Perform simple auto-detect of file format */
    context->file.bit_format_file = (context->file.raw_length > sizeof (x7_bit_file_fixed_header)) &&
            (memcmp (context->file.raw_contents, x7_bit_file_fixed_header, sizeof (x7_bit_file_fixed_header)) == 0);
    if (context->file.bit_format_file)
    {
        if (x7_bitstream_parse_bit_file_header (context))
        {
            x7_bitstream_parse (context);
        }
    }
    else
    {
        /* Assume the file is in .bin format containing the binary contents of the bitstream */
        context->data_buffer = context->file.raw_contents;
        context->data_buffer_length = context->file.raw_length;
        x7_bitstream_parse (context);
    }
}


/**
 * @brief Free the dynamic memory allocated for a bitstream
 * @param[in/out] context The context for free memory for
 */
void x7_bitstream_free (x7_bitstream_context_t *const context)
{
    if (context->controller != NULL)
    {
        free (context->data_buffer);
        context->data_buffer = NULL;
    }
    else
    {
        free (context->file.raw_contents);
        context->file.raw_contents = NULL;
        context->file.design_name = NULL;
        context->file.part_name = NULL;
        context->file.date = NULL;
        context->file.time = NULL;
    }

    free (context->packets);
    context->packets = NULL;
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
    else
    {
        printf ("Read bitsteam from file %s\n", context->file.pathname);
        if (context->file.bit_format_file)
        {
            printf (".bit format header:\n");
            printf ("  design_name=%s\n", context->file.design_name);
            printf ("  part_name=%s\n", context->file.part_name);
            printf ("  date=%s\n", context->file.date);
            printf ("  time=%s\n", context->file.time);
        }
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
