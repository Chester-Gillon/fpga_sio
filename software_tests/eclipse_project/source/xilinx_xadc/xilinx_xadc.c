/*
 * @file xilinx_xadc.c
 * @date 25 Jun 2023
 * @author Chester Gillon
 * @brief Implements an interface to read Xilinx "Analog-to-Digital Converter (XADC)" values via VFIO
 */

#include "xilinx_xadc.h"
#include "xilinx_xadc_host_interface.h"
#include "vfio_access.h"

#include <string.h>
#include <stdio.h>


/* Contains the register offsets for one XADC channel */
typedef struct
{
    /* The register offset to read the measurement value */
    uint32_t measurement_register_offset;
    /* The register offset to read the minimum value, or zero if the channel doesn't record min/max values */
    uint32_t min_register_offset;
    /* The register offset to read the maximum value, or zero if the channel doesn't record min/max values */
    uint32_t max_register_offset;
} xadc_channel_register_offsets_t;


/* Look-up table to define the registers for each XADC channel readings */
static const xadc_channel_register_offsets_t xadc_channel_register_offsets[XADC_CHANNEL_ARRAY_SIZE] =
{
    [XADC_CHANNEL_TEMPERATURE] = {.measurement_register_offset = XADC_TEMPERATURE_OFFSET,
                                  .min_register_offset = XADC_MIN_TEMP_OFFSET,
                                  .max_register_offset = XADC_MAX_TEMP_OFFSET},
    [XADC_CHANNEL_VCCINT] = {.measurement_register_offset = XADC_VCCINT_OFFSET,
                             .min_register_offset = XADC_MIN_VCCINT_OFFSET,
                             .max_register_offset = XADC_MAX_VCCINT_OFFSET},
    [XADC_CHANNEL_VCCAUX] = {.measurement_register_offset = XADC_VCCAUX_OFFSET,
                             .min_register_offset = XADC_MIN_VCCAUX_OFFSET,
                             .max_register_offset = XADC_MAX_VCCAUX_OFFSET},
    [XADC_CHANNEL_VP_VN] = {.measurement_register_offset = XADC_VP_VN_OFFSET},
    [XADC_CHANNEL_VREFP] = {.measurement_register_offset = XADC_VREFP_OFFSET},
    [XADC_CHANNEL_VREFN] = {.measurement_register_offset = XADC_VREFN_OFFSET},
    [XADC_CHANNEL_VBRAM] = {.measurement_register_offset = XADC_VBRAM_OFFSET,
                            .min_register_offset = XADC_MIN_VBRAM_OFFSET,
                            .max_register_offset = XADC_MAX_VBRAM_OFFSET},
    [XADC_CHANNEL_VAUX0] = {.measurement_register_offset = XADC_VAUX0_OFFSET},
    [XADC_CHANNEL_VAUX1] = {.measurement_register_offset = XADC_VAUX1_OFFSET},
    [XADC_CHANNEL_VAUX2] = {.measurement_register_offset = XADC_VAUX2_OFFSET},
    [XADC_CHANNEL_VAUX3] = {.measurement_register_offset = XADC_VAUX3_OFFSET},
    [XADC_CHANNEL_VAUX4] = {.measurement_register_offset = XADC_VAUX4_OFFSET},
    [XADC_CHANNEL_VAUX5] = {.measurement_register_offset = XADC_VAUX5_OFFSET},
    [XADC_CHANNEL_VAUX6] = {.measurement_register_offset = XADC_VAUX6_OFFSET},
    [XADC_CHANNEL_VAUX7] = {.measurement_register_offset = XADC_VAUX7_OFFSET},
    [XADC_CHANNEL_VAUX8] = {.measurement_register_offset = XADC_VAUX8_OFFSET},
    [XADC_CHANNEL_VAUX9] = {.measurement_register_offset = XADC_VAUX9_OFFSET},
    [XADC_CHANNEL_VAUX10] = {.measurement_register_offset = XADC_VAUX10_OFFSET},
    [XADC_CHANNEL_VAUX11] = {.measurement_register_offset = XADC_VAUX11_OFFSET},
    [XADC_CHANNEL_VAUX12] = {.measurement_register_offset = XADC_VAUX12_OFFSET},
    [XADC_CHANNEL_VAUX13] = {.measurement_register_offset = XADC_VAUX13_OFFSET},
    [XADC_CHANNEL_VAUX14] = {.measurement_register_offset = XADC_VAUX14_OFFSET},
    [XADC_CHANNEL_VAUX15] = {.measurement_register_offset = XADC_VAUX15_OFFSET}
};


/* Defines the names of the XADC channels */
const char *const xadc_channel_names[XADC_CHANNEL_ARRAY_SIZE] =
{
    [XADC_CHANNEL_TEMPERATURE] = "Temp  ",
    [XADC_CHANNEL_VCCINT     ] = "Vccint",
    [XADC_CHANNEL_VCCAUX     ] = "Vccaux",
    [XADC_CHANNEL_VP_VN      ] = "Vp_Vn ",
    [XADC_CHANNEL_VREFP      ] = "VrefP ",
    [XADC_CHANNEL_VREFN      ] = "VrefN ",
    [XADC_CHANNEL_VBRAM      ] = "Vbram ",
    [XADC_CHANNEL_CALIBRATION] = "Cal   ",
    [XADC_CHANNEL_VAUX0      ] = "Vaux0 ",
    [XADC_CHANNEL_VAUX1      ] = "Vaux1 ",
    [XADC_CHANNEL_VAUX2      ] = "Vaux2 ",
    [XADC_CHANNEL_VAUX3      ] = "Vaux3 ",
    [XADC_CHANNEL_VAUX4      ] = "Vaux4 ",
    [XADC_CHANNEL_VAUX5      ] = "Vaux5 ",
    [XADC_CHANNEL_VAUX6      ] = "Vaux6 ",
    [XADC_CHANNEL_VAUX7      ] = "Vaux7 ",
    [XADC_CHANNEL_VAUX8      ] = "Vaux8 ",
    [XADC_CHANNEL_VAUX9      ] = "Vaux9 ",
    [XADC_CHANNEL_VAUX10     ] = "Vaux10",
    [XADC_CHANNEL_VAUX11     ] = "Vaux11",
    [XADC_CHANNEL_VAUX12     ] = "Vaux12",
    [XADC_CHANNEL_VAUX13     ] = "Vaux13",
    [XADC_CHANNEL_VAUX14     ] = "Vaux14",
    [XADC_CHANNEL_VAUX15     ] = "Vaux15"
};


/* Defines the descriptions for the xadc_sequencer_mode_t enumeration */
const char *const xadc_sequencer_mode_names[] =
{
    [XADC_SEQUENCER_DEFAULT_MODE] = "Default mode",
    [XADC_SEQUENCER_SINGLE_PASS_SEQUENCE] = "Single pass sequence",
    [XADC_SEQUENCER_CONTINUOUS_SEQUENCE_MODE] = "Continuous sequence mode",
    [XADC_SEQUENCER_SINGLE_CHANNEL_MODE] = "Single channel mode (sequencer off)",
    [XADC_SEQUENCER_SIMULTANEOUS_SAMPLING_MODE] = "Simultaneous sampling mode",
    [XADC_SEQUENCER_INDEPENDENT_ADC_MODE] = "Independent ADC mode"
};


/**
 * @brief Read one raw XADC ADC value
 * @details Performs a 32-bit read from the XADC AXI, and returns the 12-bits of the ADC value
 * @param[in] xadc_regs The base address of the XADC registers to read
 * @param[in] reg_offset The XADC register offset to read the ADC value from
 * @return The 12-bit ADC value
 */
static uint32_t read_xadc_raw_adc_value (const uint8_t *const xadc_regs, const uint32_t reg_offset)
{
    const uint32_t reg_value = read_reg32 (xadc_regs, reg_offset);

    return (reg_value & 0xfff0) >> 4;
}


/**
 * @brief Scale one raw ADC value into it's units
 * @details
 *   The scaling is defined in
 *   https://www.xilinx.com/content/dam/xilinx/support/documents/user_guides/ug480_7Series_XADC.pdf
 *
 *   The reported values can be sanity checked against that shown by the XADC System Monitor values
 *   reported over JTAG by the Vivado Hardware Manager.
 * @param[in] collection Used to determine which XADC input signals are bipolar
 * @param[in] channel Which XADC channel is being scaled, where the scaling is channel dependent
 * @param[in/out] sample The ADC sample to scale
 */
static void scale_xadc_sample (const xadc_sample_collection_t *const collection, const xadc_channels_t channel,
                               xadc_adc_sample_t *const sample)
{
    switch (channel)
    {
    case XADC_CHANNEL_TEMPERATURE:
        /* Scale a temperature into degrees centigrade */
        sample->scaled_value = ((double) sample->raw_value * 503.975 / 4096.0) - 273.15;
        break;

    case XADC_CHANNEL_VCCINT:
    case XADC_CHANNEL_VCCAUX:
    case XADC_CHANNEL_VBRAM:
    case XADC_CHANNEL_VREFP:
    case XADC_CHANNEL_VREFN:
        /* Scale a voltage measured by the supply sensor which has a full range of 3V */
        sample->scaled_value = (double) sample->raw_value * 3.0 / 4096.0;
        break;

    default:
        if (collection->bipolar_channels[channel])
        {
            /* Scale a bipolar voltage which has a full range of +/- 0.5V */

            /* First convert the unsigned 12-bit raw value to a 32-bit 2's complement */
            uint32_t sign_extended_raw_value = sample->raw_value;
            const uint32_t sign_bit_mask = 1 << 11;
            const uint32_t sign_extension_mask = 0xfffff000;

            if ((sign_extended_raw_value & sign_bit_mask) != 0)
            {
                sign_extended_raw_value |= sign_extension_mask;
            }

            const int32_t signed_raw_value = (int32_t) sign_extended_raw_value;

            sample->scaled_value = (double) signed_raw_value * 0.5;
        }
        else
        {
            /* Scale a unipolar voltage which has a full range of 1V */
            sample->scaled_value = (double) sample->raw_value * 1.0 / 4096.0;
        }
        break;
    }
}


/**
 * @brief Read one XADC channel, including the min/max values where defined for the channel.
 * @param[in/out] collection Where to store the values for the channel
 * @param[in] xadc_regs The base address of the XADC registers to read
 * @param[in] channel Which XADC channel to read
 */
static void read_xadc_channel (xadc_sample_collection_t *const collection, const uint8_t *const xadc_regs,
                               const xadc_channels_t channel)
{
    const xadc_channel_register_offsets_t *register_offsets = &xadc_channel_register_offsets[channel];
    xadc_channel_sample_t *const sample = &collection->samples[channel];

    sample->measurement.raw_value = read_xadc_raw_adc_value (xadc_regs, register_offsets->measurement_register_offset);
    sample->measurement.defined = true;
    scale_xadc_sample (collection, channel, &sample->measurement);

    if (register_offsets->min_register_offset != 0)
    {
        const uint32_t initial_min_value = 0xfff;

        sample->min.raw_value = read_xadc_raw_adc_value (xadc_regs, register_offsets->min_register_offset);
        sample->min.defined = sample->min.raw_value != initial_min_value;
        if (sample->min.defined)
        {
            scale_xadc_sample (collection, channel, &sample->min);
        }
    }

    if (register_offsets->max_register_offset != 0)
    {
        const uint32_t initial_max_value = 0;

        sample->max.raw_value = read_xadc_raw_adc_value (xadc_regs, register_offsets->max_register_offset);
        sample->max.defined = sample->max.raw_value != initial_max_value;
        if (sample->max.defined)
        {
            scale_xadc_sample (collection, channel, &sample->max);
        }
    }
}


/**
 * @brief Unpack a bitmask related to XADC channels read from a pair of registers.
 * @param[out] channel_flags The unpacked flags
 * @param[in] xadc_regs The base address of the XADC registers to read
 * @param[in] lower_reg_offset Offset of XADC register containing the lower channels
 * @param[in] upper_reg_offset Offset of XADC register containing the upper channels
 */
static void unpack_xadc_channel_bitmask (bool channel_flags[const XADC_CHANNEL_ARRAY_SIZE], const uint8_t *const xadc_regs,
                                         const uint32_t lower_reg_offset, const uint32_t upper_reg_offset)
{
    /* Read the lower and upper 16-bit words containing the channel bitmasks */
    const uint32_t lower_word = read_reg32 (xadc_regs, lower_reg_offset);
    const uint32_t upper_word = read_reg32 (xadc_regs, upper_reg_offset);

    /* Re-arrange the lower and upper 16-bits words into a 32-bit mask ordered by ADC channel number.
     * I.e. bit 0 is ADC channel 0 .. bit 31 is ADC channel 31. */
    const uint32_t channels_bitmask =
            ((lower_word & 0x00ff) << 8) | /* Bits 8-15 of lower word are ADC channels 0-7 */
            ((lower_word & 0xff00) >> 8) | /* Bits 0-7  of lower word are ADC channels 8-15 */
            ((upper_word & 0xffff) << 16); /* Bits 0-15 of upper word are ADC channels 16-31 */

    for (uint32_t channel = 0; channel < XADC_CHANNEL_ARRAY_SIZE; channel++)
    {
        channel_flags[channel] = (channels_bitmask & (1u << channel)) != 0;
    }
}


/**
 * @brief Read a collection of samples from a XADC
 * @details Reads the XADC configuration to determine:
 *          a. Which channels are enabled
 *          b. If external channels are in unipolar or bipolar mode
 * @param[out] collection The samples which have been read
 * @param[in/out] xadc_regs The base address of the XADC registers to read
 */
void read_xadc_samples (xadc_sample_collection_t *const collection, uint8_t *const xadc_regs)
{
    /* Initialise to no samples defined */
    memset (collection->samples, 0, sizeof (collection->samples));

    /* Default to no channels being enabled in the sequencer */
    memset (collection->enabled_channels, false, sizeof (collection->enabled_channels));

    /* Default to channels being unipolar */
    memset (collection->bipolar_channels, false, sizeof (collection->bipolar_channels));

    /* Read the raw ADC calibration register values.
     * These are not used by the function, but are saved as diagnostics. */
    collection->raw_adc_a_supply_offset = read_reg32 (xadc_regs, XADC_SUPPLY_A_OFFSET_OFFSET);
    collection->raw_adc_a_bipolar_offset = read_reg32 (xadc_regs, XADC_ADC_A_OFFSET_OFFSET);
    collection->raw_adc_a_gain = read_reg32 (xadc_regs, XADC_ADC_A_GAIN_ERROR_OFFSET);
    collection->raw_adc_b_supply_offset = read_reg32 (xadc_regs, XADC_SUPPLY_B_OFFSET_OFFSET);
    collection->raw_adc_b_bipolar_offset = read_reg32 (xadc_regs, XADC_ADC_B_OFFSET_OFFSET);
    collection->raw_adc_b_gain = read_reg32 (xadc_regs, XADC_ADC_B_GAIN_ERROR_OFFSET);

    /* Read the raw configuration registers */
    collection->configuration_register_0 = read_reg32 (xadc_regs, XADC_CONFIGURATION_REGISTER_0_OFFSET);
    collection->configuration_register_1 = read_reg32 (xadc_regs, XADC_CONFIGURATION_REGISTER_1_OFFSET);
    collection->configuration_register_2 = read_reg32 (xadc_regs, XADC_CONFIGURATION_REGISTER_2_OFFSET);

    /* Extract the sequencer mode, using the "Sequencer Operation Settings" table from UG480 */
    const uint32_t seq_bits = (collection->configuration_register_1 & 0xf000) >> 12;
    if (seq_bits == 1)
    {
        collection->sequencer_mode = XADC_SEQUENCER_SINGLE_PASS_SEQUENCE;
    }
    else if (seq_bits == 2)
    {
        collection->sequencer_mode = XADC_SEQUENCER_CONTINUOUS_SEQUENCE_MODE;
    }
    else if (seq_bits == 3)
    {
        collection->sequencer_mode = XADC_SEQUENCER_SINGLE_CHANNEL_MODE;
    }
    else if ((seq_bits & 0xc) == 0x4)
    {
        collection->sequencer_mode = XADC_SEQUENCER_SIMULTANEOUS_SAMPLING_MODE;
    }
    else if ((seq_bits & 0xc) == 0x8)
    {
        collection->sequencer_mode = XADC_SEQUENCER_INDEPENDENT_ADC_MODE;
    }
    else
    {
        collection->sequencer_mode = XADC_SEQUENCER_DEFAULT_MODE;
    }

    if (collection->sequencer_mode == XADC_SEQUENCER_SINGLE_CHANNEL_MODE)
    {
        /* Determine the single channel which is in use */
        const xadc_channels_t single_channel = collection->configuration_register_0 & 0x1f;

        collection->enabled_channels[single_channel] = true;
        collection->bipolar_channels[single_channel] = (collection->configuration_register_0 & (1 << 10)) != 0;
    }
    else
    {
        /* Determine which channels are enabled in the sequencer, and which are in bipolar mode */
        unpack_xadc_channel_bitmask (collection->enabled_channels, xadc_regs,
                XADC_CHANNEL_SELECTION_LOWER_OFFSET, XADC_CHANNEL_SELECTION_UPPER_OFFSET);
        unpack_xadc_channel_bitmask (collection->bipolar_channels, xadc_regs,
                XADC_CHANNEL_ANALOG_INPUT_MODE_LOWER_OFFSET, XADC_CHANNEL_ANALOG_INPUT_MODE_UPPER_OFFSET);
    }

    /* Obtain values for the enabled ADC channels */
    for (xadc_channels_t channel = 0; channel < XADC_CHANNEL_ARRAY_SIZE; channel++)
    {
        /* Assume the on-chip sensors always have defined values.
         * This is because they are included in the Default Mode Sequence which is used during
         * initial power-up and FPGA configuration.
         *
         * Selected as a special case so that enabled_channels[] reports what is the current enabled channels
         * in the sequencer, based upon how the FPGA bitstream may have changed from the power-up default. */
        const bool assumed_defined_on_chip_sensor =
                (channel == XADC_CHANNEL_TEMPERATURE) ||
                (channel == XADC_CHANNEL_VCCINT) ||
                (channel == XADC_CHANNEL_VCCAUX) ||
                (channel == XADC_CHANNEL_VBRAM);

        if (collection->enabled_channels[channel] || assumed_defined_on_chip_sensor)
        {
            read_xadc_channel (collection, xadc_regs, channel);
        }
    }
}


/**
 * @brief Unpack a XADC offset calibration into a signed integer
 * @param[in] calibration_reg_value The packed calibration register value containing the offset.
 * @return The signed offset calibration in 12-bit ADC lsbs
 */
static int32_t unpack_xadc_offset_calibation (const uint32_t calibration_reg_value)
{
    const uint32_t raw_value = (calibration_reg_value & 0xfff0) >> 4;
    uint32_t sign_extended_raw_value = raw_value;
    const uint32_t sign_bit_mask = 1 << 11;
    const uint32_t sign_extension_mask = 0xfffff000;

    if ((sign_extended_raw_value & sign_bit_mask) != 0)
    {
        sign_extended_raw_value |= sign_extension_mask;
    }

    const int32_t signed_raw_value = (int32_t) sign_extended_raw_value;

    return signed_raw_value;
}


/**
 * @brief Unpack a XDC gain calibration as a signed correction factor in percent
 * @param[in] calibration_reg_value The packed calibration register value containing the gain correction
 * @return The signed correction factor in percent
 */
static double unpack_xadc_gain_calibration (const uint32_t calibration_reg_value)
{
    /* Gain calibration is stored as:
     * - 6 bits of magnitude with an lsb of 0.1%
     * - one sign bit. */
    const uint32_t gain_magnitude = calibration_reg_value & 0x3f;
    const bool sign_bit = calibration_reg_value & 0x40;
    double correction_factor;

    if (sign_bit != 0)
    {
        /* When sign bit set a positive correction factor */
        correction_factor = 0.1 * (double) gain_magnitude;
    }
    else
    {
        /* When sign bit clear a negative correction factor */
        correction_factor = -0.1 * (double) gain_magnitude;
    }

    return correction_factor;
}


/**
 * @brief Brief the collection of XADC samples which have been read.
 * @param[in] collection The collection to display
 */
void display_xadc_samples (const xadc_sample_collection_t *const collection)
{
    /* Display sequence mode and the enabled channels in the sequencer */
    printf ("XADC samples using %s\n", xadc_sequencer_mode_names[collection->sequencer_mode]);
    printf ("Current enabled channels in sequencer:");
    for (xadc_channels_t channel = 0; channel < XADC_CHANNEL_ARRAY_SIZE; channel++)
    {
        if (collection->enabled_channels[channel])
        {
            printf (" %s ", xadc_channel_names[channel]);
            if (collection->bipolar_channels[channel])
            {
                printf (" (bipolar)");
            }
        }
    }
    printf ("\n");

    /* Display ADC calibration */
    printf ("ADC A calibration: unipolar offset=%d (lsbs)  bipolar offset=%d (lsbs)  gain correction factor=%.1f (%%)\n",
            unpack_xadc_offset_calibation (collection->raw_adc_a_supply_offset),
            unpack_xadc_offset_calibation (collection->raw_adc_a_bipolar_offset),
            unpack_xadc_gain_calibration (collection->raw_adc_a_gain));
    printf ("ADC B calibration: unipolar offset=%d (lsbs)  bipolar offset=%d (lsbs)  gain correction factor=%.1f (%%)\n",
            unpack_xadc_offset_calibation (collection->raw_adc_b_supply_offset),
            unpack_xadc_offset_calibation (collection->raw_adc_b_bipolar_offset),
            unpack_xadc_gain_calibration (collection->raw_adc_b_gain));

    /* Display all channels which have a defined sample. May include on-chip sensors which have an initial sample,
     * but not not in the current sequencer. */
    printf ("  Channel  Measurement    Min          Max\n");
    for (xadc_channels_t channel = 0; channel < XADC_CHANNEL_ARRAY_SIZE; channel++)
    {
        const char *const display_units = (channel == XADC_CHANNEL_TEMPERATURE) ? "C" : "V";
        const xadc_channel_sample_t *const sample = &collection->samples[channel];

        if (sample->measurement.defined)
        {
            printf ("  %s     %6.3f%s", xadc_channel_names[channel], sample->measurement.scaled_value, display_units);

            if (sample->min.defined)
            {
                printf ("     %6.3f%s", sample->min.scaled_value, display_units);
            }
            else
            {
                printf ("           ");
            }

            if (sample->max.defined)
            {
                printf ("      %6.3f%s", sample->max.scaled_value, display_units);
            }
            printf ("\n");
        }
    }
}
