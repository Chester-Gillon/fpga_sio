/*
 * @file xilinx_xadc.h
 * @date 25 Jun 2023
 * @author Chester Gillon
 * @brief Provides an interface to read Xilinx "Analog-to-Digital Converter (XADC)" values via VFIO
 */

#ifndef XILINX_XADC_H_
#define XILINX_XADC_H_

#include "stdint.h"
#include "stdbool.h"


/* The list of XADC channels which may be read.
 * The enumeration values match the "ADC Channel Select" table from UG480 so can be used to index channels
 * in the XADC configuration registers. */
typedef enum
{
    XADC_CHANNEL_TEMPERATURE = 0,
    XADC_CHANNEL_VCCINT = 1,
    XADC_CHANNEL_VCCAUX = 2,
    XADC_CHANNEL_VP_VN = 3,
    XADC_CHANNEL_VREFP = 4,
    XADC_CHANNEL_VREFN = 5,
    XADC_CHANNEL_VBRAM = 6,
    /* 7 is an invalid channel selection */
    XADC_CHANNEL_CALIBRATION = 8,
    /* 9 - 12 are invalid channel selections */
    /* 13 - 15 are channels only supported in Zync-7000 devices */
    XADC_CHANNEL_VAUX0 = 16,
    XADC_CHANNEL_VAUX1 = 17,
    XADC_CHANNEL_VAUX2 = 18,
    XADC_CHANNEL_VAUX3 = 19,
    XADC_CHANNEL_VAUX4 = 20,
    XADC_CHANNEL_VAUX5 = 21,
    XADC_CHANNEL_VAUX6 = 22,
    XADC_CHANNEL_VAUX7 = 23,
    XADC_CHANNEL_VAUX8 = 24,
    XADC_CHANNEL_VAUX9 = 25,
    XADC_CHANNEL_VAUX10 = 26,
    XADC_CHANNEL_VAUX11 = 27,
    XADC_CHANNEL_VAUX12 = 28,
    XADC_CHANNEL_VAUX13 = 29,
    XADC_CHANNEL_VAUX14 = 30,
    XADC_CHANNEL_VAUX15 = 31,

    XADC_CHANNEL_ARRAY_SIZE
} xadc_channels_t;

extern const char *const xadc_channel_names[XADC_CHANNEL_ARRAY_SIZE];


/* Contains one XADC ADC sample */
typedef struct
{
    /* Set to true is the sample value has a defined reading */
    bool defined;
    /* The raw 12-bit ADC value */
    uint32_t raw_value;
    /* The value scaled into units */
    double scaled_value;
} xadc_adc_sample_t;


/* Contains the samples from one XADC channel */
typedef struct
{
    /* The most recent measurement value */
    xadc_adc_sample_t measurement;
    /* The min/max values sampled. Only defined when both:
     * a. The channel tracks the min/max value.
     * b. The value has changed below/above the initial measurement value since the XADC was reset. */
    xadc_adc_sample_t min;
    xadc_adc_sample_t max;
} xadc_channel_sample_t;


/* The XADC Sequencer Operation mode */
typedef enum
{
    XADC_SEQUENCER_DEFAULT_MODE,
    XADC_SEQUENCER_SINGLE_PASS_SEQUENCE,
    XADC_SEQUENCER_CONTINUOUS_SEQUENCE_MODE,
    XADC_SEQUENCER_SINGLE_CHANNEL_MODE,
    XADC_SEQUENCER_SIMULTANEOUS_SAMPLING_MODE,
    XADC_SEQUENCER_INDEPENDENT_ADC_MODE
} xadc_sequencer_mode_t;

extern const char *const xadc_sequencer_mode_names[];


/* Contains a collection of the samples read from the XADC */
typedef struct
{
    /* The raw calibration register values for both ADCs.
     * The 7 Series FPGAs and Zynq-7000 SoC XADC Dual 12-Bit 1 MSPS Analog-to-Digital Converter User Guide (UG480)
     * uses the term "bipolar offset" whereas the pg091-xadc-wiz uses the term "ADC A offset".
     *
     * Suspect the "supply offset" applies to all unipolar readings made using the ADC.
     */
    uint32_t raw_adc_a_supply_offset;
    uint32_t raw_adc_a_bipolar_offset;
    uint32_t raw_adc_a_gain;
    uint32_t raw_adc_b_supply_offset;
    uint32_t raw_adc_b_bipolar_offset;
    uint32_t raw_adc_b_gain;
    /* The raw configuration register values */
    uint32_t configuration_register_0;
    uint32_t configuration_register_1;
    uint32_t configuration_register_2;
    /* Extracted from the configuration registers */
    xadc_sequencer_mode_t sequencer_mode;
    /* The array of samples from the XADC channels */
    xadc_channel_sample_t samples[XADC_CHANNEL_ARRAY_SIZE];
    /* Defines which channels are enabled in the sequencer */
    bool enabled_channels[XADC_CHANNEL_ARRAY_SIZE];
    /* Defines which XADC channels take bipolar measurements */
    bool bipolar_channels[XADC_CHANNEL_ARRAY_SIZE];
} xadc_sample_collection_t;


void read_xadc_samples (xadc_sample_collection_t *const collection, uint8_t *const xadc_regs);
void display_xadc_samples (const xadc_sample_collection_t *const collection);

#endif /* XILINX_XADC_H_ */
