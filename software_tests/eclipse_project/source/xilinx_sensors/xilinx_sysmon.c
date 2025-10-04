/*
 * @file xilinx_sysmon.c
 * @date 31 Jul 2024
 * @author Chester Gillon
 * @brief Implements an interface to read Xilinx "UltraScale Architecture System Monitor (SYSMON)" values via VFIO
 * @details
 *  Has been written and tested only on the SYSMONE4 in an Kintex UltraScale+ device.. Some changes for run on other
 *  SYMON based devices would be:
 *  a. Disabling reading of the ADC Slow Channel Selection on device based upon the SYSMONE1.
 *  b. Add reading of calibration registers on SYSMONE1 based devices.
 *  c. Add reading of PC voltages on Zynq UltraScale+ MPSoC devices devices.
 */

#include "xilinx_sysmon.h"
#include "xilinx_sysmon_host_interface.h"
#include "vfio_access.h"

#include <string.h>
#include <stdio.h>


/* Contains the register offsets for one SYSMOM channel */
typedef struct
{
    /* The register offset to read the measurement value */
    uint32_t measurement_register_offset;
    /* The register offset to read the minimum value, or zero if the channel doesn't record min/max values */
    uint32_t min_register_offset;
    /* The register offset to read the maximum value, or zero if the channel doesn't record min/max values */
    uint32_t max_register_offset;
} sysmon_channel_register_offsets_t;


/* Look-up table to define the registers for each SYSMON channel readings */
static const sysmon_channel_register_offsets_t sysmon_channel_register_offsets[SYSMON_CHANNEL_ARRAY_SIZE] =
{
    [SYSMON_CHANNEL_TEMPERATURE] = {.measurement_register_offset = SYSMON_TEMPERATURE_OFFSET,
                                    .min_register_offset = SYSMON_MIN_TEMP_OFFSET,
                                    .max_register_offset = SYSMON_MAX_TEMP_OFFSET},
    [SYSMON_CHANNEL_VCCINT] = {.measurement_register_offset = SYSMON_VCCINT_OFFSET,
                               .min_register_offset = SYSMON_MIN_VCCINT_OFFSET,
                               .max_register_offset = SYSMON_MAX_VCCINT_OFFSET},
    [SYSMON_CHANNEL_VCCAUX] = {.measurement_register_offset = SYSMON_VCCAUX_OFFSET,
                               .min_register_offset = SYSMON_MIN_VCCAUX_OFFSET,
                               .max_register_offset = SYSMON_MAX_VCCAUX_OFFSET},
    [SYSMON_CHANNEL_VP_VN] = {.measurement_register_offset = SYSMON_VP_VN_OFFSET},
    [SYSMON_CHANNEL_VREFP] = {.measurement_register_offset = SYSMON_VREFP_OFFSET},
    [SYSMON_CHANNEL_VREFN] = {.measurement_register_offset = SYSMON_VREFN_OFFSET},
    [SYSMON_CHANNEL_VBRAM] = {.measurement_register_offset = SYSMON_VBRAM_OFFSET,
                              .min_register_offset = SYSMON_MIN_VBRAM_OFFSET,
                              .max_register_offset = SYSMON_MAX_VBRAM_OFFSET},
    [SYSMON_CHANNEL_VAUX0] = {.measurement_register_offset = SYSMON_VAUX0_OFFSET},
    [SYSMON_CHANNEL_VAUX1] = {.measurement_register_offset = SYSMON_VAUX1_OFFSET},
    [SYSMON_CHANNEL_VAUX2] = {.measurement_register_offset = SYSMON_VAUX2_OFFSET},
    [SYSMON_CHANNEL_VAUX3] = {.measurement_register_offset = SYSMON_VAUX3_OFFSET},
    [SYSMON_CHANNEL_VAUX4] = {.measurement_register_offset = SYSMON_VAUX4_OFFSET},
    [SYSMON_CHANNEL_VAUX5] = {.measurement_register_offset = SYSMON_VAUX5_OFFSET},
    [SYSMON_CHANNEL_VAUX6] = {.measurement_register_offset = SYSMON_VAUX6_OFFSET},
    [SYSMON_CHANNEL_VAUX7] = {.measurement_register_offset = SYSMON_VAUX7_OFFSET},
    [SYSMON_CHANNEL_VAUX8] = {.measurement_register_offset = SYSMON_VAUX8_OFFSET},
    [SYSMON_CHANNEL_VAUX9] = {.measurement_register_offset = SYSMON_VAUX9_OFFSET},
    [SYSMON_CHANNEL_VAUX10] = {.measurement_register_offset = SYSMON_VAUX10_OFFSET},
    [SYSMON_CHANNEL_VAUX11] = {.measurement_register_offset = SYSMON_VAUX11_OFFSET},
    [SYSMON_CHANNEL_VAUX12] = {.measurement_register_offset = SYSMON_VAUX12_OFFSET},
    [SYSMON_CHANNEL_VAUX13] = {.measurement_register_offset = SYSMON_VAUX13_OFFSET},
    [SYSMON_CHANNEL_VAUX14] = {.measurement_register_offset = SYSMON_VAUX14_OFFSET},
    [SYSMON_CHANNEL_VAUX15] = {.measurement_register_offset = SYSMON_VAUX15_OFFSET},
    [SYSMON_CHANNEL_VUSER0] = {.measurement_register_offset = SYSMON_VUSER0_OFFSET,
                               .min_register_offset = SYSMON_MIN_VUSER0_OFFSET,
                               .max_register_offset = SYSMON_MAX_VUSER0_OFFSET},
    [SYSMON_CHANNEL_VUSER1] = {.measurement_register_offset = SYSMON_VUSER1_OFFSET,
                                                          .min_register_offset = SYSMON_MIN_VUSER1_OFFSET,
                                                          .max_register_offset = SYSMON_MAX_VUSER1_OFFSET},
    [SYSMON_CHANNEL_VUSER2] = {.measurement_register_offset = SYSMON_VUSER2_OFFSET,
                               .min_register_offset = SYSMON_MIN_VUSER2_OFFSET,
                               .max_register_offset = SYSMON_MAX_VUSER2_OFFSET},
    [SYSMON_CHANNEL_VUSER3] = {.measurement_register_offset = SYSMON_VUSER3_OFFSET,
                               .min_register_offset = SYSMON_MIN_VUSER3_OFFSET,
                               .max_register_offset = SYSMON_MAX_VUSER3_OFFSET}
};


/* Defines the names of the SYSMON channels */
const char *const sysmon_channel_names[SYSMON_CHANNEL_ARRAY_SIZE] =
{
    [SYSMON_CHANNEL_TEMPERATURE] = "Temp  ",
    [SYSMON_CHANNEL_VCCINT     ] = "Vccint",
    [SYSMON_CHANNEL_VCCAUX     ] = "Vccaux",
    [SYSMON_CHANNEL_VP_VN      ] = "Vp_Vn ",
    [SYSMON_CHANNEL_VREFP      ] = "VrefP ",
    [SYSMON_CHANNEL_VREFN      ] = "VrefN ",
    [SYSMON_CHANNEL_VBRAM      ] = "Vbram ",
    [SYSMON_CHANNEL_CALIBRATION] = "Cal   ",
    [SYSMON_CHANNEL_VAUX0      ] = "Vaux0 ",
    [SYSMON_CHANNEL_VAUX1      ] = "Vaux1 ",
    [SYSMON_CHANNEL_VAUX2      ] = "Vaux2 ",
    [SYSMON_CHANNEL_VAUX3      ] = "Vaux3 ",
    [SYSMON_CHANNEL_VAUX4      ] = "Vaux4 ",
    [SYSMON_CHANNEL_VAUX5      ] = "Vaux5 ",
    [SYSMON_CHANNEL_VAUX6      ] = "Vaux6 ",
    [SYSMON_CHANNEL_VAUX7      ] = "Vaux7 ",
    [SYSMON_CHANNEL_VAUX8      ] = "Vaux8 ",
    [SYSMON_CHANNEL_VAUX9      ] = "Vaux9 ",
    [SYSMON_CHANNEL_VAUX10     ] = "Vaux10",
    [SYSMON_CHANNEL_VAUX11     ] = "Vaux11",
    [SYSMON_CHANNEL_VAUX12     ] = "Vaux12",
    [SYSMON_CHANNEL_VAUX13     ] = "Vaux13",
    [SYSMON_CHANNEL_VAUX14     ] = "Vaux14",
    [SYSMON_CHANNEL_VAUX15     ] = "Vaux15",
    [SYSMON_CHANNEL_VUSER0     ] = "Vuser0",
    [SYSMON_CHANNEL_VUSER1     ] = "Vuser1",
    [SYSMON_CHANNEL_VUSER2     ] = "Vuser2",
    [SYSMON_CHANNEL_VUSER3     ] = "Vuser3"
};


/* Defines the descriptions for the sysmon_sequencer_mode_t enumeration */
const char *const sysmon_sequencer_mode_names[] =
{
    [SYSMON_SEQUENCER_DEFAULT_MODE] = "Default mode",
    [SYSMON_SEQUENCER_SINGLE_PASS_SEQUENCE] = "Single pass sequence",
    [SYSMON_SEQUENCER_CONTINUOUS_SEQUENCE_MODE] = "Continuous sequence mode",
    [SYSMON_SEQUENCER_SINGLE_CHANNEL_MODE] = "Single channel mode (sequencer off)"
};


/**
 * @brief Read one raw SYSMON ADC value
 * @details Performs a 32-bit read from the SYSMON AXI, and returns the 10-bits of the ADC value
 * @param[in] sysmon_regs The base address of the SYSMON registers to read
 * @param[in] reg_offset The SYSMON register offset to read the ADC value from
 * @return The 12-bit ADC value
 */
static uint32_t read_sysmon_raw_adc_value (const uint8_t *const sysmon_regs, const uint32_t reg_offset)
{
    const uint32_t reg_value = read_reg32 (sysmon_regs, reg_offset);

    return (reg_value & 0xffc0) >> 6;
}


/**
 * @brief Scale one raw ADC value into it's units
 * @details
 *   The scaling is defined in https://docs.amd.com/v/u/en-US/ug580-ultrascale-sysmon
 *
 *   The reported values can be sanity checked against that shown by the SYSMON System Monitor values
 *   reported over JTAG by the Vivado Hardware Manager.
 * @param[in] collection Used to determine which SYSMOM input signals are bipolar
 * @param[in] channel Which SYSMON channel is being scaled, where the scaling is channel dependent
 * @param[in/out] sample The ADC sample to scale
 */
static void scale_sysmon_sample (const sysmon_sample_collection_t *const collection, const sysmon_channels_t channel,
                                 sysmon_adc_sample_t *const sample)
{
    const bool internal_reference_selected = (collection->flag_register & (1 << 9)) != 0;

    switch (channel)
    {
    case SYSMON_CHANNEL_TEMPERATURE:
        if (internal_reference_selected)
        {
            /* Scale a temperature into degrees centigrade when the internal reference is used */
            sample->scaled_value = ((double) sample->raw_value * 509.3140064 / 1024.0) - 280.23087870;
        }
        else
        {
            /* Scale a temperature into degrees centigrade when the external reference is used */
            sample->scaled_value = ((double) sample->raw_value * 507.5921310 / 1024.0) - 279.42657680;
        }
        break;

    case SYSMON_CHANNEL_VCCINT:
    case SYSMON_CHANNEL_VCCAUX:
    case SYSMON_CHANNEL_VP_VN:
    case SYSMON_CHANNEL_VREFP:
    case SYSMON_CHANNEL_VREFN:
    case SYSMON_CHANNEL_VBRAM:
        /* Internal power supply sensors which have a range of 3V */
        sample->scaled_value = (double) sample->raw_value * 3.0 / 1024.0;
        break;

    case SYSMON_CHANNEL_VUSER0:
    case SYSMON_CHANNEL_VUSER1:
    case SYSMON_CHANNEL_VUSER2:
    case SYSMON_CHANNEL_VUSER3:
        {
            /* User supply sensors have a different range depending upon if a high range supply.
             * Use the PMBUS bit set by the System Management Wizard which indicates if a high range supply. */
            const uint32_t hrio_bit = channel - SYSMON_CHANNEL_VUSER0;
            const bool is_hrio_supply = (collection->configuration_register_4 & (1 << hrio_bit)) != 0;

            if (is_hrio_supply)
            {
                /* High range IO user supply sensor with a range of 6V */
                sample->scaled_value = (double) sample->raw_value * 6.0 / 1024.0;
            }
            else
            {
                /* User supply sensor with a range of 3V */
                sample->scaled_value = (double) sample->raw_value * 3.0 / 1024.0;
            }
        }
        break;

    default:
        /* External ADC input */
        if (collection->bipolar_channels[channel])
        {
            /* Scale a bipolar voltage which has a full range of +/- 0.5V */

            /* First convert the unsigned 10-bit raw value to a 32-bit 2's complement */
            uint32_t sign_extended_raw_value = sample->raw_value;
            const uint32_t sign_bit_mask = 1 << 13;
            const uint32_t sign_extension_mask = 0xfffffc00;

            if ((sign_extended_raw_value & sign_bit_mask) != 0)
            {
                sign_extended_raw_value |= sign_extension_mask;
            }

            const int32_t signed_raw_value = (int32_t) sign_extended_raw_value;

            sample->scaled_value = (double) signed_raw_value * 1.0 / 1024.0;
        }
        else
        {
            /* Scale a unipolar voltage which has a full range of 1V */
            sample->scaled_value = (double) sample->raw_value * 1.0 / 1024.0;
        }
        break;
    }
}


/**
 * @brief Read one SYSMON channel, including the min/max values where defined for the channel.
 * @param[in/out] collection Where to store the values for the channel
 * @param[in] sysmon_regs The base address of the SYSMON registers to read
 * @param[in] channel Which SYSMON channel to read
 */
static void read_sysmon_channel (sysmon_sample_collection_t *const collection, const uint8_t *const sysmon_regs,
                                 const sysmon_channels_t channel)
{
    const sysmon_channel_register_offsets_t *register_offsets = &sysmon_channel_register_offsets[channel];
    sysmon_channel_sample_t *const sample = &collection->samples[channel];

    /* Check check of if the measurement register offset is defined is due to the calibration channel being available
     * in the sequencer, but not having any actual measurement register */
    if (register_offsets->measurement_register_offset != 0)
    {
        sample->measurement.raw_value = read_sysmon_raw_adc_value (sysmon_regs, register_offsets->measurement_register_offset);
        sample->measurement.defined = true;
        scale_sysmon_sample (collection, channel, &sample->measurement);
    }

    if (register_offsets->min_register_offset != 0)
    {
        const uint32_t initial_min_value = 0x3ff;

        sample->min.raw_value = read_sysmon_raw_adc_value (sysmon_regs, register_offsets->min_register_offset);
        sample->min.defined = sample->min.raw_value != initial_min_value;
        if (sample->min.defined)
        {
            scale_sysmon_sample (collection, channel, &sample->min);
        }
    }

    if (register_offsets->max_register_offset != 0)
    {
        const uint32_t initial_max_value = 0;

        sample->max.raw_value = read_sysmon_raw_adc_value (sysmon_regs, register_offsets->max_register_offset);
        sample->max.defined = sample->max.raw_value != initial_max_value;
        if (sample->max.defined)
        {
            scale_sysmon_sample (collection, channel, &sample->max);
        }
    }
}


/**
 * @brief Unpack a bitmask related to SYSMON channels read from a pair of registers.
 * @param[out] channel_flags The unpacked flags
 * @param[in] sysmon_regs The base address of the SYSMON registers to read
 * @param[in] lower_reg_offset Offset of SYSMON register containing the lower channels
 * @param[in] upper_reg_offset Offset of SYSMON register containing the upper channels
 * @param[in] user_reg_offset Optional offset of SYSMON register containing the user supply channels.
 *                            The value of zero indicates not used.
 */
static void unpack_sysmon_channel_bitmask (bool channel_flags[const SYSMON_CHANNEL_ARRAY_SIZE], const uint8_t *const sysmon_regs,
                                           const uint32_t lower_reg_offset, const uint32_t upper_reg_offset,
                                           const uint32_t user_reg_offset)
{
    /* Read the lower and upper 16-bit words containing the channel bitmasks */
    const uint64_t lower_word = read_reg32 (sysmon_regs, lower_reg_offset);
    const uint64_t upper_word = read_reg32 (sysmon_regs, upper_reg_offset);
    const uint64_t user_word = (user_reg_offset != 0) ? read_reg32 (sysmon_regs, user_reg_offset) : 0;

    /* Re-arrange the 16-bits words into a 36-bit mask ordered by ADC channel number.
     * I.e. bit 0 is ADC channel 0 .. bit 35 is ADC channel 35. */
    const uint64_t channels_bitmask =
            ((lower_word & 0x00ff) << 8) | /* Bits 8-15 of lower word are ADC channels 0-7 */
            ((lower_word & 0xff00) >> 8) | /* Bits 0-7  of lower word are ADC channels 8-15 */
            ((upper_word & 0xffff) << 16)| /* Bits 0-15 of upper word are ADC channels 16-31 */
            ((user_word  & 0x000f) << 32); /* Bits 0-3  of user  word are ADC channels 32-35 */

    for (uint32_t channel = 0; channel < SYSMON_CHANNEL_ARRAY_SIZE; channel++)
    {
        channel_flags[channel] = (channels_bitmask & (1ull << channel)) != 0;
    }
}

/**
 * @brief Read a collection of samples from a SYSMON
 * @details Reads the SYSMON configuration to determine:
 *          a. Which channels are enabled
 *          b. If external channels are in unipolar or bipolar mode
 * @param[out] device_collection The samples which have been read
 * @param[in/out] device_sysmon_regs The base address of the device SYSMON registers to read
 * @param[in] num_sysmon_slaves The number of SYSMON slaves, which can be non-zero for SSI devices
 */
void read_sysmon_samples (sysmon_device_collection_t *const device_collection,
                          uint8_t *const device_sysmon_regs, const uint32_t num_sysmon_slaves)
{
    device_collection->num_instances = 1 /* master */ + num_sysmon_slaves;

    /* Read all SYSMON instances in the device */
    for (uint32_t instance = 0; instance < device_collection->num_instances; instance++)
    {
        sysmon_sample_collection_t *const collection = &device_collection->collections[instance];
        uint8_t *const sysmon_regs = &device_sysmon_regs[instance * SYSMON_PER_SLAVE_OFFSET];

        /* Initialise to no samples defined */
        memset (collection->samples, 0, sizeof (collection->samples));

        /* Default to no channels being enabled in the sequencer */
        memset (collection->enabled_channels, false, sizeof (collection->enabled_channels));

        /* Default to no channels being averaged */
        memset (collection->averaged_channels, false, sizeof (collection->averaged_channels));

        /* Default to channels being unipolar */
        memset (collection->bipolar_channels, false, sizeof (collection->bipolar_channels));

        /* Default to standard acquisition time */
        memset (collection->channel_increased_acquisition_times, false, sizeof (collection->channel_increased_acquisition_times));

        /* Read the raw configuration registers */
        collection->configuration_register_0 = read_reg32 (sysmon_regs, SYSMON_CONFIGURATION_REGISTER_0_OFFSET);
        collection->configuration_register_1 = read_reg32 (sysmon_regs, SYSMON_CONFIGURATION_REGISTER_1_OFFSET);
        collection->configuration_register_2 = read_reg32 (sysmon_regs, SYSMON_CONFIGURATION_REGISTER_2_OFFSET);
        collection->configuration_register_3 = read_reg32 (sysmon_regs, SYSMON_CONFIGURATION_REGISTER_3_OFFSET);
        collection->configuration_register_4 = read_reg32 (sysmon_regs, SYSMON_CONFIGURATION_REGISTER_4_OFFSET);
        collection->anlog_bus_configuration = read_reg32 (sysmon_regs, SYSMON_ANALOG_BUS_CONFIGURATION_OFFSET);
        collection->flag_register = read_reg32 (sysmon_regs, SYSMON_FLAG_REGISTER_OFFSET);

        /* Extract the number of samples averaged, using the "Averaging Filter Settings" table from UG580 */
        const uint32_t average_filter_bits = (collection->configuration_register_0 & 0x3000) >> 12;
        switch (average_filter_bits)
        {
        case 0:
            collection->num_averaged_samples = 0;
            break;

        case 1:
            collection->num_averaged_samples = 16;
            break;

        case 2:
            collection->num_averaged_samples = 64;
            break;

        case 3:
            collection->num_averaged_samples = 256;
            break;
        }

        /* Extract the sequencer mode, using the "Sequencer Operation Settings" table from UG580 */
        const uint32_t seq_bits = (collection->configuration_register_1 & 0xf000) >> 12;
        switch (seq_bits)
        {
        case 1:
            collection->sequencer_mode = SYSMON_SEQUENCER_SINGLE_PASS_SEQUENCE;
            break;

        case 2:
            collection->sequencer_mode = SYSMON_SEQUENCER_CONTINUOUS_SEQUENCE_MODE;
            break;

        case 3:
            collection->sequencer_mode = SYSMON_SEQUENCER_SINGLE_CHANNEL_MODE;
            break;

        default:
            collection->sequencer_mode = SYSMON_SEQUENCER_DEFAULT_MODE;
            break;
        }

        if (collection->sequencer_mode == SYSMON_SEQUENCER_SINGLE_CHANNEL_MODE)
        {
            /* Determine the single channel which is in use */
            const sysmon_channels_t single_channel = collection->configuration_register_0 & 0x3f;

            collection->enabled_channels[single_channel] = true;
            collection->averaged_channels[single_channel] = collection->num_averaged_samples != 0;
            collection->bipolar_channels[single_channel] = (collection->configuration_register_0 & (1 << 10)) != 0;
            collection->channel_increased_acquisition_times[single_channel] = (collection->configuration_register_0 & (1 << 8)) != 0;
        }
        else
        {
            /* Determine which channels are enabled in the sequencer, are averaged, are in bipolar mode and
             * have increased acquisition time */
            unpack_sysmon_channel_bitmask (collection->enabled_channels, sysmon_regs,
                    SYSMON_CHANNEL_SELECTION_LOWER_OFFSET, SYSMON_CHANNEL_SELECTION_UPPER_OFFSET, SYSMON_CHANNEL_SELECTION_USER_OFFSET);
            unpack_sysmon_channel_bitmask (collection->enabled_slow_channels, sysmon_regs,
                    SYSMON_SLOW_CHANNEL_SELECTION_LOWER_OFFSET, SYSMON_SLOW_CHANNEL_SELECTION_UPPER_OFFSET,
                    SYSMON_SLOW_CHANNEL_SELECTION_USER_OFFSET);
            unpack_sysmon_channel_bitmask (collection->averaged_channels, sysmon_regs,
                    SYSMON_CHANNEL_AVERAGING_LOWER_OFFSET, SYSMON_CHANNEL_AVERAGING_UPPER_OFFSET, SYSMON_CHANNEL_AVERAGING_USER_OFFSET);
            unpack_sysmon_channel_bitmask (collection->bipolar_channels, sysmon_regs,
                    SYSMON_CHANNEL_ANALOG_INPUT_MODE_LOWER_OFFSET, SYSMON_CHANNEL_ANALOG_INPUT_MODE_UPPER_OFFSET, 0);
            unpack_sysmon_channel_bitmask (collection->channel_increased_acquisition_times, sysmon_regs,
                    SYSMON_CHANNEL_ACQUISITION_TIME_LOWER_OFFSET, SYSMON_CHANNEL_ACQUISITION_TIME_UPPER_OFFSET, 0);
        }

        /* Obtain values for the enabled SYSMON channels */
        for (sysmon_channels_t channel = 0; channel < SYSMON_CHANNEL_ARRAY_SIZE; channel++)
        {
            /* Assume the on-chip sensors always have defined values.
             * This is because they are included in the Default Mode Sequence which is used during
             * initial power-up and FPGA configuration.
             *
             * Selected as a special case so that enabled_channels[] reports what is the current enabled channels
             * in the sequencer, based upon how the FPGA bitstream may have changed from the power-up default. */
            const bool assumed_defined_on_chip_sensor =
                    (channel == SYSMON_CHANNEL_CALIBRATION) ||
                    (channel == SYSMON_CHANNEL_TEMPERATURE) ||
                    (channel == SYSMON_CHANNEL_VCCINT) ||
                    (channel == SYSMON_CHANNEL_VCCAUX) ||
                    (channel == SYSMON_CHANNEL_VBRAM);

            if (collection->enabled_channels[channel] || collection->enabled_slow_channels[channel] || assumed_defined_on_chip_sensor)
            {
                read_sysmon_channel (collection, sysmon_regs, channel);
            }
        }
    }
}


/**
 * @brief Brief the collection of SYSMON samples which have been read.
 * @param[in] device_collection The samples to display
 */
void display_sysmon_samples (const sysmon_device_collection_t *const device_collection)
{
    /* Display all SYSMON instances in the device */
    for (uint32_t instance = 0; instance < device_collection->num_instances; instance++)
    {
        const sysmon_sample_collection_t *const collection = &device_collection->collections[instance];

        /* Display sequence mode and the enabled channels in the sequencer */
        printf ("SYSMON");
        if (device_collection->num_instances > 1)
        {
            printf (" instance %u", instance);
        }
        printf (" samples using %s\n", sysmon_sequencer_mode_names[collection->sequencer_mode]);
        printf ("Number of samples averaged ");
        if (collection->num_averaged_samples > 0)
        {
            printf ("%u\n", collection->num_averaged_samples);
        }
        else
        {
            printf ("none\n");
        }
        printf ("Current enabled channels in sequencer:");
        for (sysmon_channels_t channel = 0; channel < SYSMON_CHANNEL_ARRAY_SIZE; channel++)
        {
            if (collection->enabled_channels[channel] || collection->enabled_slow_channels[channel])
            {
                printf (" %s ", sysmon_channel_names[channel]);
                if (collection->bipolar_channels[channel])
                {
                    printf (" (bipolar)");
                }
                if (collection->channel_increased_acquisition_times[channel])
                {
                    printf (" (acq time)");
                }
                if (collection->enabled_slow_channels[channel])
                {
                    printf (" (slow)");
                }
                if (collection->averaged_channels[channel])
                {
                    printf (" (averaged)");
                }
            }
        }
        printf ("\n");

        /* Display the raw Analog Bus configuration pending understanding how to decode it */
        printf ("Analog Bus configuration 0x%04X\n", collection->anlog_bus_configuration);

        /* Display all channels which have a defined sample. May include on-chip sensors which have an initial sample,
         * but not not in the current sequencer. */
        printf ("  Channel  Measurement     Min           Max\n");
        for (sysmon_channels_t channel = 0; channel < SYSMON_CHANNEL_ARRAY_SIZE; channel++)
        {
            const char *const display_units = (channel == SYSMON_CHANNEL_TEMPERATURE) ? "C" : "V";
            const sysmon_channel_sample_t *const sample = &collection->samples[channel];

            if (sample->measurement.defined)
            {
                printf ("  %s     %7.4f%s", sysmon_channel_names[channel], sample->measurement.scaled_value, display_units);

                if (sample->min.defined)
                {
                    printf ("     %7.4f%s", sample->min.scaled_value, display_units);
                }
                else
                {
                    printf ("           ");
                }

                if (sample->max.defined)
                {
                    printf ("      %7.4f%s", sample->max.scaled_value, display_units);
                }
                printf ("\n");
            }
        }
    }
}
