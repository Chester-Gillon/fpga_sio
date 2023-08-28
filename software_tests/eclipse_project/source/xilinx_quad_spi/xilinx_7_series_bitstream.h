/*
 * @file xilinx_7_series_bitstream.h
 * @date  20 Aug 2023
 * @author Chester Gillon
 * @brief Defines an interface for handling the bitstreams of Xilinx 7-series devices
 */

#ifndef XILINX_7_SERIES_BITSTREAM_H_
#define XILINX_7_SERIES_BITSTREAM_H_

#include "xilinx_quad_spi.h"

#include <limits.h>


/* Values for the header type field in the FPGA bitstream */
typedef enum
{
    /* Used for register reads and writes */
    X7_TYPE_1_PACKET = 1,
    /* The Type 2 packet, which must follow a Type 1 packet, is used to write long blocks. No address is
       presented here because it uses the previous Type 1 packet address. */
    X7_TYPE_2_PACKET = 2
} x7_packet_header_type_t;


/* The opcodes for X7_TYPE_1_PACKET */
typedef enum
{
    X7_PACKET_OPCODE_NOP      = 0,
    X7_PACKET_OPCODE_READ     = 1,
    X7_PACKET_OPCODE_WRITE    = 2,
    X7_PACKET_OPCODE_RESERVED = 3
} x7_packet_opcode_t;


/* The type 1 packet registers */
typedef enum
{
    X7_PACKET_TYPE_1_REG_CRC      = 0x00,
    X7_PACKET_TYPE_1_REG_FAR      = 0x01,
    X7_PACKET_TYPE_1_REG_FDRI     = 0x02,
    X7_PACKET_TYPE_1_REG_FDRO     = 0x03,
    X7_PACKET_TYPE_1_REG_CMD      = 0x04,
    X7_PACKET_TYPE_1_REG_CTL0     = 0x05,
    X7_PACKET_TYPE_1_REG_MASK     = 0x06,
    X7_PACKET_TYPE_1_REG_STAT     = 0x07,
    X7_PACKET_TYPE_1_REG_LOUT     = 0x08,
    X7_PACKET_TYPE_1_REG_COR0     = 0x09,
    X7_PACKET_TYPE_1_REG_MFWR     = 0x0A,
    X7_PACKET_TYPE_1_REG_CBC      = 0x0B,
    X7_PACKET_TYPE_1_REG_IDCODE   = 0x0C,
    X7_PACKET_TYPE_1_REG_AXSS     = 0x0D,
    X7_PACKET_TYPE_1_REG_COR1     = 0x0E,
    X7_PACKET_TYPE_1_REG_WBSTAR   = 0x10,
    X7_PACKET_TYPE_1_REG_TIMER    = 0x11,
    X7_PACKET_TYPE_1_REG_RBCRC_SW = 0x13,
    X7_PACKET_TYPE_1_REG_BOOTSTS  = 0x16,
    X7_PACKET_TYPE_1_REG_CTL1     = 0x18,
    X7_PACKET_TYPE_1_REG_BSPI     = 0x1F
} x7_packet_type_1_register_t;


/* The configuration Command Register Codes */
typedef enum
{
    X7_COMMAND_NULL        = 0x00,
    X7_COMMAND_WCFG        = 0x01,
    X7_COMMAND_MFW         = 0x02,
    X7_COMMAND_DGHIGH_LFRM = 0x03,
    X7_COMMAND_RCFG        = 0x04,
    X7_COMMAND_START       = 0x05,
    X7_COMMAND_RCAP        = 0x06,
    X7_COMMAND_RCRC        = 0x07,
    X7_COMMAND_AGHIGH      = 0x08,
    X7_COMMAND_SWITCH      = 0x09,
    X7_COMMAND_GRESTORE    = 0x0A,
    X7_COMMAND_SHUTDOWN    = 0x0B,
    X7_COMMAND_GCAPTURE    = 0x0C,
    X7_COMMAND_DESYNC      = 0x0D,
    X7_COMMAND_RESERVED    = 0x0E,
    X7_COMMAND_IPROG       = 0x0F,
    X7_COMMAND_CRCC        = 0x10,
    X7_COMMAND_LTIMER      = 0x11,
    X7_COMMAND_BSPI_READ   = 0x12,
    X7_COMMAND_FALL_EDGE   = 0x13
} x7_command_register_code_t;


/* Defines one configuration packet in the bitstream */
typedef struct
{
    /* The packet header type, which validates the other fields */
    x7_packet_header_type_t header_type;
    /* For X7_TYPE_1_PACKET the opcode for the packet */
    x7_packet_opcode_t opcode;
    /* For X7_TYPE_1_PACKET the register address targeted by the packet */
    x7_packet_type_1_register_t register_address;
    /* The number of 32-bit words following the packet header */
    uint32_t word_count;
    /* Offset into x7_bitstream_context_t->data_buffer for the 32-bit words following the packet header */
    uint32_t data_words_offset;
} x7_packet_record_t;


/* Defines the context specific for reading a bitstream from a file */
typedef struct
{
    /* The pathname of the bitstream file */
    char pathname[PATH_MAX];
    /* The contents of the raw bitstream file, possibly containing a .bit header */
    uint8_t *raw_contents;
    /* The number of bytes in raw_contents */
    uint32_t raw_length;
    /* When true raw_contents has a .bit format header.
     * When false assumed to a .bin format file. */
    bool bit_format_file;
    /* When bit_format_file is true the strings from the .bit file header */
    const char *design_name;
    const char *part_name;
    const char *date;
    const char *time;
} x7_bitstream_file_context_t;


/* Contains the context for reading a bitstream for a Xilinx 7-series device */
typedef struct
{
    /* Used to describe any error parsing the bitstream. Length allows for a pathname. */
    char error[PATH_MAX * 2];
    /* When non-NULL the Quad SPI controller to read the bitstream from flash */
    quad_spi_controller_context_t *controller;
    /* When reading the bitstream from flash, the start address in flash */
    uint32_t flash_start_address;
    /* When controller is NULL, the context for reading the bitstream from a file */
    x7_bitstream_file_context_t file;
    /* Buffer used to parse the bitstream from.
     * When reading from a SPI flash the buffer length is increased as search for the end of bitstream.
     * When reading from a file the entire file is read. */
    uint8_t *data_buffer;
    /* The current length of the data_buffer in bytes:
     * - When reading the bitstream from a file this is based upon the file length.
     * - When reading the bitstream from flash this grows in chunks read from the flash,
     *   which may end up more than bitstream_length_bytes */
    uint32_t data_buffer_length;
    /* The length of the bitstream in data_buffer, which ends at the last NOP seen after the end of the configuration */
    uint32_t bitstream_length_bytes;
    /* The byte index into data_buffer to read the next bitstream word from.
     * May not be aligned, it depends upon the alignment the Sync word was read from. */
    uint32_t next_word_index;
    /* Set true when the Sync word has been found */
    bool sync_word_found;
    /* The byte index into data_buffer that the Sync word was found */
    uint32_t sync_word_byte_index;
    /* Set true when has seen the end of the configuration in the bitstream.
     * When false have failed to parse a valid bitstream, and the following fields may contain an incomplete bitstream */
    bool end_of_configuration_seen;
    /* Dynamically sized array of configuration packets found in the bitstream */
    x7_packet_record_t *packets;
    /* The number of valid entries in packets[] */
    uint32_t num_packets;
    /* The current allocated size of the packets[] array */
    uint32_t packets_allocated_length;
} x7_bitstream_context_t;


void x7_bitstream_read_from_spi_flash (x7_bitstream_context_t *const context, quad_spi_controller_context_t *const controller,
                                       const uint32_t flash_start_address);
uint32_t x7_bitstream_unpack_word (const x7_bitstream_context_t *const context, const uint32_t word_index);
void x7_bitstream_read_from_file (x7_bitstream_context_t *const context, const char *const bitstream_pathname);
void x7_bitstream_free (x7_bitstream_context_t *const context);
void x7_bitstream_summarise (const x7_bitstream_context_t *const context);

#endif /* XILINX_7_SERIES_BITSTREAM_H_ */
