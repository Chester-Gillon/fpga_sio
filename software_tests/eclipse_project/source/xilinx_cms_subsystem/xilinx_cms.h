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

#endif /* CMS_SUBSYSTEM_XILINX_CMS_H_ */
