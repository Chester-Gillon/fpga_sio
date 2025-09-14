/*
 * @file xilinx_7_series_bitstream.c
 * @date  20 Aug 2023
 * @author Chester Gillon
 * @brief Implements a mechanism for handling the bitstreams of Xilinx 7-series devices
 * @details
 * This provides a mechanism for sanity checking the bitstreams, either in SPI flash or in a file.
 *
 * This file was originally written just for Xilinx 7-series devices, using following as a guide for the bitstream layout:
 *    https://docs.amd.com/v/u/en-US/ug470_7Series_Config
 *
 * For the 7-series devices checked bitstreams for 7A200T and 7K160T
 *
 * Subsequently:
 * 1. Added additional support for Xilinx UltraScale and UltraScale+ devices, using the following:
 *      https://docs.amd.com/r/en-US/ug570-ultrascale-configuration
 *
 *    For the UltraScale devices checked a bitstream for a KU060.
 *    @todo The KU060 bitstream had a command code of 0x15, between GRESTORE and DGHIGH_LFRM commands.
 *    UG570 doesn't describe what command code 0x15 is.
 * 2. Added additional support for Zync UltraScale and UltraScale+ devices:
 *    a. The Zynq UltraScale+ Device TRM https://docs.amd.com/v/u/en-US/ug1085-zynq-ultrascale-trm only lists the IDCODES
 *       for the PS (processing system).
 *    b. Application Note: UltraScale and UltraScale+ FPGAs Internal Programming of BBRAM and eFUSEs
 *       https://docs.amd.com/v/u/en-US/xapp1283-internalprogramming-bbram-efuses contains:
 *         "For FPGA devices, the JTAG IDCODE can be found in UG570 [Ref 2]. For Zynq UltraScale+
 *          devices, the JTAG IDCODE in the PL is different than the published IDCODE for the PS. Contact your
 *          local Xilinx FAE or send an e-mail to secure.solutions@xilinx.com to get the appropriate PL JTAG
 *          IDCODE for your Zynq UltraScale+ device."
 *    c. Using a bitstream created for a xczu4ev searched for the raw hex IDCODE in the installation directory
 *       for Vivado 2024.2. This found the data/verilog/src/unisims/JTAG_SIME2.v source file which has the
 *       IDCODE from the xczu4ev bitstream and other Zynq devices
 * 3. Added additional support for accelerator boards.
 *    https://github.com/erinadreno/list_of_Xilinx_FPGAs?tab=readme-ov-file#alveo-and-kria had the list of FPGA for
 *    the different boards, and the JTAG_SIME2.v source file had the IDCODe values.
 *
 */

#include "xilinx_7_series_bitstream.h"
#include "identify_pcie_fpga_design.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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


/* One entry in an array to provide a look-up from enumeration value to name.
 * A NULL enum_name indicates the end of the array. */
#define X7_ENUM_UNKNOWN_STRING_LEN 64
typedef struct
{
    uint32_t value;
    const char *name;
} x7_enum_lut_entry_t;


/* Lookup table giving names for x7_packet_opcode_t */
static const x7_enum_lut_entry_t x7_packet_opcode_names[] =
{
    {.value = X7_PACKET_OPCODE_NOP     , .name = "NOP"},
    {.value = X7_PACKET_OPCODE_READ    , .name = "read"},
    {.value = X7_PACKET_OPCODE_WRITE   , .name = "write"},
    {.value = X7_PACKET_OPCODE_RESERVED, .name = "reserved"},
    {                                    .name = NULL}
};

/* Lookup table giving names for x7_packet_type_1_register_t */
static const x7_enum_lut_entry_t x7_packet_type_1_register_names[] =
{
    {.value = X7_PACKET_TYPE_1_REG_CRC     , .name = "CRC"},
    {.value = X7_PACKET_TYPE_1_REG_FAR     , .name = "FAR"},
    {.value = X7_PACKET_TYPE_1_REG_FDRI    , .name = "FDRI"},
    {.value = X7_PACKET_TYPE_1_REG_FDRO    , .name = "FDRO"},
    {.value = X7_PACKET_TYPE_1_REG_CMD     , .name = "CMD"},
    {.value = X7_PACKET_TYPE_1_REG_CTL0    , .name = "CTL0"},
    {.value = X7_PACKET_TYPE_1_REG_MASK    , .name = "MASK"},
    {.value = X7_PACKET_TYPE_1_REG_STAT    , .name = "STAT"},
    {.value = X7_PACKET_TYPE_1_REG_LOUT    , .name = "LOUT"},
    {.value = X7_PACKET_TYPE_1_REG_COR0    , .name = "COR0"},
    {.value = X7_PACKET_TYPE_1_REG_MFWR    , .name = "MFWR"},
    {.value = X7_PACKET_TYPE_1_REG_CBC     , .name = "CBC"},
    {.value = X7_PACKET_TYPE_1_REG_IDCODE  , .name = "IDCODE"},
    {.value = X7_PACKET_TYPE_1_REG_AXSS    , .name = "AXSS"},
    {.value = X7_PACKET_TYPE_1_REG_COR1    , .name = "COR1"},
    {.value = X7_PACKET_TYPE_1_REG_WBSTAR  , .name = "WBSTAR"},
    {.value = X7_PACKET_TYPE_1_REG_TIMER   , .name = "TIMER"},
    {.value = X7_PACKET_TYPE_1_REG_RBCRC_SW, .name = "RBCRC_SW"},
    {.value = X7_PACKET_TYPE_1_REG_BOOTSTS , .name = "BOOTSTS"},
    {.value = X7_PACKET_TYPE_1_REG_CTL1    , .name = "CTL1"},
    {.value = X7_PACKET_TYPE_1_REG_BSPI    , .name = "BSPI"},
    {                                        .name = NULL}
};

/* Lookup table giving names for x7_command_register_code_t */
static const x7_enum_lut_entry_t x7_command_register_code_names[] =
{
    {.value = X7_COMMAND_NULL       , .name = "NULL"},
    {.value = X7_COMMAND_WCFG       , .name = "WCFG"},
    {.value = X7_COMMAND_MFW        , .name = "MFW"},
    {.value = X7_COMMAND_DGHIGH_LFRM, .name = "DGHIGH_LFRM"},
    {.value = X7_COMMAND_RCFG       , .name = "RCFG"},
    {.value = X7_COMMAND_START      , .name = "START"},
    {.value = X7_COMMAND_RCAP       , .name = "RCAP"},
    {.value = X7_COMMAND_RCRC       , .name = "RCRC"},
    {.value = X7_COMMAND_AGHIGH     , .name = "AGHIGH"},
    {.value = X7_COMMAND_SWITCH     , .name = "SWITCH"},
    {.value = X7_COMMAND_GRESTORE   , .name = "GRESTORE"},
    {.value = X7_COMMAND_SHUTDOWN   , .name = "SHUTDOWN"},
    {.value = X7_COMMAND_GCAPTURE   , .name = "GCAPTURE"},
    {.value = X7_COMMAND_DESYNC     , .name = "DESYNC"},
    {.value = X7_COMMAND_RESERVED   , .name = "RESERVED"},
    {.value = X7_COMMAND_IPROG      , .name = "IPROG"},
    {.value = X7_COMMAND_CRCC       , .name = "CRCC"},
    {.value = X7_COMMAND_LTIMER     , .name = "LTIMER"},
    {.value = X7_COMMAND_BSPI_READ  , .name = "BSPI_READ"},
    {.value = X7_COMMAND_FALL_EDGE  , .name = "FALL_EDGE"},
    {                                 .name = NULL}
};


/* Lookup table giving names for the X7_PACKET_TYPE_1_REG_IDCODE values.
 * The don't care bits are left as zeros, and there are no mask bits defined for the don't care bits,
 * on the assumption that:
 * a. The don't care bits are the device revision.
 * b. The bitstreams are written with the bits for the device revision left at zeros. */
static const x7_enum_lut_entry_t x7_idcode_names[] =
{
    /* Spartan-7 Family */
    {.value = 0X3622093, .name = "7S6"},
    {.value = 0X3620093, .name = "7S15"},
    {.value = 0X37C4093, .name = "7S25"},
    {.value = 0X362F093, .name = "7S50"},
    {.value = 0X37C8093, .name = "7S75"},
    {.value = 0X37C7093, .name = "7S100"},
    /* Artix-7 Family */
    {.value = 0X37C3093, .name = "7A12T"},
    {.value = 0X362E093, .name = "7A15T"},
    {.value = 0X37C2093, .name = "7A25T"},
    {.value = 0X362D093, .name = "7A35T"},
    {.value = 0X362C093, .name = "7A50T"},
    {.value = 0X3632093, .name = "7A75T"},
    {.value = 0X3631093, .name = "7A100T"},
    {.value = 0X3636093, .name = "7A200T"},
    /* Kintex-7 Family */
    {.value = 0X3647093, .name = "7K70T"},
    {.value = 0X364C093, .name = "7K160T"},
    {.value = 0X3651093, .name = "7K325T"},
    {.value = 0X3747093, .name = "7K355T"},
    {.value = 0X3656093, .name = "7K410T"},
    {.value = 0X3752093, .name = "7K420T"},
    {.value = 0X3751093, .name = "7K480T"},
    /* Virtex-7 Family */
    {.value = 0X3671093, .name = "7V585T"},
    {.value = 0X36B3093, .name = "7V2000T"},
    {.value = 0X3667093, .name = "7VX330T"},
    {.value = 0X3682093, .name = "7VX415T"},
    {.value = 0X3687093, .name = "7VX485T"},
    {.value = 0X3692093, .name = "7VX550T"},
    {.value = 0X3691093, .name = "7VX690T"},
    {.value = 0X3696093, .name = "7VX980T"},
    {.value = 0X36D5093, .name = "7VX1140T"},
    {.value = 0X36D9093, .name = "7VH580T"},
    {.value = 0X36DB093, .name = "7VH870T"},
    /* Kintex UltraScale FPGAs */
    {.value = 0X3824093, .name = "KU025"},
    {.value = 0X3823093, .name = "KU035"},
    {.value = 0X3822093, .name = "KU040"},
    {.value = 0X3919093, .name = "KU060"},
    {.value = 0X380F093, .name = "KU085"},
    {.value = 0X3844093, .name = "KU095"},
    {.value = 0X390D093, .name = "KU115"},
    /* Virtex UltraScale FPGAs */
    {.value = 0X3939093, .name = "VU065"},
    {.value = 0X3843093, .name = "VU080"},
    {.value = 0X3842093, .name = "VU095"},
    {.value = 0X392D093, .name = "VU125"},
    {.value = 0X3933093, .name = "VU160"},
    {.value = 0X3931093, .name = "VU190"},
    {.value = 0X396D093, .name = "VU440"},
    /* Artix UltraScale+ FPGAs */
    {.value = 0X4AF6093, .name = "AU7P"},
    {.value = 0X4AC4093, .name = "AU10P"},
    {.value = 0X4AC2093, .name = "AU15P"},
    {.value = 0X4A65093, .name = "AU20P"},
    {.value = 0X4A64093, .name = "AU25P"},
    /* Kintex UltraScale+ FPGAs */
    {.value = 0X4A63093, .name = "KU3P"},
    {.value = 0X4A62093, .name = "KU5P"},
    {.value = 0X484A093, .name = "KU9P"},
    {.value = 0X4A4E093, .name = "KU11P"},
    {.value = 0X4A52093, .name = "KU13P"},
    {.value = 0X4A56093, .name = "KU15P"},
    {.value = 0X4ACF093, .name = "KU19P"},
    /* Virtex UltraScale+ FPGAs */
    {.value = 0X4B39093, .name = "VU3P"},
    {.value = 0X4B2B093, .name = "VU5P"},
    {.value = 0X4B29093, .name = "VU7P"},
    {.value = 0X4B31093, .name = "VU9P"},
    {.value = 0X4B49093, .name = "VU11P"},
    {.value = 0X4B51093, .name = "VU13P"},
    {.value = 0X4BA1093, .name = "VU19P"},
    {.value = 0X4ACE093, .name = "VU23P"},
    {.value = 0X4B43093, .name = "VU27P"},
    {.value = 0X4B41093, .name = "VU29P"},
    {.value = 0X4B6B093, .name = "VU31P"},
    {.value = 0X4B69093, .name = "VU33P"},
    {.value = 0X4B71093, .name = "VU35P"},
    {.value = 0X4B79093, .name = "VU37P"},
    {.value = 0X4B73093, .name = "VU45P"},
    {.value = 0X4B7B093, .name = "VU47P"},
    {.value = 0X4B61093, .name = "VU57P"},
    /* Zynq-7000 SoC */
    {.value = 0X3722093, .name = "7Z010"},
    {.value = 0X373B093, .name = "7Z015"},
    {.value = 0X3727093, .name = "7Z020"},
    {.value = 0X372C093, .name = "7Z030"},
    {.value = 0X3732093, .name = "7Z035"},
    {.value = 0X3731093, .name = "7Z045"},
    {.value = 0X3736093, .name = "7Z100"},
    {.value = 0X3723093, .name = "7Z007S"},
    {.value = 0X373C093, .name = "7Z012S"},
    {.value = 0X3728093, .name = "7Z014S"},
    /* Zynq UltraScale+                    Product Family */
    {.value = 0X4826093, .name = "XCZU1" }, /* CG EG    */
    {.value = 0X4A43093, .name = "XCZU2" }, /* CG EG    */
    {.value = 0X4A42093, .name = "XCZU3" }, /* CG EG    */
    {.value = 0X4A47093, .name = "XCZU4" }, /* CG EG EV */
    {.value = 0X4A46093, .name = "XCZU5" }, /* CG EG EV */
    {.value = 0X484B093, .name = "XCZU6" }, /* CG EG    */
    {.value = 0X4A5A093, .name = "XCZU7" }, /* CG EG EV */
    {.value = 0X484A093, .name = "XCZU9" }, /* CG       */
    {.value = 0X4A4E093, .name = "XCZU11"}, /*    EG    */
    {.value = 0X4A52093, .name = "XCZU15"}, /*    EG    */
    {.value = 0X4A57093, .name = "XCZU17"}, /*    EG    */
    {.value = 0X4A56093, .name = "XCZU19"}, /*    EG    */
    {.value = 0X4AF2093, .name = "XCZU3T"}, /* CG EG    */
    /* Zynq UltraScale+ RFSoC */
    {.value = 0X4A83093, .name = "XCZU21DR"},
    {.value = 0X4A87093, .name = "XCZU25DR"},
    {.value = 0X4A86093, .name = "XCZU27DR"},
    {.value = 0X4A82093, .name = "XCZU28DR"},
    {.value = 0X4A84093, .name = "XCZU29DR"},
    {.value = 0X4A88093, .name = "XCZU39DR"},
    {.value = 0X4ADA093, .name = "XCZU42DR"},
    {.value = 0X4AA7093, .name = "XCZU43DR"},
    {.value = 0X4AA2093, .name = "XCZU46DR"},
    {.value = 0X4AA9093, .name = "XCZU47DR"},
    {.value = 0X4AA5093, .name = "XCZU48DR"},
    {.value = 0X4AA8093, .name = "XCZU49DR"},
    {.value = 0X4AD8093, .name = "XCZU55DR"},
    {.value = 0X4AD9093, .name = "XCZU57DR"},
    {.value = 0X4AA3093, .name = "XCZU58DR"},
    {.value = 0X4AA6093, .name = "XCZU59DR"},
    {.value = 0X4ADB093, .name = "XCZU63DR"},
    {.value = 0X4ADC093, .name = "XCZU64DR"},
    {.value = 0X4AD7093, .name = "XCZU65DR"},
    {.value = 0X4AD6093, .name = "XCZU67DR"},
    /* Accelerator cards.
     * Comments from https://github.com/erinadreno/list_of_Xilinx_FPGAs?tab=readme-ov-file#alveo-and-kria
     *
     *                                         Product                Corresponding standalone FPGA */
    {.value = 0X4A58093, .name = "XCU25"},  /* Alveo U25N             XCZU19EG */
    {.value = 0X4AD5093, .name = "XCU26"},  /* Alveo SN1000 SmartNIC  XCVU23P */
    {.value = 0X4A5C093, .name = "XCU30"},  /* Alveo U30              XCZU7EV */
    {.value = 0X4B7D093, .name = "XCU55C"}, /* Alveo U55C             XCVU47P */
    {.value = 0X4B77093, .name = "XCU50"},  /* Alveo U50              XCVU35P */
    {.value = 0X4B37093, .name = "XCU200"}, /* Alveo U200             XCVU9P */
    {.value = 0X4B57093, .name = "XCU250"}, /* Alveo U250             XCVU13P */
    {.value = 0X4B7D093, .name = "XCU280"}, /* Alveo U280             XCVU37P */
    {.value = 0X4AD5093, .name = "XCUX35"}, /* Alveo X3522            XCVU23P */
    {.value = 0X4A49093, .name = "XCK26"},  /* Kria K26               XCZU5EV */
    {                    .name = NULL}
};


/**
 * @brief Lookup the name for enumeration
 * @param[in] lut The lookup table
 * @param[in] value The enumeration value to get the name for
 * @param[out] unknown A caller supplied buffer to format a string if the enumeration value isn't found in lut
 * @return The name for the enumeration.
 */
static const char *x7_bitstream_lookup_enum (const x7_enum_lut_entry_t *const lut, const uint32_t value,
                                             char unknown[const X7_ENUM_UNKNOWN_STRING_LEN])
{
    const x7_enum_lut_entry_t *lut_entry;

    lut_entry = lut;
    while (lut_entry->name != NULL)
    {
        if (value == lut_entry->value)
        {
            return lut_entry->name;
        }
        lut_entry++;
    }

    snprintf (unknown, X7_ENUM_UNKNOWN_STRING_LEN, "unknown (0x%x)", value);
    return unknown;
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
 * @brief Determine if a packet from a bitstream is a NOP
 * @param[in] packet The packet to compare
 * @return Returns true if the packet is a NOP
 */
bool x7_packet_is_nop (const x7_packet_record_t *const packet)
{
    return (packet->header_type == X7_TYPE_1_PACKET) && (packet->opcode == X7_PACKET_OPCODE_NOP);
}


/**
 * @brief Determine if a packet from a bitstream is a write to a specific register
 * @param[in] packet The packet to compare
 * @param[in] register_address The specific register to compare against
 * @return Returns true if the packet is a write to register_address
 */
bool x7_packet_is_register_write (const x7_packet_record_t *const packet, const x7_packet_type_1_register_t register_address)
{
    return (packet->header_type == X7_TYPE_1_PACKET) && (packet->opcode == X7_PACKET_OPCODE_WRITE) &&
            (packet->register_address == register_address);
}


/**
 * @brief Determine if a packet from a bitstream is writing one word to a specific register
 * @param[in] context Used to obtain the register value
 * @param[in] packet The packet to compare
 * @param[in] register_address The specific register to compare against
 * @param[out] register_value The value which has been written to the register
 * @return Returns true if the packet is a write of a single word to the register_address
 */
bool x7_packet_is_word_register_write (const x7_bitstream_context_t *const context,
                                       const x7_packet_record_t *const packet,
                                       const x7_packet_type_1_register_t register_address, uint32_t *const register_value)
{
    if (x7_packet_is_register_write (packet, register_address) && (packet->word_count == 1))
    {
        *register_value = x7_bitstream_unpack_word (context, packet->data_words_offset);
        return true;
    }
    else
    {
        return false;
    }
}


/**
 * @brief Determine if a packet from a bitstream is writing a specific command
 * @param[in] context Used to obtain the register value
 * @param[in] packet The packet to compare
 * @param[in] expected_command The command to compare against
 * @return Returns true if the packet is a write of expected_command
 */
bool x7_packet_is_command (const x7_bitstream_context_t *const context,
                           const x7_packet_record_t *const packet,
                           const x7_command_register_code_t expected_command)
{
    uint32_t command_value;

    return x7_packet_is_word_register_write (context, packet, X7_PACKET_TYPE_1_REG_CMD, &command_value) &&
            (command_value == expected_command);
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
 * @param[in] current_slr_index Which SLR are currently parsing
 * @return Returns true if read the data words, or false if an error occurred.
 */
static bool x7_bitstream_read_packet_data (x7_bitstream_context_t *const context, const x7_packet_record_t *const new_packet,
                                           const uint32_t current_slr_index)
{
    x7_bitstream_slr_t *const slr = &context->slrs[current_slr_index];
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
    if (slr->num_packets == slr->packets_allocated_length)
    {
        slr->packets_allocated_length += grow_increment;
        slr->packets = realloc (slr->packets, sizeof (slr->packets[0]) * slr->packets_allocated_length);
        if (slr->packets == NULL)
        {
            snprintf (context->error, sizeof (context->error), "Failed to allocate packets array of %zu bytes",
                    sizeof (slr->packets[0]) * slr->packets_allocated_length);
            return false;
        }
    }

    slr->packets[slr->num_packets] = *new_packet;
    slr->num_packets++;

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
    uint32_t current_slr_index = 0;
    context->bitstream_length_bytes = 0;
    context->num_slrs = 0;
    context->next_word_index = 0;

    /* Iterate over the possible SLRs for a device, each of which has a Sync word.
     * Can't to seem to find where bitstream format for SLRs is documented. The following structure was written to ensure
     * the entire bitstream for a XCU200 was parsed.
     *
     * The loop will only execute once for devices which don't use SLRs. */
    bool parse_complete = false;
    do
    {
        /* Start an empty SLR */
        current_slr_index = context->num_slrs;
        x7_bitstream_slr_t *const slr = &context->slrs[current_slr_index];

        slr->packets = NULL;
        slr->num_packets = 0;
        slr->packets_allocated_length = 0;
        slr->end_of_configuration_seen = false;

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
        slr->sync_word_byte_index = context->next_word_index;
        slr->sync_word_found = false;
        do
        {
            context->next_word_index = slr->sync_word_byte_index;
            if (!x7_bitstream_get_next_word (context, &candidate_sync_word))
            {
                snprintf (context->error, sizeof (context->error), "No Sync word found");
                return;
            }
            if (candidate_sync_word == X7_BITSTREAM_SYNC_WORD)
            {
                slr->sync_word_found = true;
                context->num_slrs++;
            }
            else
            {
                slr->sync_word_byte_index++;
            }
        } while (!slr->sync_word_found);

        /* Parse the bitstream configuration words until see the end of configuration, or the end of the data buffer */
        bool slr_parse_complete = false;
        bool previous_packet_was_type_1 = false;
        uint32_t header_word_index;
        uint32_t configuration_header_word;
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

                if (slr->end_of_configuration_seen)
                {
                    if (configuration_header_word == X7_BITSTREAM_SYNC_WORD)
                    {
                        /* If a Sync word follows the final NOP then there should be a following SLR which will have an
                         * additional Sync word. Therefore, exit the SLR parse loop and start looking for the Sync word
                         * for the following SLR.
                         *
                         * Can't find documentation which describes the words between this Sync word and the next Sync word
                         * at the start of the next SLR. Did seem to be a write with no data to register address 0x1e
                         * for which can't find documentation. */
                        slr_parse_complete = true;
                    }
                    else if ((context->num_slrs > 1) &&
                             (new_packet.header_type == X7_TYPE_1_PACKET) && (new_packet.opcode == X7_PACKET_OPCODE_WRITE))
                    {
                        /* When a device has SLRs, the final SLR can have a sequence of DESYNC and START commands,
                         * apparently for each SLR. Store these commands so get to the end of the bitstream. */
                        slr->end_of_configuration_seen = false;
                        new_packet.register_address = (configuration_header_word & 0x07FFE000) >> 13;
                        new_packet.word_count = (configuration_header_word & 0x000007FF);
                        if (!x7_bitstream_read_packet_data (context, &new_packet, current_slr_index))
                        {
                            return;
                        }
                    }
                    else if ((new_packet.header_type == X7_TYPE_1_PACKET) && (new_packet.opcode == X7_PACKET_OPCODE_NOP) &&
                            (new_packet.word_count == 0))
                    {
                        /* Once have seen the end of configuration, store any padding NOP's until read a configuration word which
                         * isn't a a NOP. Doesn't validate a configuration word which isn't a NOP since when reading a SPI flash
                         * will likely find an erased word after the NOPs. */
                        if (!x7_bitstream_read_packet_data (context, &new_packet, current_slr_index))
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
                        if (!x7_bitstream_read_packet_data (context, &new_packet, current_slr_index))
                        {
                            return;
                        }

                        /* Look for the write of the DESYNC command which indicates the end of the configuration */
                        if ((new_packet.opcode == X7_PACKET_OPCODE_WRITE) && (new_packet.word_count == 1))
                        {
                            const uint32_t reg_data = x7_bitstream_unpack_word (context, new_packet.data_words_offset);

                            if ((new_packet.register_address == X7_PACKET_TYPE_1_REG_CMD) && (reg_data == X7_COMMAND_DESYNC))
                            {
                                slr->end_of_configuration_seen = true;
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
                        if (!x7_bitstream_read_packet_data (context, &new_packet, current_slr_index))
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
        } while (!slr_parse_complete && !parse_complete);
    }
    while (!parse_complete && (context->num_slrs < X7_MAX_NUM_SLRS));
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
 * @brief Read one line from an Intel HEX file, storing the contents as an array of bytes
 * @param[in/out] context Contains the Intel HEX file being read
 * @return Returns true if have read a valid line of ASCII hex bytes, in terms of length and checksum
 */
static bool x7_bitstream_read_intel_hex_line (x7_bitstream_context_t *const context)
{
    bool line_valid;

    /* Check for the Start Code for a line of Intel HEX. Since the file might be a bit or bin format,
     * don't discard other contents looking for a Start Code. The Xilinx tools which create .mcs file
     * don't include related information in the Intel HEX file. */
    line_valid = (context->file.intel_hex_line_start_offset < context->file.raw_length) &&
        (context->file.raw_contents[context->file.intel_hex_line_start_offset] == ':');

    if (line_valid)
    {
        /* Convert the line of ASCII hex bytes to binary */
        context->file.intel_hex_line_start_offset++;
        context->file.intel_hex_line_len = 0;
        while ((context->file.intel_hex_line_start_offset < context->file.raw_length) &&
                (context->file.intel_hex_line_len < X7_INTEL_HEX_MAX_BYTES_PER_LINE) &&
                (isxdigit (context->file.raw_contents[context->file.intel_hex_line_start_offset])) &&
                (isxdigit (context->file.raw_contents[context->file.intel_hex_line_start_offset + 1])))
        {
            const int hex_msdigit = tolower (context->file.raw_contents[context->file.intel_hex_line_start_offset]);
            const int hex_lsdigit = tolower (context->file.raw_contents[context->file.intel_hex_line_start_offset + 1]);
            uint8_t *const byte_value = &context->file.intel_hex_line_bytes[context->file.intel_hex_line_len];

            if (hex_msdigit >= 'a')
            {
                *byte_value = (uint8_t) (((hex_msdigit - 'a') + 0xa) << 4);
            }
            else
            {
                *byte_value = (uint8_t) ((hex_msdigit - '0') << 4);
            }
            if (hex_lsdigit >= 'a')
            {
                *byte_value |= (uint8_t) ((hex_lsdigit - 'a') + 0xa);
            }
            else
            {
                *byte_value |= (uint8_t) (hex_lsdigit - '0');
            }

            context->file.intel_hex_line_start_offset += 2;
            context->file.intel_hex_line_len++;
        }

        /* Skip any new line or carriage return characters following the ASCII hex */
        while ((context->file.intel_hex_line_start_offset < context->file.raw_length               ) &&
                ((context->file.raw_contents[context->file.intel_hex_line_start_offset] == '\n') ||
                 (context->file.raw_contents[context->file.intel_hex_line_start_offset] == '\r')   ))
        {
            context->file.intel_hex_line_start_offset++;
        }

        /* Verify the length of the line of Intel HEX in terms of:
         * a. Minimum valid length.
         * b. The Byte Count in the 1st byte is consistent with the number of bytes read from the line. */
        const uint32_t min_valid_line_len = 1 /* Byte count */ + 2 /* Address */ + 1 /* Record Type */ + 1 /* Checksum */;
        line_valid = (context->file.intel_hex_line_len >= min_valid_line_len) &&
                ((context->file.intel_hex_line_bytes[0] + min_valid_line_len) == context->file.intel_hex_line_len);

        if (line_valid)
        {
            /* Verify the checksum for the bytes in the line of Intel HEX */
            uint32_t byte_sum = 0;

            for (uint32_t byte_index = 0; byte_index < context->file.intel_hex_line_len; byte_index++)
            {
                byte_sum += context->file.intel_hex_line_bytes[byte_index];
            }

            line_valid = (byte_sum & 0xff) == 0;
        }
    }

    return line_valid;
}


/**
 * @brief Attempt to read a bitstream file as Intel HEX format
 * @details
 *  This parses the Intel HEX file in context->file.raw_contents, storing the binary contents in context->data_buffer.
 *  The assumptions are:
 *  a. The starting address in the Intel HEX file is zero. If that is not the case, then the start of
 *     context->data_buffer will be padded with 0xFF's which increase the size.
 *  b. There is only one bitstream in the Intel HEX file.
 *     If the Intel HEX file is readback from an actual SPI flash which uses Fallback Configuration then this
 *     assumption won't be true and this program will only report the bitstream of the lowest address.
 * @param[in/out] context Contains the bitstream file being read
 * @return Returns true if has successfully parsed the input file in Intel HEX format until the end of file record.
 */
static bool x7_bitstream_read_intel_hex_file (x7_bitstream_context_t *const context)
{
    bool valid_intel_hex_file;
    uint32_t extended_start_address_offset;
    bool seen_end_of_file_type;

    /* The sub-set of Intel HEX record types used to extract the bitstream contents */
    enum
    {
        INTEL_HEX_RECORD_TYPE_DATA = 0x00,
        INTEL_HEX_RECORD_TYPE_END_OF_FILE = 0x01,
        INTEL_HEX_RECORD_TYPE_EXTENDED_SEGMENT_ADDRESS = 0x02,
        INTEL_HEX_RECORD_TYPE_EXTENDED_LINEAR_ADDRESS = 0x04
    };

    context->data_buffer = NULL;
    context->data_buffer_length = 0;
    context->file.intel_hex_line_start_offset = 0;

    valid_intel_hex_file = x7_bitstream_read_intel_hex_line (context);
    if (valid_intel_hex_file)
    {
        /* Have read an initial valid Intel HEX line from the start of the file.
         * Attempt to parse as an Intel HEX file, storing the binary contents in context->data_buffer */
        extended_start_address_offset = 0;
        seen_end_of_file_type = false;
        while (!seen_end_of_file_type && valid_intel_hex_file)
        {
            /* Split the line into the record field.
             * Checksum not used here, as validated by x7_bitstream_read_intel_hex_line() */
            const uint8_t record_byte_count = context->file.intel_hex_line_bytes[0];
            const uint32_t record_address = (((uint32_t) context->file.intel_hex_line_bytes[1]) << 8) |
                    ((uint32_t) context->file.intel_hex_line_bytes[2]);
            const uint8_t record_type = context->file.intel_hex_line_bytes[3];
            const uint8_t *const record_data = &context->file.intel_hex_line_bytes[4];

            switch (record_type)
            {
            case INTEL_HEX_RECORD_TYPE_DATA:
                {
                    const uint32_t data_start_offset = extended_start_address_offset + record_address;
                    const uint32_t min_data_buffer_len = data_start_offset + record_byte_count;
                    const uint32_t grow_size_multiple = 32768;
                    const uint32_t new_data_buffer_length =
                            ((min_data_buffer_len + grow_size_multiple - 1) / grow_size_multiple) * grow_size_multiple;

                    /* Grow the length of the data buffer to contain space for the record data */
                    if (new_data_buffer_length > context->data_buffer_length)
                    {
                        context->data_buffer = realloc (context->data_buffer, new_data_buffer_length);
                        if (context->data_buffer != NULL)
                        {
                            /* Fill the expanded data_buffer with 0xFF, in case the file skips blank parts of the address space */
                            memset (&context->data_buffer[context->data_buffer_length], 0xFF,
                                    new_data_buffer_length - context->data_buffer_length);
                            context->data_buffer_length = new_data_buffer_length;
                        }
                        else
                        {
                            snprintf (context->error, sizeof (context->error), "Failed to allocate data_buffer of %u bytes",
                                    new_data_buffer_length);
                            valid_intel_hex_file = false;
                        }
                    }

                    if (valid_intel_hex_file)
                    {
                        /* Store the data bytes from the record in the Intel HEX file */
                        memcpy (&context->data_buffer[data_start_offset], record_data, record_byte_count);
                    }
                }
                break;

            case INTEL_HEX_RECORD_TYPE_END_OF_FILE:
                valid_intel_hex_file = record_byte_count == 0;
                if (valid_intel_hex_file)
                {
                    seen_end_of_file_type = true;
                }
                break;

            case INTEL_HEX_RECORD_TYPE_EXTENDED_SEGMENT_ADDRESS:
                valid_intel_hex_file = record_byte_count == 2;
                if (valid_intel_hex_file)
                {
                    extended_start_address_offset = (((uint32_t) record_data[0]) << 16) | (((uint32_t) record_data[1]) << 8);
                }
                break;

            case INTEL_HEX_RECORD_TYPE_EXTENDED_LINEAR_ADDRESS:
                valid_intel_hex_file = record_byte_count == 2;
                if (valid_intel_hex_file)
                {
                    extended_start_address_offset = (((uint32_t) record_data[0]) << 24) | (((uint32_t) record_data[1]) << 16);
                }
                break;

            default:
                /* Ignore this record type, as doesn't affect the bitstream contents */
                break;
            }

            if (!seen_end_of_file_type && valid_intel_hex_file)
            {
                valid_intel_hex_file = x7_bitstream_read_intel_hex_line (context);
            }
        }

        if (!seen_end_of_file_type)
        {
            /* While the file started with a valid Intel HEX file did't find the end of file record.
             * The caller will try a different file type, but warn might have encountered a truncated Intel HEX file */
            printf ("Warning: Didn't find end of file record in %s, possible truncated Intel HEX file\n", context->file.pathname);
            valid_intel_hex_file = false;
            free (context->data_buffer);
            context->data_buffer = NULL;
            context->data_buffer_length = 0;
        }
    }

    return valid_intel_hex_file;
}


/**
 * @brief Read a bitstream from a local file on the host
 * @details Handles .bit, .mcs or .bin format files created by the Xilinx Vivado tools
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
     * Reject a file which is >= 4GiB as too large for a bitstream as for a SPI flash can only support 32-bit addressing.
     * For an Intel Hex file this limits the maximum bitstream size to less than 2GiB.
     * From UG570 max bitstream size for rhe UltraScale Architecture-based FPGAs is 1Gb so this check is still valid. */
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
    if (x7_bitstream_read_intel_hex_file (context))
    {
        context->file.file_format = X7_BITSTREAM_FILE_FORMAT_INTEL_HEX;
        x7_bitstream_parse (context);
    }
    else if ((context->file.raw_length > sizeof (x7_bit_file_fixed_header)) &&
            (memcmp (context->file.raw_contents, x7_bit_file_fixed_header, sizeof (x7_bit_file_fixed_header)) == 0))
    {
        context->file.file_format = X7_BITSTREAM_FILE_FORMAT_BIT;
        if (x7_bitstream_parse_bit_file_header (context))
        {
            x7_bitstream_parse (context);
        }
    }
    else
    {
        /* Assume the file is in .bin format containing the binary contents of the bitstream */
        context->file.file_format = X7_BITSTREAM_FILE_FORMAT_BIN;
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
        if (context->file.file_format == X7_BITSTREAM_FILE_FORMAT_INTEL_HEX)
        {
            free (context->data_buffer);
            context->data_buffer = NULL;
        }
    }

    for (uint32_t slr_index = 0; slr_index < context->num_slrs; slr_index++)
    {
        free (context->slrs[slr_index].packets);
        context->slrs[slr_index].packets = NULL;
    }
}


/**
 * @brief Summarise the section of packets from a bitstream which perform configuration data writes
 * @details
 *  This starts with a write to the FAR register, and contains any:
 *  - NOP commands
 *  - Further writes to to the FAR register
 *  - WCFG or MFW commands
 *  - Writes to the FDRI or MFWR registers which contain configuration data
 *  - Type 2 packets which contain configuration data
 *
 *  Returns once have read anything not in the above list, which is normally a write to the CRC register.
 *
 *  When bitstream compression isn't used, all the configuration data will be written in a single type 2 packet.
 *
 *  When bitstream compression is used, the configuration data is written in a mixture of FDRI or MFWR register writes
 *  and type 2 packets.
 *
 *  This function was written to reduce the amount of output for summarising the bitstream, by inspecting a sample of
 *  bitstreams. As the contents of the configuration data isn't documented no need to try and decode the configurarion
 *  data contents.
 * @param[in] context The bitstream being parsed
 * @param[in] current_slr_index Which SLR are currently parsing
 * @param[in/out] packet_index Used to advance through the packets in the bitstream.
 *                On entry the first packet of configuration data to summarise
 *                On exit the packet after the end of the configuration data
 */
static void x7_bitstream_summarise_configuration_data_writes (const x7_bitstream_context_t *const context,
                                                              const uint32_t current_slr_index,
                                                              uint32_t *const packet_index)
{
    const x7_bitstream_slr_t *const slr = &context->slrs[current_slr_index];
    bool parsing_configuration_data_writes = true;
    uint32_t num_nops = 0;
    uint32_t num_far_writes = 0;
    uint32_t num_wcfg_commands = 0;
    uint32_t num_fdri_writes = 0;
    uint32_t total_fdri_words = 0;
    uint32_t num_mfw_commands = 0;
    uint32_t num_null_commands = 0;
    uint32_t num_mfwr_writes = 0;
    uint32_t total_mfwr_words = 0;
    uint32_t num_type_2_packets = 0;
    uint32_t total_type_2_packet_words = 0;
    uint32_t total_packets_consumed = 0;

    /* Count the packets which form the bitstream section which contains the configuration data writes */
    while ((*packet_index < slr->num_packets) && parsing_configuration_data_writes)
    {
        const x7_packet_record_t *const packet = &slr->packets[*packet_index];

        parsing_configuration_data_writes = true;
        if (x7_packet_is_nop (packet))
        {
            num_nops++;
        }
        else if (x7_packet_is_register_write (packet, X7_PACKET_TYPE_1_REG_FAR))
        {
            num_far_writes++;
        }
        else if (x7_packet_is_command (context, packet, X7_COMMAND_WCFG))
        {
            num_wcfg_commands++;
        }
        else if (x7_packet_is_register_write (packet, X7_PACKET_TYPE_1_REG_FDRI))
        {
            num_fdri_writes++;
            total_fdri_words += packet->word_count;
        }
        else if (x7_packet_is_command (context, packet, X7_COMMAND_MFW))
        {
            num_mfw_commands++;
        }
        else if (x7_packet_is_command (context, packet, X7_COMMAND_NULL))
        {
            /* Seen in a KU060 compressed bitsteam.
             * Not sure if the NULL command is a placeholder for something else. */
            num_null_commands++;
        }
        else if (x7_packet_is_register_write (packet, X7_PACKET_TYPE_1_REG_MFWR))
        {
            num_mfwr_writes++;
            total_mfwr_words += packet->word_count;
        }
        else if (packet->header_type == X7_TYPE_2_PACKET)
        {
            num_type_2_packets++;
            total_type_2_packet_words += packet->word_count;
        }
        else
        {
            parsing_configuration_data_writes = false;
        }

        if (parsing_configuration_data_writes)
        {
            (*packet_index)++;
            total_packets_consumed++;
        }
    }

    if ((total_packets_consumed == 1) && (num_far_writes == 1))
    {
        /* If only a single FAR write found then this is actual after the configuration writes ,
         * so let the caller display the actual FAR write */
        (*packet_index)--;
    }
    else
    {
        /* Summarise the packets counted above */
        printf ("  Configuration data writes consisting of:\n");
        if (num_nops > 0)
        {
            printf ("    %u NOPs\n", num_nops);
        }
        if (num_far_writes > 0)
        {
            printf ("    %u FAR writes\n", num_far_writes);
        }
        if (num_wcfg_commands > 0)
        {
            printf ("    %u WCFG commands\n", num_wcfg_commands);
        }
        if (num_fdri_writes > 0)
        {
            printf ("    %u FDRI writes with a total of %u words\n", num_fdri_writes, total_fdri_words);
        }
        if (num_mfw_commands > 0)
        {
            printf ("    %u MFW commands\n", num_mfw_commands);
        }
        if (num_null_commands > 0)
        {
            printf ("    %u NULL commands\n", num_null_commands);
        }
        if (num_mfwr_writes > 0)
        {
            printf ("    %u MFWR writes with a total of %u words\n", num_mfwr_writes, total_mfwr_words);
        }
        if (num_type_2_packets > 0)
        {
            printf ("    %u Type 2 packets with a total of %u words\n", num_type_2_packets, total_type_2_packet_words);
        }
    }
}


/**
 * @brief Summarise the contents of a bitstream to the console, for manually analysis
 * @param[in] context Contains the bitstream to summarise
 */
void x7_bitstream_summarise (const x7_bitstream_context_t *const context)
{
    char unknown_opcode[X7_ENUM_UNKNOWN_STRING_LEN];
    char unknown_register[X7_ENUM_UNKNOWN_STRING_LEN];
    char unknown_command[X7_ENUM_UNKNOWN_STRING_LEN];
    char unknown_idcode[X7_ENUM_UNKNOWN_STRING_LEN];
    uint32_t register_value;

    /* Indicate if the bitstream was parsed successfully and therefore if appears valid.
     * Even if not considered valid displays the information which was parsed. */
    uint32_t total_packets = 0;
    bool end_of_configuration_seen = false;
    for (uint32_t slr_index = 0; slr_index < context->num_slrs; slr_index++)
    {
        total_packets += context->slrs[slr_index].num_packets;
        end_of_configuration_seen = context->slrs[slr_index].end_of_configuration_seen;
    }
    if (end_of_configuration_seen)
    {
        printf ("Successfully parsed bitstream of length %u bytes with %u configuration packets",
                context->bitstream_length_bytes, total_packets);
        if (context->num_slrs > 1)
        {
            printf (" with %u SLRs\n", context->num_slrs);
        }
        else
        {
            printf ("\n");
        }
    }
    else
    {
        printf ("Error parsing bitstream: %s\n", context->error);
    }

    /* Display information specific to the source of the parsed bitstream */
    if (context->controller != NULL)
    {
        printf ("Read %u bytes from SPI flash starting at address %u\n", context->data_buffer_length, context->flash_start_address);
    }
    else
    {
        printf ("Read bitsteam from file %s\n", context->file.pathname);
        if (context->file.file_format == X7_BITSTREAM_FILE_FORMAT_BIT)
        {
            printf (".bit format header:\n");
            printf ("  design_name=%s\n", context->file.design_name);
            printf ("  part_name=%s\n", context->file.part_name);
            printf ("  date=%s\n", context->file.date);
            printf ("  time=%s\n", context->file.time);
        }
    }

    for (uint32_t current_slr_index = 0; current_slr_index < context->num_slrs; current_slr_index++)
    {
        const x7_bitstream_slr_t *const slr = &context->slrs[current_slr_index];

        /* Display the byte index of the sync word, which indicates how much padding before the start of the bitstream */
        if (slr->sync_word_found)
        {
            if (context->num_slrs > 1)
            {
                /* Indicate the start of each SLR */
                printf ("SLR[%u] ", current_slr_index);
            }
            printf ("Sync word at byte index 0x%X\n", slr->sync_word_byte_index);
        }

        /* Summarise the configuration packets */
        uint32_t packet_index = 0;
        while (packet_index < slr->num_packets)
        {
            /* Produce a summary of configuration data packets */
            if (x7_packet_is_register_write (&slr->packets[packet_index], X7_PACKET_TYPE_1_REG_FAR))
            {
                x7_bitstream_summarise_configuration_data_writes (context, current_slr_index, &packet_index);
            }

            /* Display individual configuration packets which are not part of the configuration data */
            const x7_packet_record_t *const packet = &slr->packets[packet_index];
            switch (packet->header_type)
            {
            case X7_TYPE_1_PACKET:
                printf ("  Type 1 packet opcode %s",
                        x7_bitstream_lookup_enum (x7_packet_opcode_names, packet->opcode, unknown_opcode));
                if (x7_packet_is_nop (packet))
                {
                    /* For NOPs just display the number of consecutive NOPs as no meaningful contents */
                    uint32_t num_consecutive_nops = 1;
                    while ((packet_index < slr->num_packets) && (x7_packet_is_nop (&slr->packets[packet_index + 1])))
                    {
                        num_consecutive_nops++;
                        packet_index++;
                    }

                    if (num_consecutive_nops > 1)
                    {
                        printf (" (%u consecutive)\n", num_consecutive_nops);
                    }
                    else
                    {
                        printf ("\n");
                    }
                }
                else
                {
                    printf (" register %s",
                            x7_bitstream_lookup_enum (x7_packet_type_1_register_names, packet->register_address, unknown_register));
                    if (x7_packet_is_word_register_write (context, packet, X7_PACKET_TYPE_1_REG_CMD, &register_value))
                    {
                        /* Decode the name of the command written */
                        printf (" command %s\n",
                                x7_bitstream_lookup_enum (x7_command_register_code_names, register_value, unknown_command));
                    }
                    else if (x7_packet_is_word_register_write (context, packet, X7_PACKET_TYPE_1_REG_IDCODE, &register_value))
                    {
                        /* Display the name of the device */
                        printf (" %s\n", x7_bitstream_lookup_enum (x7_idcode_names, register_value, unknown_idcode));
                    }
                    else if (x7_packet_is_word_register_write (context, packet, X7_PACKET_TYPE_1_REG_AXSS, &register_value))
                    {
                        /* Display the user access register as both the decode build timestamp and raw value */
                        char formatted_timestamp[USER_ACCESS_TIMESTAMP_LEN];

                        format_user_access_timestamp (register_value, formatted_timestamp);
                        printf (" %08X - %s\n", register_value, formatted_timestamp);
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
                break;

            case X7_TYPE_2_PACKET:
                printf ("  Type 2 packet opcode %s word_count %u\n",
                        x7_bitstream_lookup_enum (x7_packet_opcode_names, packet->opcode, unknown_opcode), packet->word_count);
                break;
            }

            packet_index++;
        }
    }
}
