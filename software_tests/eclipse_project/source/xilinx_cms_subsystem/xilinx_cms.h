/*
 * @file xilinx_cms.h
 * @date Oct 25, 2025
 * @author Chester Gillon
 * @brief Provides a mechanism to access the Xilinx Card Management Solution Subsystem (CMS Subsystem) via VFIO
 */

#ifndef CMS_SUBSYSTEM_XILINX_CMS_H_
#define CMS_SUBSYSTEM_XILINX_CMS_H_

#include "vfio_access.h"

#include <time.h>


/* The available sensors which have measurement values */
typedef enum
{
    CMS_SENSOR_1V2_VCCIO,
    CMS_SENSOR_2V5_VPP23,
    CMS_SENSOR_3V3_AUX,
    CMS_SENSOR_3V3_PEX,
    CMS_SENSOR_3V3PEX_I_IN,
    CMS_SENSOR_12V_AUX,
    CMS_SENSOR_12V_AUX1,
    CMS_SENSOR_12V_AUX_I_IN,
    CMS_SENSOR_12V_PEX,
    CMS_SENSOR_12V_SW,
    CMS_SENSOR_12VPEX_I_IN,
    CMS_SENSOR_AUX_3V3_I,
    CMS_SENSOR_CAGE_TEMP0,
    CMS_SENSOR_CAGE_TEMP1,
    CMS_SENSOR_CAGE_TEMP2,
    CMS_SENSOR_CAGE_TEMP3,
    CMS_SENSOR_DDR4_VPP_BTM,
    CMS_SENSOR_DDR4_VPP_TOP,
    CMS_SENSOR_DIMM_TEMP0,
    CMS_SENSOR_DIMM_TEMP1,
    CMS_SENSOR_DIMM_TEMP2,
    CMS_SENSOR_DIMM_TEMP3,
    CMS_SENSOR_FAN_SPEED,
    CMS_SENSOR_FAN_TEMP,
    CMS_SENSOR_FPGA_TEMP,
    CMS_SENSOR_GTAVCC,
    CMS_SENSOR_GTVCC_AUX,
    CMS_SENSOR_HBM_1V2,
    CMS_SENSOR_HBM_1V2_I,
    CMS_SENSOR_HBM_TEMP1,
    CMS_SENSOR_HBM_TEMP2,
    CMS_SENSOR_MGT0V9AVCC,
    CMS_SENSOR_MGTAVCC,
    CME_SENSOR_MGTAVCC_I,
    CMS_SENSOR_MGTAVTT,
    CMS_SENSOR_MGTAVTT_I,
    CMS_SENSOR_PEX_3V3_POWER,
    CMS_SENSOR_PEX_12V_POWER,
    CMS_SENSOR_SE98_TEMP0,
    CMS_SENSOR_SE98_TEMP1,
    CMS_SENSOR_SE98_TEMP2,
    CMS_SENSOR_SYS_5V5,
    CMS_SENSOR_V12_IN_AUX0_I,
    CMS_SENSOR_V12_IN_AUX1_I,
    CMS_SENSOR_V12_IN_I,
    CMS_SENSOR_VCC0V85,
    CMS_SENSOR_VCC1V2_BTM,
    CMS_SENSOR_VCC1V2_I,
    CMS_SENSOR_VCC1V2_TOP,
    CMS_SENSOR_VCC1V5,
    CMS_SENSOR_VCC1V8,
    CMS_SENSOR_VCC3V3,
    CMS_SENSOR_VCC_5V0,
    CMS_SENSOR_VCCAUX,
    CMS_SENSOR_VCCAUX_PMC,
    CMS_SENSOR_VCCINT,
    CMS_SENSOR_VCCINT_I,
    CMS_SENSOR_VCCINT_IO,
    CMS_SENSOR_VCCINT_IO_I,
    CMS_SENSOR_VCCINT_POWER,
    CMS_SENSOR_VCCINT_TEMP,
    CMS_SENSOR_VCCINT_VCU_0V9,
    CMS_SENSOR_VCCRAM,
    CMS_SENSOR_VCCSOC,
    CMS_SENSOR_VPP2V5,

    /* These are power values derived by multiplying corresponding voltage and current measurements.
     * Must be last in the enumerations, so the the measurements values are read first. */
    CMS_SENSOR_12V_AUX_POWER,
    CMS_SENSOR_12V_PEX_POWER,
    CMS_SENSOR_3V3_PEX_POWER,
    CMS_SENSOR_3V3_AUX_POWER,

    CMS_SENSOR_ARRAY_SIZE
} cms_sensor_ids_t;


/* Defines a cached copy of the CMS mailbox used to populate a request, and then obtain the response */
#define CMS_MAILBOX_FRAME_SIZE_BYTES 0x1000
#define CMS_MAILBOX_MAX_PAYLOAD_SIZE_BYTES (CMS_MAILBOX_FRAME_SIZE_BYTES - sizeof (uint32_t))
#define CMS_MAILBOX_MAX_PAYLOAD_SIZE_WORDS (CMS_MAILBOX_MAX_PAYLOAD_SIZE_BYTES / sizeof (uint32_t))
typedef struct
{
    /* Controls the data size of the request written to the mailbox:
     * a. When request_fixed_size is true, request_payload_size_bytes gives the request size.
     * b. When request_fixed_size is false, the message header gives the request size. */
    bool request_fixed_size;
    uint32_t request_payload_size_bytes;
    /* Controls the data size of the response read from the mailbox:
     * a. When response_fixed_size is true, response_payload_size_bytes gives the response size.
     * b. WHen response_fixed_size is false, the message header gives the response size. */
    bool response_fixed_size;
    uint32_t response_payload_size_bytes;
    /* The contents of the mailbox as:
     * a. Header.
     * b. Variable size payload, indexed as either bytes or words which depends upon the type of Host Request or CMS Reply.
     * This contents is held in host memory, and copied to/from the CMS Mailbox during a transaction.
     * I.e. preserved across mailbox transactions. */
    uint32_t header;
    union
    {
        uint8_t  bytes[CMS_MAILBOX_MAX_PAYLOAD_SIZE_BYTES];
        uint32_t words[CMS_MAILBOX_MAX_PAYLOAD_SIZE_WORDS];
    } payload;
    /* The error register for mailbox. Non-zero means an error occurred, and the response isn't valid. */
    uint32_t host_msg_error_reg;
} cms_mailbox_t;


/* The software profile obtained from the PROFILE_NAME_REG. This is used to determine card specific features. */
typedef enum
{
    CMS_SOFTWARE_PROFILE_U200_U250,
    CMS_SOFTWARE_PROFILE_U280,
    CMS_SOFTWARE_PROFILE_U50,
    CMS_SOFTWARE_PROFILE_U55,
    CMS_SOFTWARE_PROFILE_UL3524,
    CMS_SOFTWARE_PROFILE_U45N,
    CMS_SOFTWARE_PROFILE_X3,
    CMS_SOFTWARE_PROFILE_UL3422,

    CMS_SOFTWARE_PROFILE_ARRAY_SIZE
} cms_software_profile_t;


/* The different display units for sensor measurement values */
typedef enum
{
    CMS_UNITS_MILLI_VOLTS,
    CMS_UNITS_MILLI_AMPS,
    CMS_UNITS_CELSIUS,
    CMS_UNITS_RPM,
    CMS_UNITS_MILLI_WATTS,
    CMS_UNITS_MICRO_WATTS
} cms_sensor_units_t;


/* The definition of one sensor with has measurement values */
typedef struct
{
    /* Name for display */
    const char *const name;
    /* How to display the values */
    cms_sensor_units_t units;
    /* When false reads measurement values.
     * When true derives power from other sensors. */
    bool derived_power;
    /* Register offset for maximum value */
    uint32_t max_reg_offset;
    /* Register offset for average value */
    uint32_t avg_reg_offset;
    /* Register offset for instantaneous value */
    uint32_t ins_reg_offset;
    /* Which card(s) support the sensors */
    bool supported_cards[CMS_SOFTWARE_PROFILE_ARRAY_SIZE];
    /* When derived_power is true, the sensors used to derive the power */
    cms_sensor_ids_t voltage_sensor;
    cms_sensor_ids_t current_sensor;
} cms_sensor_definition_t;


extern const cms_sensor_definition_t cms_sensor_definitions[CMS_SENSOR_ARRAY_SIZE];


/* The sensor IDs for the card information */
typedef enum
{
    CMS_SNSR_ID_CARD_SN,
    CMS_SNSR_ID_MAC_ADDRESS0,
    CMS_SNSR_ID_MAC_ADDRESS1,
    CMS_SNSR_ID_MAC_ADDRESS2,
    CMS_SNSR_ID_MAC_ADDRESS3,
    CMS_SNSR_ID_CARD_REV,
    CMS_SNSR_ID_CARD_NAME,
    CMS_SNSR_ID_SAT_VERSION,
    CMS_SNSR_ID_TOTAL_POWER_AVAIL,
    CMS_SNSR_ID_FAN_PRESENCE,
    CMS_SNSR_ID_CONFIG_MODE,
    CMS_SNSR_ID_NEW_MAC_SCHEME,
    CMS_SNSR_ID_CAGE_TYPE_00,
    CMS_SNSR_ID_CAGE_TYPE_01,
    CMS_SNSR_ID_CAGE_TYPE_02,
    CMS_SNSR_ID_CAGE_TYPE_03,

    CMS_SNSR_ID_ARRAY_SIZE
} cms_snsr_id_t;


/* The contents of one card information sensor */
typedef struct
{
    /* The number of bytes of data for the sensor. Zero if the sensor isn't available for the card. */
    size_t data_len;
    /* The data bytes for the sensor. NULL if the sensor isn't available for the card. */
    const uint8_t *data;
} cms_card_information_sensor_t;


/* Low speed IO signals which may be read for one QSFP module */
typedef struct
{
    /* false: Interrupt Set, true: Interrupt Clear */
    bool qsfp_int_l;
    /* false: Module Present, true: Module not Present */
    bool qsfp_modprs_l;
    /* false: Module Selected, true: Module not Selected */
    bool qsfp_modsel_l;
    /* false: High Power Mode, true: Low Power Mode */
    bool qsfp_lpmode;
    /* false: Reset Active, true: Reset Clear */
    bool qsfp_reset_l;
} cms_qsfp_low_speed_io_read_data_t;


/* The values for one sensor */
typedef struct
{
    /* True when the sensor values are valid, which is card specific */
    bool valid;
    /* The maximum value */
    uint32_t max;
    /* The average value */
    uint32_t average;
    /* The instantaneous value */
    uint32_t instantaneous;
} cms_sensor_values_t;


/* The collection of all sensors from the CMS subsystem */
typedef struct
{
    /* True when power good is indicated.
     * From PG348 it isn't clear if "power bad" will prevent the FPGA from working to allow the CMS subsystem to run. */
    bool power_good;
    /* The values for all sensors */
    cms_sensor_values_t sensors[CMS_SENSOR_ARRAY_SIZE];
} cms_sensor_collection_t;


/* Defines the context used to access a CMS Subsystem */
typedef struct
{
    /* Absolute timeout for a CMS operation */
    struct timespec cms_timeout;
    /* Mapped to the MicroBlaze reset register in the CMS */
    uint8_t *microblaze_reset_register;
    /* Mapped to the Host Interrupt Controller in the CMS. */
    uint8_t *host_interrupt_controller;
    /* Mapped to the Host/CMS shared memory */
    uint8_t *host_cms_shared_memory;
    /* Identifies which card the CMS is running on */
    cms_software_profile_t software_profile;
    /* Mapped to the Host/CMS mailbox */
    uint8_t *cms_mailbox_header;
    uint8_t *cms_mailbox_payload;
    /* Used to read the card information when initialise access, and kept to allow reference.
     * Done since the information is expected to be static. */
    cms_mailbox_t card_information_mailbox;
    /* The sensors in the card information. The data points at the card_information_mailbox */
    cms_card_information_sensor_t card_information_sensors[CMS_SNSR_ID_ARRAY_SIZE];
} xilinx_cms_context_t;


extern const char *const cms_software_profile_names[CMS_SOFTWARE_PROFILE_ARRAY_SIZE];
extern const uint32_t cms_num_qsfp_modules[CMS_SOFTWARE_PROFILE_ARRAY_SIZE];


bool cms_initialise_access (xilinx_cms_context_t *const context,
                            vfio_device_t *const vfio_device,
                            const uint32_t cms_subsystem_bar_index, const size_t cms_subsystem_base_offset);
bool cms_mailbox_transaction (xilinx_cms_context_t *const context, cms_mailbox_t *const transaction);
bool cms_read_qsfp_module_low_speed_io (xilinx_cms_context_t *const context, const uint32_t cage_select,
                                        cms_qsfp_low_speed_io_read_data_t *const low_speed_io);
void cms_display_configuration (const xilinx_cms_context_t *const context);
void cms_read_sensors (const xilinx_cms_context_t *const context, cms_sensor_collection_t *const collection);
void cms_display_sensors (const cms_sensor_collection_t *const collection);

#endif /* CMS_SUBSYSTEM_XILINX_CMS_H_ */
