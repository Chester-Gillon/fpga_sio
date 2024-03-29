/*
 * @file i2c_dump_info.c
 * @date 8 May 2023
 * @author Chester Gillon
 * @details
 *   Use the /fpga_tests/i2c_probe FPGA image to dump information from the I2C devices on the Trenz Electronic TEF1001-02-B2IX4-A
 */

#include <stdlib.h>
#include <stdio.h>

#include "i2c_bit_banged.h"
#include "pmbus_access.h"
#include "ltm4676a_access.h"
#include "vfio_access.h"
#include "xilinx_xadc.h"
#include "identify_pcie_fpga_design.h"


/**
 * @brief Display information by reading the fan control register in the CPLD
 * @details https://wiki.trenz-electronic.de/display/PD/TEF1001+CPLD#TEF1001CPLD-FAN1 documents the register information.
 *
 *          This function uses a STOP after writing the register address, so the read of the register value is done
 *          using a START. I.e. a bit_banged_i2c_write() call followed by bit_banged_i2c_read()
 *
 *          An initial attempt to use bit_banged_i2c_read_byte_addressable_reg() which uses a repeated START failed
 *          to perform the read.
 *
 *          The TEF1001 board used for the test is a revision 2 board which uses a revision 3 CPLD.
 *          Looking the revision 3 CPLD source code in the i2c_ram.vhd source file the ST_DATA_IN state only
 *          supports looking for i2c_stop, i.e. doesn't support a repeated START after a write of the register address.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 */
static void dump_tef1001_fan_info (bit_banged_i2c_controller_context_t *const controller)
{
    const uint8_t i2c_slave_address = 0x74;
    const uint8_t fan_ctrl_reg_address = 0;
    const uint8_t fan1_rps_reg_address = 1;

    uint8_t reg_value;

    printf ("\nTEF1001 CPLD fan information:\n");

    if (bit_banged_i2c_write (controller, i2c_slave_address, sizeof (fan_ctrl_reg_address), &fan_ctrl_reg_address, true)
            == sizeof (fan_ctrl_reg_address))
    {
        if (bit_banged_i2c_read (controller, i2c_slave_address, sizeof (reg_value), &reg_value, true))
        {
            printf ("  FAN Control register = 0x%02x (fan %s)\n", reg_value, ((reg_value & 0x80) != 0) ? "Enabled" : "Disabled");
        }
        else
        {
            printf ("Failed to read FAN Control register\n");
        }
    }

    if (bit_banged_i2c_write (controller, i2c_slave_address, sizeof (fan1_rps_reg_address), &fan1_rps_reg_address, true)
            == sizeof (fan_ctrl_reg_address))
    {
        if (bit_banged_i2c_read (controller, i2c_slave_address, sizeof (reg_value), &reg_value, true))
        {
            /* The CPLD FAN1 RPS register counts the number of rising edges on the fan sense output per second.
               https://shop.trenz-electronic.de/en/25130-Heat-Sink-including-fan-for-Trenz-Electronic-TEB0911-and-TEF1001-Series
               says the fan type is "EFB0512HA", and is a 4 wire fan.

               https://media.digikey.com/pdf/Data%20Sheets/Delta%20PDFs/EFB0512HA-TP42_Spec.pdf is the specification for a 4-wire
               version of EFB0512HA which shows the motor is 4 poles and the fan sense output has two pulses for each rotation. */
            const uint32_t secs_per_minute = 60;
            const uint32_t fan_sense_pulses_per_rotation = 2;
            const uint32_t fan_rpm = ((uint32_t) reg_value) * (secs_per_minute / fan_sense_pulses_per_rotation);

            printf ("  FAN1 RPM = %u\n", fan_rpm);
        }
        else
        {
            printf ("Failed to read FAN1 Revolutions per second register\n");
        }
    }
}


/**
 * @brief Read one 16-bit register in a DDR temperature sensor
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] reg_address Which temperature sensor register to read
 * @param[out] reg_value The value of the register
 * @return Returns true of the register was read, or false if a NACK from the I2C slave.
 */
static bool read_ddr_temperature_register (bit_banged_i2c_controller_context_t *const controller, const uint8_t reg_address,
                                           uint16_t *const reg_value)
{
    bool success;
    const uint8_t i2c_slave_address = 0x19;
    uint8_t data[sizeof (uint16_t)];

    success = bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address, reg_address, sizeof (data), data);
    if (success)
    {
        /* Convert the bytes read as big-endian to native format */
        *reg_value = (uint16_t) ((data[0] << 8u) | data[1]);
    }

    return success;
}


/**
 * @brief Display the value read from a DDR temperature sensor in degrees-C
 * @param[in] reg_value The register value to display in degrees-C
 */
static void decode_ddr_temperature (const uint16_t reg_value)
{
    const uint32_t temperature_setting_mask =     0x0FFF;
    const uint32_t temperature_sign_mask    =     0x1000;
    const uint32_t sign_extension           = 0xFFFFF000;
    uint32_t unsigned_temp;
    int32_t signed_temp;
    const double temperature_scaling = 0.0625; /* 12 bits, with least significant bit 0.0625 C */

    unsigned_temp = reg_value & temperature_setting_mask;
    if ((reg_value & temperature_sign_mask) != 0)
    {
        unsigned_temp |= sign_extension;
    }
    signed_temp = (int32_t) unsigned_temp;
    printf ("(%.1lf C)", (double) signed_temp * temperature_scaling);
}


/**
 * @brief Display information from a DDR temperature sensor, for the DDR3 module fitted in the TEF1001.
 * @details JEDEC Standard No. 21-C 4.1.4 "Definition of the TSE2002av Serial Presence Detect (SPD) EEPROM with
 *           Temperature Sensor (TS) for Memory Module Applications" was used to obtain the register definitions.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] Used to look up the sensor manufacturer
 */
static void dump_ddr_temperature_information (bit_banged_i2c_controller_context_t *const controller,
                                              struct pci_access *const pacc)
{
    char vendor_name[256];
    const uint8_t capabilities_reg_address = 0x00;
    const uint8_t configuration_reg_address = 0x01;
    const uint8_t high_limit_reg_address = 0x02;
    const uint8_t low_limit_reg_address = 0x03;
    const uint8_t tcrit_limit_reg_address = 0x04;
    const uint8_t ambient_temperature_reg_address = 0x05;
    const uint8_t manufacter_id_reg_address = 0x6;

    uint16_t reg_value;

    printf ("\nDDR temperature sensor information:\n");

    if (read_ddr_temperature_register (controller, capabilities_reg_address, &reg_value))
    {
        printf ("  Capabilities = 0x%04x\n", reg_value);
    }
    else
    {
        printf ("Failed to read DDR temperature Capabilities\n");
    }

    if (read_ddr_temperature_register (controller, configuration_reg_address, &reg_value))
    {
        printf ("  Configuration register = 0x%04x\n", reg_value);
    }
    else
    {
        printf ("Failed to read DDR configuration register\n");
    }

    if (read_ddr_temperature_register (controller, high_limit_reg_address, &reg_value))
    {
        printf ("  Temperature high limit register = 0x%04x ", reg_value);
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR temperature high limit\n");
    }

    if (read_ddr_temperature_register (controller, low_limit_reg_address, &reg_value))
    {
        printf ("  Temperature low limit register = 0x%04x ", reg_value);
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR temperature low limit\n");
    }

    if (read_ddr_temperature_register (controller, tcrit_limit_reg_address, &reg_value))
    {
        printf ("  Temperature critical limit register = 0x%04x ", reg_value);
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR temperature critical limit\n");
    }

    if (read_ddr_temperature_register (controller, ambient_temperature_reg_address, &reg_value))
    {
        /* Display the ambient temperature value, along with any alert flags */
        const uint16_t tcrit_mask = 0x8000;
        const uint16_t high_mask = 0x4000;
        const uint16_t low_mask = 0x2000;

        printf ("  Ambient temperature register = 0x%04x%s%s%s ",
                reg_value,
                (reg_value & tcrit_mask) != 0 ? " above TCRIT" : "",
                (reg_value & high_mask) != 0 ? " above HIGH" : "",
                (reg_value & low_mask) != 0 ? " below LOW" : "");
        decode_ddr_temperature (reg_value);
        printf ("\n");
    }
    else
    {
        printf ("Failed to read DDR ambient temperature\n");
    }

    if (read_ddr_temperature_register (controller, manufacter_id_reg_address, &reg_value))
    {
        pci_lookup_name (pacc, vendor_name, sizeof (vendor_name), PCI_LOOKUP_VENDOR, reg_value);
        printf ("  Sensor manufacturer ID register = 0x%04x (%s)\n", reg_value, vendor_name);
    }
    else
    {
        printf ("Failed to read DDR temperature sensor manufacturer ID\n");
    }
}


/**
 * @brief Extract one bit-field from a DDR3 SPD byte
 * @param[in] spd_byte The byte to extract the field from
 * @param[in] field_width_bits The width of the field in bits
 * @param[in] field_lsb The least significant bit of the field
 * @return The extracted field value
 */
static inline uint32_t ddr3_spd_extract_field (const uint8_t spd_byte, const uint32_t field_width_bits, const uint32_t field_lsb)
{
    const uint32_t field_mask = (1u << field_width_bits) - 1u;

    return (((uint32_t) spd_byte) >> field_lsb) & field_mask;
}


/**
 * @brief Calculate the 2 byte CRC for the contents of the DDR3 SPD
 * @detail Algortihm from JEDEC standard No. 21-C 4.1.2.11 - 1
 * @param[in] crc_coverage_bytes The number of SPD bytes covered by the CRC
 * @param[in] spd_bytes The SPD bytes to calculate the CRC for
 * @return The calculated CRC
 */
static uint32_t calculate_spd_crc (const uint32_t crc_coverage_bytes, const uint8_t spd_bytes[const crc_coverage_bytes])
{
    uint32_t crc;
    uint32_t byte_index;
    uint32_t bit_index;

    crc = 0;
    for (byte_index = 0; byte_index < crc_coverage_bytes; byte_index++)
    {
        crc = crc ^ (uint32_t) spd_bytes[byte_index] << 8;
        for (bit_index = 0; bit_index < 8; bit_index++)
        {
            if ((crc & 0x8000) != 0)
            {
                crc = crc << 1 ^ 0x1021;
            }
            else
            {
                crc = crc << 1;
            }
        }
    }

    return crc & 0xffff;
}


/**
 * @brief Decode the DDR3 SPD information, for the DDR3 module fitted in the TEF1001.
 * @details JEDEC standard No. 21-C 4.1.2.11 - 1 "Serial Presence Detect (SPD) for DDR3 SDRAM Modules DDR3 SPD"
 *          Document Release 6 was used to obtain the register definitions.
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 */
static void dump_ddr3_spd_information (bit_banged_i2c_controller_context_t *const controller)
{
    const uint8_t i2c_slave_address = 0x51;
    uint8_t ddr3_spd[256] = {0};
    const uint8_t start_address = 0;
    bool decode_valid = true;

    /* Read the entire 256 byte DDR3 SPD contents */
    if (bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address, start_address, sizeof (ddr3_spd), ddr3_spd))
    {
        printf ("\nDDR3 SPD decode:\n");

        /* Display the RAW SPD bytes, to allow debugging of the decode. Byte indices in decimal to match JEDEC standard */
        printf ("  RAW bytes:");
        for (uint32_t byte_index = 0; byte_index < sizeof (ddr3_spd); byte_index++)
        {
            if ((byte_index % 16) == 0)
            {
                printf ("\n    %3u-%3u:", byte_index, byte_index + 15);
            }
            printf (" %02x", ddr3_spd[byte_index]);
        }
        printf ("\n");

        /* Extract the number of bytes before attempting a decode */
        const uint8_t bytes_used_info = ddr3_spd[0];
        const uint32_t crc_coverage_bytes_field = ddr3_spd_extract_field (bytes_used_info, 1, 7);
        const uint32_t spd_bytes_total_field = ddr3_spd_extract_field (bytes_used_info, 3, 4);
        const uint32_t spd_bytes_used_field = ddr3_spd_extract_field (bytes_used_info, 4, 0);
        const uint32_t crc_coverage_bytes = (crc_coverage_bytes_field == 1) ? 117 : 126;
        uint32_t spd_bytes_total = 0;
        uint32_t spd_bytes_used = 0;

        switch (spd_bytes_total_field)
        {
        case 1: spd_bytes_total = 256; break;
        default: decode_valid = false; break;
        }

        switch (spd_bytes_used_field)
        {
        case 1: spd_bytes_used = 128; break;
        case 2: spd_bytes_used = 176; break;
        case 3: spd_bytes_used = 256; break;
        default: decode_valid = false; break;
        }

        if (!decode_valid)
        {
            printf ("  Unable to decode bytes_used_info=0x%x\n", bytes_used_info);
            return;
        }

        printf ("  CRC coverage bytes=%u  SPD bytes total=%u  SPD bytes used=%u\n",
                crc_coverage_bytes, spd_bytes_total, spd_bytes_used);

        /* Validate the SPD CRC before continuing */
        const uint32_t actual_crc = calculate_spd_crc (crc_coverage_bytes, ddr3_spd);
        const uint32_t expected_crc = (uint32_t) ddr3_spd[126] | ((uint32_t) ddr3_spd[127] << 8);
        decode_valid = actual_crc == expected_crc;
        printf ("  Actual CRC=0x%04x  Expected CRC=0x%04x : CRC %s\n", actual_crc, expected_crc, decode_valid ? "PASS" : "FAIL");
        if (!decode_valid)
        {
            return;
        }

        /* Display the SPD revision, which uses BCD fields.
         * This is for information, the actual revision isn't used to control the following decoding */
        const uint8_t spd_revision = ddr3_spd[1];
        const uint32_t encoding_level_field = ddr3_spd_extract_field (spd_revision, 4, 4);
        const uint32_t additions_level_field = ddr3_spd_extract_field (spd_revision, 4, 0);
        printf ("  SPD revision %u.%u\n", encoding_level_field, additions_level_field);

        /* Verify that is a DDR3 DRAM device type, as that is the only type this function is set to decode */
        const uint8_t spd_dram_type = ddr3_spd[2];
        const uint8_t ddr3_dram_type = 0xb;
        if (spd_dram_type != ddr3_dram_type)
        {
            printf ("  Unexpected DRAM type 0x%x - this function only handles DDR3\n", spd_dram_type);
            return;
        }

        /* Display the module type */
        const uint8_t module_type_byte = ddr3_spd[3];
        const uint32_t module_type_field = ddr3_spd_extract_field (module_type_byte, 4, 0);
        const char *module_type = NULL;
        switch (module_type_field)
        {
        case 1: module_type = "RDIMM"; break;
        case 2: module_type = "UDIMM"; break;
        case 3: module_type = "SO-DIMM"; break;
        case 4: module_type = "Micro-DIMM"; break;
        case 5: module_type = "Mini-RDIMM"; break;
        case 6: module_type = "Mini-UDIMM"; break;
        case 7: module_type = "Mini-CDIMM"; break;
        case 8: module_type = "72b-SO-UDIMM"; break;
        case 9: module_type = "72b-SO-RDIMM"; break;
        case 10: module_type = "72b-SO-CDIMM"; break;
        case 11: module_type = "LRDIMM"; break;
        case 12: module_type = "16b-SO-DIMM"; break;
        case 13: module_type = "32b-SO-DIMM"; break;
        default: decode_valid = false; break;
        }
        if (!decode_valid)
        {
            printf ("  Unable to decode module_type_byte=0x%x\n", module_type_byte);
            return;
        }
        printf ("  Module type : %s\n", module_type);

        /* Display the SDRAM Density and Banks */
        const uint8_t density_and_banks_byte = ddr3_spd[4];
        const uint32_t bank_address_bits_field = ddr3_spd_extract_field (density_and_banks_byte, 3, 4);
        const uint32_t total_sdram_capacity_field = ddr3_spd_extract_field (density_and_banks_byte, 4, 0);
        uint32_t bank_address_bits = 0;
        uint32_t total_sdram_capacity_megabits = 0;
        switch (bank_address_bits_field)
        {
        case 0: bank_address_bits = 3; break;
        case 1: bank_address_bits = 4; break;
        case 2: bank_address_bits = 5; break;
        case 3: bank_address_bits = 6; break;
        default: decode_valid = false;
        }
        switch (total_sdram_capacity_field)
        {
        case 0: total_sdram_capacity_megabits = 256; break;
        case 1: total_sdram_capacity_megabits = 512; break;
        case 2: total_sdram_capacity_megabits = 1024; break;
        case 3: total_sdram_capacity_megabits = 2048; break;
        case 4: total_sdram_capacity_megabits = 4096; break;
        case 5: total_sdram_capacity_megabits = 8192; break;
        case 6: total_sdram_capacity_megabits = 16384; break;
        default: decode_valid = false;
        }
        if (!decode_valid)
        {
            printf ("  Unable to decode density_and_banks_byte=0x%x\n", density_and_banks_byte);
            return;
        }
        printf ("  Bank Address Bits=%u  Total SDRAM Capacity (Mibibits)=%u\n", bank_address_bits, total_sdram_capacity_megabits);

        /* Display the SDRAM addressing */
        const uint8_t sdram_addressing_byte = ddr3_spd[5];
        const uint32_t row_address_bits_field = ddr3_spd_extract_field (sdram_addressing_byte, 3, 3);
        const uint32_t column_address_bits_field = ddr3_spd_extract_field (sdram_addressing_byte, 3, 0);
        uint32_t row_address_bits = 0;
        uint32_t column_address_bits = 0;
        switch (row_address_bits_field)
        {
        case 0: row_address_bits = 12; break;
        case 1: row_address_bits = 13; break;
        case 2: row_address_bits = 14; break;
        case 3: row_address_bits = 15; break;
        case 4: row_address_bits = 16; break;
        default: decode_valid = false;
        }
        switch (column_address_bits_field)
        {
        case 0: column_address_bits = 9; break;
        case 1: column_address_bits = 10; break;
        case 2: column_address_bits = 11; break;
        case 3: column_address_bits = 12; break;
        default: decode_valid = false;
        }
        if (!decode_valid)
        {
            printf ("  Unable to decode sdram_addressing_byte=0x%x\n", sdram_addressing_byte);
            return;
        }
        printf ("  Row Address Bits=%u  Column Addess Bits=%u\n", row_address_bits, column_address_bits);

        /* Display the Module Nominal Voltage - which are encoded as single bit flags */
        const uint8_t module_nominal_voltage_byte = ddr3_spd[6];
        printf ("  Module Nominal Voltage : %s  %s  %s\n",
                ddr3_spd_extract_field (module_nominal_voltage_byte, 1, 2) ? "1.25 V operable" : "NOT 1.25 V operable",
                ddr3_spd_extract_field (module_nominal_voltage_byte, 1, 1) ? "1.35 V operable" : "NOT 1.35 V operable",
                ddr3_spd_extract_field (module_nominal_voltage_byte, 1, 0) ? "NOT 1.5 V operable" : "1.5 V operable");

        /* Display the Module Organisation */
        const uint8_t module_organisation_byte = ddr3_spd[7];
        const uint32_t number_of_ranks_field = ddr3_spd_extract_field (module_organisation_byte, 3, 3);
        const uint32_t sdram_device_width_field = ddr3_spd_extract_field (module_organisation_byte, 3, 0);
        uint32_t number_of_ranks = 0;
        uint32_t sdram_device_width = 0;
        switch (number_of_ranks_field)
        {
        case 0: number_of_ranks = 1; break;
        case 1: number_of_ranks = 2; break;
        case 2: number_of_ranks = 3; break;
        case 3: number_of_ranks = 4; break;
        case 4: number_of_ranks = 8; break;
        default: decode_valid = false; break;
        }
        switch (sdram_device_width_field)
        {
        case 0: sdram_device_width = 4; break;
        case 1: sdram_device_width = 8; break;
        case 2: sdram_device_width = 16; break;
        case 3: sdram_device_width = 32; break;
        default: decode_valid = false; break;
        }
        if (!decode_valid)
        {
            printf ("  Unable to decode module_organisation_byte=0x%x\n", module_organisation_byte);
            return;
        }
        printf ("  Number of Ranks=%u  SDRAM Device Width (bits)=%u\n", number_of_ranks, sdram_device_width);

        /* Display the Module Memory Bus Width */
        const uint8_t module_bus_width_byte = ddr3_spd[8];
        const uint32_t bus_width_extension_field = ddr3_spd_extract_field (module_bus_width_byte, 2, 3);
        const uint32_t primary_bus_width_field = ddr3_spd_extract_field (module_bus_width_byte, 3, 0);
        uint32_t bus_width_extension = 0;
        uint32_t primary_bus_width  = 0;
        switch (bus_width_extension_field)
        {
        case 0: bus_width_extension = 0; break;
        case 1: bus_width_extension = 8; break;
        default: decode_valid = false; break;
        }
        switch (primary_bus_width_field)
        {
        case 0: primary_bus_width = 8; break;
        case 1: primary_bus_width = 16; break;
        case 2: primary_bus_width = 32; break;
        case 3: primary_bus_width = 64; break;
        default: decode_valid = false; break;
        }
        if (!decode_valid)
        {
            printf ("  Unable to decode module_bus_width_byte=0x%x\n", module_bus_width_byte);
            return;
        }
        printf ("  Bus width extension (bits)=%u  Primary bus width (bits)=%u\n", bus_width_extension, primary_bus_width);

        /* Calculate the module capacity, using the equation in JEDEC standard No. 21-C 4.1.2.11 - 1 */
        const uint32_t module_capacity_megabytes =
                total_sdram_capacity_megabits / 8 * primary_bus_width / sdram_device_width * number_of_ranks;
        printf ("  Module capacity (MiB)=%u\n", module_capacity_megabytes);

        /* Display the Module Part Number which is ASCII, right padded with spaces */
        printf ("  Module Part Number \"%.*s\"\n", 18, &ddr3_spd[128]);

        /* Display module specific bytes */
        switch (module_type_field)
        {
        case 2: /* UDIMM */
        case 3: /* SO-DIMM */
        case 4: /* Micro-DIMM */
        case 6: /* Mini-UDIMM */
        case 8: /* 72b-SO-UDIMM */
        case 12: /* 16b-SO-DIMM */
        case 13: /* 32b-SO-DIMM */
            {
                /* Only need to display the Address Mapping from Edge Connector to DRAM field; the mechanical dimensions
                 * are not important for programming a DDR3 controller. */
                const uint8_t address_mapping_byte = ddr3_spd[63];
                const uint32_t rank_1_mapping_field = ddr3_spd_extract_field (address_mapping_byte, 1, 0);
                printf ("  %s Rank 1 Mapping : %s\n", module_type, rank_1_mapping_field ? "mirrored" : "standard");
            }
            break;

        default:
            printf ("  Module specific decoding not implemented for module type %s\n", module_type);
            break;
        }

        /* Display supported CAS latencies, which is a bit mask */
        const uint32_t cas_latencies_supported_low_byte = ddr3_spd[14];
        const uint32_t cas_latencies_supported_high_byte = ddr3_spd[15];
        const uint32_t cas_latencies_supported_mask = (cas_latencies_supported_high_byte << 8) | cas_latencies_supported_low_byte;
        const uint32_t num_cas_latency_bits = 15;
        const uint32_t bit_to_cas_latency_offset = 4;
        printf ("  Supported CAS latencies:");
        for (uint32_t latency_bit = 0; latency_bit < num_cas_latency_bits; latency_bit++)
        {
            if ((cas_latencies_supported_mask & (1u << latency_bit)) != 0)
            {
                printf (" %u", latency_bit + bit_to_cas_latency_offset);
            }
        }
        printf ("\n");

        /* Display the timebases */
        const uint8_t fine_timebase_byte = ddr3_spd[9];
        const uint32_t fine_timebase_dividend = ddr3_spd_extract_field (fine_timebase_byte, 4, 4);
        const uint32_t fine_timebase_divisor = ddr3_spd_extract_field (fine_timebase_byte, 4, 0);
        const double ftb_ns = 1E-3 * ((double) fine_timebase_dividend / (double) fine_timebase_divisor);
        printf ("  Fine Timebase Dividend=%u Divisor=%u FTB=%.3f ns\n", fine_timebase_dividend, fine_timebase_divisor, ftb_ns);

        const uint8_t medium_timebase_dividend = ddr3_spd[10];
        const uint8_t medium_timebase_divisor = ddr3_spd[11];
        const double mtb_ns = (double) medium_timebase_dividend / (double) medium_timebase_divisor;
        printf ("  Medium Timebase Dividend=%u Divisor=%u MTB=%.3f ns\n", medium_timebase_dividend, medium_timebase_divisor, mtb_ns);

        /* Display times computed from multiples of medium/fine timebases */
        const uint8_t tCKmin_mtb_units = ddr3_spd[12];
        const int8_t tCKmin_ftb_units = (int8_t) ddr3_spd[34];
        const double tCKmin = (mtb_ns * tCKmin_mtb_units) + (ftb_ns * tCKmin_ftb_units);
        printf ("  SDRAM Minimum Cycle Time (tCKmin)=%.3f ns\n", tCKmin);

        const uint8_t tAAmin_mtb_units = ddr3_spd[16];
        const int8_t tAAmin_ftb_units = (int8_t) ddr3_spd[35];
        const double tAAmin = (mtb_ns * tAAmin_mtb_units) + (ftb_ns * tAAmin_ftb_units);
        printf ("  Minimum CAS Latency Time (tAAmin)=%.3f ns\n", tAAmin);

        const uint8_t tWRmin_mtb_units = ddr3_spd[17];
        const double tWRmin = mtb_ns * tWRmin_mtb_units;
        printf ("  Minimum Write Recovery Time (tWRmin)=%0.3f ns\n", tWRmin);

        const uint8_t tRCDmin_mtb_units = ddr3_spd[18];
        const int8_t tRCDmin_ftb_units = (int8_t) ddr3_spd[36];
        const double tRCDmin = (mtb_ns * tRCDmin_mtb_units) + (ftb_ns * tRCDmin_ftb_units);
        printf ("  Minimum RAS# to CAS# Delay Time (tRCDmin)=%0.3f ns\n", tRCDmin);

        const uint8_t tRRDmin_mtb_units = ddr3_spd[19];
        const double tRRDmin = mtb_ns * tRRDmin_mtb_units;
        printf ("  Minimum Row Active to Row Active Delay Time (tRRDmin)=%0.3f ns\n", tRRDmin);

        const uint8_t tRPmin_mtb_units = ddr3_spd[20];
        const int8_t tRPmin_ftb_units = (int8_t) ddr3_spd[37];
        const double tRPmin = (mtb_ns * tRPmin_mtb_units) + (ftb_ns * tRPmin_ftb_units);
        printf ("  Minimum Row Precharge Delay Time (tRPmin)=%0.3f ns\n", tRPmin);

        const uint8_t tRAS_tRC_upper_nibbles_byte = ddr3_spd[21];
        const uint32_t tRASmin_msn_mtb_units = ddr3_spd_extract_field (tRAS_tRC_upper_nibbles_byte, 4, 0);
        const uint32_t tRCmin_msn_mtb_units  = ddr3_spd_extract_field (tRAS_tRC_upper_nibbles_byte, 4, 4);

        const uint32_t tRASmin_mtb_units = (tRASmin_msn_mtb_units << 8) | ddr3_spd[22];
        const double tRASmin = mtb_ns * tRASmin_mtb_units;
        printf ("  Minimum Active to Precharge Delay Time (tRASmin)=%0.3f ns\n", tRASmin);

        const uint32_t tRCmin_mtb_units = (tRCmin_msn_mtb_units << 8) | ddr3_spd[23];
        const int8_t tRCmin_ftb_units = (int8_t) ddr3_spd[38];
        const double tRCmin = (mtb_ns * tRCmin_mtb_units) + (ftb_ns * tRCmin_ftb_units);
        printf ("  Minimum Active to Active/Refresh Delay Time (tRCmin)=%0.3f ns\n", tRCmin);

        const uint32_t tRFCmin_lsb_mtb_units = ddr3_spd[24];
        const uint32_t tRFCmin_msb_mtb_units = ddr3_spd[25];
        const uint32_t tRFCmin_mtb_units = (tRFCmin_msb_mtb_units << 8) | tRFCmin_lsb_mtb_units;
        const double tRFCmin = mtb_ns * tRFCmin_mtb_units;
        printf ("  Minimum Refresh Recovery Delay Time (tRFCmin)=%0.3f ns\n", tRFCmin);

        const uint8_t tWTRmin_mtb_units = ddr3_spd[26];
        const double tWTRmin = mtb_ns * tWTRmin_mtb_units;
        printf ("  Minimum Internal Write to Read Command Delay Time (tWTRmin)=%0.3f ns\n", tWTRmin);

        const uint8_t tRTPmin_mtb_units = ddr3_spd[27];
        const double tRTPmin = mtb_ns * tRTPmin_mtb_units;
        printf ("  Minimum Internal Read to Precharge Command Delay Time (tRTPmin)=%0.3f ns\n", tRTPmin);

        const uint32_t tFAWmin_msn_mtb_units = ddr3_spd_extract_field (ddr3_spd[28], 4, 0);
        const uint32_t tFAWmin_mtb_units = (tFAWmin_msn_mtb_units << 8) | ddr3_spd[29];
        const double tFAWmin = mtb_ns * tFAWmin_mtb_units;
        printf ("  Minimum Four Activate Window Delay Time (tFAWmin)=%0.3f ns\n", tFAWmin);

        /* Display SDRAM optional features */
        const uint8_t optional_features_mask = ddr3_spd[30];
        printf ("  SDRAM Optional Features : DLL-Off Mode Support %s  RZQ/7 %s  RZQ/6 %s\n",
                ddr3_spd_extract_field (optional_features_mask, 1, 7) ? "Supported" : "Not Supported",
                ddr3_spd_extract_field (optional_features_mask, 1, 1) ? "Supported" : "Not Supported",
                ddr3_spd_extract_field (optional_features_mask, 1, 0) ? "Supported" : "Not Supported");

        /* Display SDRAM Thermal and Refresh Options */
        const uint8_t thermal_and_refresh_mask = ddr3_spd[31];
        printf ("  SDRAM Thermal and Refresh Options:\n");
        printf ("    Partial Array Self Refresh (PASR) : %s\n",
                ddr3_spd_extract_field (thermal_and_refresh_mask, 1, 7) ? "Supported" : "Not Supported");
        printf ("    On-die Thermal Sensor (ODTS) Readout : %s\n",
                ddr3_spd_extract_field (thermal_and_refresh_mask, 1, 3) ?
                        "On-die thermal sensor readout is supported" : "On-die thermal sensor readout is not supported");
        printf ("    Auto Self Refresh (ASR) : %s\n",
                ddr3_spd_extract_field (thermal_and_refresh_mask, 1, 2) ?
                        "ASR is supported and the SDRAM will determine the proper refresh rate for any supported temperature" :
                        "ASR is not supported");
        printf ("    Extended Temperature Refresh Rate : %s\n",
                ddr3_spd_extract_field (thermal_and_refresh_mask, 1, 1) ?
                        "Extended operating temperature range from 85-95 C supported with standard 1X refresh rate" :
                        "Use in extended operating temperature range from 85-95 C requires 2X refresh rate");
        printf ("    Extended Temperature Range : %s\n",
                ddr3_spd_extract_field (thermal_and_refresh_mask, 1, 0) ?
                        "Normal and extended operating temperature range 0-95 C supported" :
                        "Normal operating temperature range 0-85 C supported");

        /* Display supported thermal options */
        const uint8_t thermal_options = ddr3_spd[32];
        printf ("  Thermal Sensor : %s\n", ddr3_spd_extract_field (thermal_options, 1, 7) ?
                "Thermal sensor incorporated onto this assembly" : "Thermal sensor not incorporated onto this assembly");

        /* Display SDRAM Device Type */
        const uint8_t device_type_byte = ddr3_spd[33];
        const uint32_t device_type_field = ddr3_spd_extract_field (device_type_byte, 1, 7);
        const uint32_t die_count_field = ddr3_spd_extract_field (device_type_byte, 3, 6);
        const uint32_t signal_loading_field = ddr3_spd_extract_field (device_type_byte, 2, 0);
        const char *die_count = NULL;
        const char *signal_loading = NULL;
        switch (die_count_field)
        {
        case 0 : die_count = "Not Specified"; break;
        case 1 : die_count = "Single die"; break;
        case 2 : die_count = "2 die"; break;
        case 3 : die_count = "4 die"; break;
        case 4 : die_count = "8 die"; break;
        default: decode_valid = false; break;
        }

        switch (signal_loading_field)
        {
        case 0: signal_loading = "Not specified"; break;
        case 1: signal_loading = "Multi load stack"; break;
        case 2: signal_loading = "Single load stack"; break;
        default: decode_valid = false; break;
        }

        if (!decode_valid)
        {
            printf ("  Unable to decode device_type_byte=0x%x\n", device_type_byte);
            return;
        }

        printf ("  SDRAM Device Type : %s\n", device_type_field ? "Non-Standard Device" : "Standard Monolithic DRAM Device");
        printf ("  Die Count : %s\n", die_count);
        printf ("  Signal Loading : %s\n", signal_loading);

        /* Display SDRAM Maximum Active Count (MAC) Value.
         * tMAW is described as multiples of tREFI, but the value of tREFI isn't defined in the SPD information.
         * Need to find the datasheet for the underlying DDR3 device to get the value of tREFI. */
        const uint8_t mac_byte = ddr3_spd[41];
        const uint32_t vendor_specific_field = ddr3_spd_extract_field (mac_byte, 2, 6);
        const uint32_t tMAW_field = ddr3_spd_extract_field (mac_byte, 2, 4);
        const uint32_t MAC_field = ddr3_spd_extract_field (mac_byte, 4, 0);
        const char *tMAW = NULL;
        const char *MAC = NULL;

        switch (tMAW_field)
        {
        case 0: tMAW = "8192 * tREFI"; break;
        case 1: tMAW = "4096 * tREFI"; break;
        case 2: tMAW = "2048 * tREFI"; break;
        default: decode_valid = false; break;
        }

        switch (MAC_field)
        {
        case 0: MAC = "Untested MAC"; break;
        case 1: MAC = "700 K"; break;
        case 2: MAC = "600 K"; break;
        case 3: MAC = "500 K"; break;
        case 4: MAC = "400 K"; break;
        case 5: MAC = "300 K"; break;
        case 6: MAC = "200 K"; break;
        case 8: MAC = "Unrestricted MAC"; break;
        default: decode_valid = false; break;
        }

        if (!decode_valid)
        {
            printf ("  Unable to decode mac_byte=0x%x\n", mac_byte);
            return;
        }

        printf ("  Maximum Activate : Vendor Specific=%u  Maximum Activate Window (tMAW)=%s  Maximum Activate Count (MAC)=%s\n",
                vendor_specific_field, tMAW, MAC);
    }
    else
    {
        printf ("Failed to read DDR3 SPD\n");
    }
}


/**
 * @brief Write to the PAGE_SEL register in a Si5338 to select the page of registers to access.
 * @details This is necessary as the register address is only a byte, but the Si5338 has more than 256 registers
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 * @param[in] i2c_slave_address Address of the Si5338 to set PAGE_SEL in
 * @param[in] page_sel Value of PAGE_SEL to write
 * @return Returns true if the write was successful
 */
static bool si5338_select_page (bit_banged_i2c_controller_context_t *const controller,
                                const uint8_t i2c_slave_address, const uint8_t page_sel)
{
    bool success;
    const uint8_t page_sel_reg_address = 0xff;
    const uint8_t write_data[] = {page_sel_reg_address, page_sel};
    uint8_t page_sel_readback;

    /* Write the the PAGE_SEL register */
    success = bit_banged_i2c_write (controller, i2c_slave_address, sizeof (write_data), write_data, true);

    /* Readback the PAGE_SEL register to check took effect */
    if (success)
    {
        success = bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address, page_sel_reg_address,
                sizeof (page_sel_readback), &page_sel_readback);
    }

    if (success)
    {
        success = page_sel_readback == page_sel;
        if (!success)
        {
            printf ("Wrote %u to PAGE_SEL, but readback %u\n", page_sel, page_sel_readback);
        }
    }
    else
    {
        printf ("Failed to modify PAGE_SEL\n");
    }

    return success;
}


/**
 * @brief Read all Si5338 registers to test communication, and decode registers related to the device identity.
 * @details As the Si5338 OTP is delivered blank, expect that the Si5338 registers related to clock outputs will be the
 *          reset values which leave the clock outputs disabled.
 *
 *          The registers are defined in
 *          https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/reference-manuals/Si5338-RM.pdf
 * @param[in/out] controller The controller for the GPIO bit-banged interface
 */
static void dump_si5338_information (bit_banged_i2c_controller_context_t *const controller)
{
    bool success;
    const uint8_t i2c_slave_address = 0x70;
    uint8_t all_registers[351] = {0}; /* Si5338-RM shows register addresses go up to 350 */

    printf ("\nSi5338 Clock Generator information:\n");

    /* Read the registers in page 1 */
    success = si5338_select_page (controller, i2c_slave_address, 1);
    if (success)
    {
        const size_t upper_reg_start_offset = 256;
        success = bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address,
                0, sizeof (all_registers) - upper_reg_start_offset, &all_registers[upper_reg_start_offset]);
    }

    /* Read the registers in page 0 */
    if (success)
    {
        success = si5338_select_page (controller, i2c_slave_address, 0);
        if (success)
        {
            success = bit_banged_i2c_read_byte_addressable_reg (controller, i2c_slave_address,
                    0, 256, all_registers);
        }
    }

    if (success)
    {
        /* Display the device identity */
        const int device_revision_id = ((char) (all_registers[0] & 0x7)) + 'A';
        const uint32_t base_part_number = all_registers[2] & 0x3f;
        const int device_grade = ((char) ((all_registers[3] & 0xf1) >> 3)) + 'A';
        printf ("  Device: Si53%u%c revision %c\n", base_part_number, device_grade, device_revision_id);

        /* Display the NVM code, which is expected to be zero as the TEF1001 documentation says the Si5338A is delivered
         * with the "OTP Area" not programmed. */
        const uint32_t nvm_code = (((uint32_t) all_registers[3] & 0x1) << 16) |
                (((uint32_t) all_registers[4]) << 16) |
                ((uint32_t) all_registers[5]);
        printf ("  NVM code=%u\n", nvm_code);

        /* Display the configured I2C address from the register, which should match the i2c_slave_address constant
         * as otherwise wouldn't be able to communicate with the Si5338A. */
        const uint32_t configured_i2c_address = all_registers[27] & 0x7f;
        printf ("  Configured 7-bit I2C address=0x%x\n", configured_i2c_address);
    }
    else
    {
        printf ("Failed to read Si5338 registers\n");
    }
}


/**
 * @brief Dump information from I2C devices TEF1001-02-B2IX4-A
 * @param[in/out] design The FPGA design containing the peripherals to use for the probe
 * @param[in] pacc Used to lookup vendor IDs.
 */
static void dump_tef1001_information (fpga_design_t *const design, struct pci_access *const pacc)
{
    bit_banged_i2c_controller_context_t controller = {0};

    /* The I2C address of the two DCDC LTM4676A regulators */
    const uint8_t u3_ltm4676a_i2c_slave_address = 0x40; /* provides 4V and 1.5V */
    const uint8_t u4_ltm4676a_i2c_slave_address = 0x4f; /* provides 1V */

    printf ("Using design %s in device %s\n", fpga_design_names[design->design_id], design->vfio_device->device_name);
    select_i2c_controller (true, design->bit_banged_i2c_gpio_regs, &controller);

    dump_tef1001_fan_info (&controller);
    dump_ddr_temperature_information (&controller, pacc);
    dump_ddr3_spd_information (&controller);
    dump_ltm4676a_information (&controller, u3_ltm4676a_i2c_slave_address);
    dump_ltm4676a_information (&controller, u4_ltm4676a_i2c_slave_address);
    dump_si5338_information (&controller);

    /* Display XADC values if included in the FPGA */
    if (design->xadc_regs != NULL)
    {
        xadc_sample_collection_t xadc_collection;

        read_xadc_samples (&xadc_collection, design->xadc_regs);
        printf ("\n");
        display_xadc_samples (&xadc_collection);
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Perform tests on the FPGA designs which have the required I2C peripherals */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if ((design->iic_regs != NULL) && (design->bit_banged_i2c_gpio_regs != NULL))
        {
            dump_tef1001_information (design, designs.vfio_devices.pacc);
        }
    }

    return EXIT_SUCCESS;
}
