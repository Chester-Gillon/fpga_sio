/*
 * @file xilinx_sysmon.h
 * @date 31 Jul 2024
 * @author Chester Gillon
 * @brief Provides an interface to read Xilinx "UltraScale Architecture System Monitor (SYSMON)" values via VFIO
 */

#ifndef XILINX_SYSMON_H_
#define XILINX_SYSMON_H_

#include <stdbool.h>
#include <stdint.h>


/* The list of SYSMON channels which may be read.
 * The enumeration values match the "ADC Channel Select" table in UG580 so can be used to index channels
 * in the SYSMON configuration registers. */
typedef enum
{
    SYSMON_CHANNEL_TEMPERATURE = 0,
    SYSMON_CHANNEL_VCCINT = 1,
    SYSMON_CHANNEL_VCCAUX = 2,
    SYSMON_CHANNEL_VP_VN = 3,
    SYSMON_CHANNEL_VREFP = 4,
    SYSMON_CHANNEL_VREFN = 5,
    SYSMON_CHANNEL_VBRAM = 6,
    /* 7 is an invalid channel selection */
    SYSMON_CHANNEL_CALIBRATION = 8,
    /* 9 to 12 are invalid channel selections */
    /* 13 to 15 are channels only supported in Zynq UltraScale+ MPSoC devices */
    SYSMON_CHANNEL_VAUX0 = 16,
    SYSMON_CHANNEL_VAUX1 = 17,
    SYSMON_CHANNEL_VAUX2 = 18,
    SYSMON_CHANNEL_VAUX3 = 19,
    SYSMON_CHANNEL_VAUX4 = 20,
    SYSMON_CHANNEL_VAUX5 = 21,
    SYSMON_CHANNEL_VAUX6 = 22,
    SYSMON_CHANNEL_VAUX7 = 23,
    SYSMON_CHANNEL_VAUX8 = 24,
    SYSMON_CHANNEL_VAUX9 = 25,
    SYSMON_CHANNEL_VAUX10 = 26,
    SYSMON_CHANNEL_VAUX11 = 27,
    SYSMON_CHANNEL_VAUX12 = 28,
    SYSMON_CHANNEL_VAUX13 = 29,
    SYSMON_CHANNEL_VAUX14 = 30,
    SYSMON_CHANNEL_VAUX15 = 31,
    SYSMON_CHANNEL_VUSER0 = 32,
    SYSMON_CHANNEL_VUSER1 = 33,
    SYSMON_CHANNEL_VUSER2 = 34,
    SYSMON_CHANNEL_VUSER3 = 35,

    SYSMON_CHANNEL_ARRAY_SIZE
} sysmon_channels_t;


/* Contains one SYSMON ADC sample */
typedef struct
{
    /* Set to true is the sample value has a defined reading */
    bool defined;
    /* The raw 10-bit ADC value */
    uint32_t raw_value;
    /* The value scaled into units */
    double scaled_value;
} sysmon_adc_sample_t;


/* Contains the samples from one SYSMON channel */
typedef struct
{
    /* The most recent measurement value */
    sysmon_adc_sample_t measurement;
    /* The min/max values sampled. Only defined when both:
     * a. The channel tracks the min/max value.
     * b. The value has changed below/above the initial measurement value since the SYSMON was reset. */
    sysmon_adc_sample_t min;
    sysmon_adc_sample_t max;
} sysmon_channel_sample_t;


/* The SYSMON Sequencer Operation mode */
typedef enum
{
    SYSMON_SEQUENCER_DEFAULT_MODE,
    SYSMON_SEQUENCER_SINGLE_PASS_SEQUENCE,
    SYSMON_SEQUENCER_CONTINUOUS_SEQUENCE_MODE,
    SYSMON_SEQUENCER_SINGLE_CHANNEL_MODE,
} sysmon_sequencer_mode_t;

extern const char *const sysmon_sequencer_mode_names[];


/* Contains a collection of the samples read from the SYSMON */
typedef struct
{
    /* The raw configuration register values */
    uint32_t configuration_register_0;
    uint32_t configuration_register_1;
    uint32_t configuration_register_2;
    uint32_t configuration_register_3;
    uint32_t configuration_register_4;
    /* The raw Analog Bus configuration register value.
     * This isn't decoded since:
     * a. The bits aren't documented in PG185
     * b. UG580 marks the register address as reserved.
     *
     * The value is displayed for diagnostics since the System Management Wizard appears to set the value according to the
     * User Supply Selection:
     * a. Each User Supply is allocated 4 bits.
     * b. Of which 2 bits appear to be selecting the monitored supply.
     * c. Of which 2 bits appear to be selecting the bank, in conjunction with the "quadrant" the bank is in.
     *
     * TBC:
     * a. If the Analog Bus configuration encodes the bank selection and monitored supply completely or if their are other
     *    configuration settings in the FPGA fabric the System Management Wizard controls.
     * b. If the register can be written at runtime to select monitoring of different supplies.
     * c. If do modify the Analog Bus configuration how to determine the bank to allow scale_sysmon_sample() to select
     *    either the 3V or 6V range. */
    uint32_t anlog_bus_configuration;
    /* To determine if the reference is internal or external */
    uint32_t flag_register;
    /* Zero means no averaging */
    uint32_t num_averaged_samples;
    /* Extracted from the configuration registers */
    sysmon_sequencer_mode_t sequencer_mode;
    /* The array of samples from the SYSMON channels */
    sysmon_channel_sample_t samples[SYSMON_CHANNEL_ARRAY_SIZE];
    /* Defines which channels are enabled in the (fast) sequencer */
    bool enabled_channels[SYSMON_CHANNEL_ARRAY_SIZE];
    /* Defines which channels are enabled in the slow sequencer */
    bool enabled_slow_channels[SYSMON_CHANNEL_ARRAY_SIZE];
    /* Defines which channels have averaging enabled */
    bool averaged_channels[SYSMON_CHANNEL_ARRAY_SIZE];
    /* Defines which SYSMON channels take bipolar measurements */
    bool bipolar_channels[SYSMON_CHANNEL_ARRAY_SIZE];
    /* Defines which SYSMON channels have longer acquisition time */
    bool channel_increased_acquisition_times[SYSMON_CHANNEL_ARRAY_SIZE];
} sysmon_sample_collection_t;


void read_sysmon_samples (sysmon_sample_collection_t *const collection, uint8_t *const sysmon_regs);
void display_sysmon_samples (const sysmon_sample_collection_t *const collection);

#endif /* XILINX_SYSMON_H_ */
