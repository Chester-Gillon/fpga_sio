/*
 * @file qdma_transfers.h
 * @date 3 Jan 2026
 * @author Chester Gillon
 * @brief Defines an interface for performing transfers using the Xilinx QDMA Subsystem for PCI Express, using VFIO
 */

#ifndef QDMA_TRANSFERS_H_
#define QDMA_TRANSFERS_H_

#include "vfio_access.h"


/* QDMA HW version string array length */
#define QDMA_HW_VERSION_STRING_LEN          32

typedef enum
{
    /** @QDMA_RTL_BASE - RTL Base  */
    QDMA_RTL_BASE,
    /** @QDMA_RTL_PATCH - RTL Patch  */
    QDMA_RTL_PATCH,
    /** @QDMA_RTL_NONE - Not a valid RTL version */
    QDMA_RTL_NONE
} qdma_rtl_version_t;

typedef enum
{
    /** @QDMA_VIVADO_2018_3 - Vivado version 2018.3  */
    QDMA_VIVADO_2018_3,
    /** @QDMA_VIVADO_2019_1 - Vivado version 2019.1  */
    QDMA_VIVADO_2019_1,
    /** @QDMA_VIVADO_2019_2 - Vivado version 2019.2  */
    QDMA_VIVADO_2019_2,
    /** @QDMA_VIVADO_2020_1 - Vivado version 2020.1  */
    QDMA_VIVADO_2020_1,
    /** @QDMA_VIVADO_2020_2 - Vivado version 2020.2  */
    QDMA_VIVADO_2020_2,
    /** @QDMA_VIVADO_2021_1 - Vivado version 2021.1  */
    QDMA_VIVADO_2021_1,
    /** @QDMA_VIVADO_2022_1 - Vivado version 2022.1  */
    QDMA_VIVADO_2022_1,
    /** @QDMA_VIVADO_NONE - Not a valid Vivado version*/
    QDMA_VIVADO_NONE
} qdma_vivado_release_id_t;

typedef enum
{
    /** @QDMA_VERSAL_HARD_IP - Hard IP  */
    QDMA_VERSAL_HARD_IP,
    /** @QDMA_VERSAL_SOFT_IP - Soft IP  */
    QDMA_VERSAL_SOFT_IP,
    /** @QDMA_SOFT_IP - Hard IP  */
    QDMA_SOFT_IP,
    /** @EQDMA_SOFT_IP - Soft IP  */
    EQDMA_SOFT_IP,
    /** @QDMA_VERSAL_NONE - Not versal device  */
    QDMA_NONE_IP
} qdma_ip_type_t;

typedef enum
{
    /** @QDMA_DEVICE_SOFT - UltraScale+ IP's  */
    QDMA_DEVICE_SOFT,
    /** @QDMA_DEVICE_VERSAL_CPM4 -VERSAL IP  */
    QDMA_DEVICE_VERSAL_CPM4,
    /** @QDMA_DEVICE_VERSAL_CPM5 -VERSAL IP  */
    QDMA_DEVICE_VERSAL_CPM5,
    /** @QDMA_DEVICE_NONE - Not a valid device  */
    QDMA_DEVICE_NONE
} qdma_device_type_t;

typedef enum
{
    /** @QDMA_DESC_ENG_INTERNAL_BYPASS - Internal and Bypass mode */
    QDMA_DESC_ENG_INTERNAL_BYPASS,
    /** @QDMA_DESC_ENG_BYPASS_ONLY - Only Bypass mode  */
    QDMA_DESC_ENG_BYPASS_ONLY,
    /** @QDMA_DESC_ENG_INTERNAL_ONLY - Only Internal mode  */
    QDMA_DESC_ENG_INTERNAL_ONLY,
    /** @QDMA_DESC_ENG_MODE_MAX - Max of desc engine modes  */
    QDMA_DESC_ENG_MODE_MAX
} qdma_desc_eng_mode_t;


/* The QDMA device hardware version information */
typedef struct
{
    /* RTL Version */
    qdma_rtl_version_t rtl_version;
    /* Vivado Release id */
    qdma_vivado_release_id_t vivado_release;
    /* Versal IP state */
    qdma_ip_type_t ip_type;
    /* Device Type */
    qdma_device_type_t device_type;
    /* RTL Version string*/
    char qdma_rtl_version_str[QDMA_HW_VERSION_STRING_LEN];
    /* Vivado Release id string */
    char qdma_vivado_release_id_str[QDMA_HW_VERSION_STRING_LEN];
    /* Qdma device type string */
    char qdma_device_type_str[QDMA_HW_VERSION_STRING_LEN];
    /* Versal IP state string */
    char qdma_ip_type_str[QDMA_HW_VERSION_STRING_LEN];
} qdma_hw_version_info_t;


/* QDMA device attributes */
typedef struct
{
    /** @num_pfs - Num of PFs*/
    uint32_t num_pfs;
    /** @num_qs - Num of Queues */
    uint32_t num_qs;
    /** @flr_present - FLR resent or not? */
    bool flr_present;
    /** @st_en - ST mode supported or not? */
    bool st_en;
    /** @mm_en - MM mode supported or not? */
    bool mm_en;
    /** @mm_cmpt_en - MM with Completions supported or not?
     *
     * In https://github.com/Xilinx/dma_ip_drivers when (!mm_en && !mm_cmpt_en) modifications to the following are not allowed:
     * - Counter thresholds
     * - Timer Counters
     * - Writeback Interval */
    bool mm_cmpt_en;
    /** @mailbox_en - Mailbox supported or not? */
    bool mailbox_en;
    /** @debug_mode - Debug mode is enabled/disabled for IP */
    bool debug_mode;
    /** @desc_eng_mode - Descriptor Engine mode:
     * Internal only/Bypass only/Internal & Bypass
     */
    qdma_desc_eng_mode_t desc_eng_mode;
    /** @mm_channel_max - Num of MM channels */
    uint32_t mm_channel_max;

    /** Below are the list of HW features which are populated by qdma_access based on RTL version
     */
    /** @qid2vec_ctx - To indicate support of qid2vec context
     *
     * https://github.com/Xilinx/dma_ip_drivers only uses for QDMA_DEVICE_VERSAL_CPM4 */
    bool qid2vec_ctx;
    /** @cmpt_ovf_chk_dis - To indicate support of overflow check disable in CMPT ring
     *
     * In https://github.com/Xilinx/dma_ip_drivers this doesn't seem to control anything. */
    bool cmpt_ovf_chk_dis;
    /** @mailbox_intr - To indicate support of mailbox interrupt
     *
     * In https://github.com/Xilinx/dma_ip_drivers this doesn't seem to control anything. */
    bool mailbox_intr;
    /** @sw_desc_64b - To indicate support of 64 bytes C2H/H2C descriptor format
     *
     * In https://github.com/Xilinx/dma_ip_drivers this doesn't seem to control anything. */
    bool sw_desc_64b;
    /** @cmpt_desc_64b - To indicate support of 64 bytes CMPT descriptor format
     *
     * In https://github.com/Xilinx/dma_ip_drivers this doesn't seem to control anything. */
    bool cmpt_desc_64b;
    /** @dynamic_bar - To indicate support of dynamic bar detection
     *
     * In https://github.com/Xilinx/dma_ip_drivers this doesn't seem to control anything. */
    bool dynamic_bar;
    /** @legacy_intr - To indicate support of legacy interrupt
     *
     * In https://github.com/Xilinx/dma_ip_drivers this controls if the module is allowed to use LEGACY_INTR_MODE */
    bool legacy_intr;
    /** @cmpt_trig_count_timer - To indicate support of counter + timer trigger mode.
     *
     * In https://github.com/Xilinx/dma_ip_drivers this doesn't seem to control anything. */
    bool cmpt_trig_count_timer;
} qdma_dev_attributes_t;


/* The context for one QDMA device */
typedef struct
{
    /* Points at the underlying VFIO device */
    vfio_device_t *vfio_device;
    /* When non-zero the amount of memory addressed by the QDMA Subsystem. */
    size_t qdma_memory_size_bytes;
    /* The base address of the memory addressable by the QDMA Subsystem */
    size_t qdma_memory_base_address;
    /* Mapped to the QDMA control registers */
    uint8_t *control_registers;
    /* The QDMA hardware version information extracted from registers */
    qdma_hw_version_info_t version_info;
    /* The QDMA device capability information extracted from the version_info and other registers */
    qdma_dev_attributes_t dev_cap;
} qdma_device_context_t;


extern const char *const qdma_desc_eng_mode_names[];


bool qdma_identify_device (qdma_device_context_t *const qdma_device,
                           vfio_device_t *const vfio_device, const uint32_t qdma_bridge_bar,
                           const size_t qdma_memory_base_address,const size_t qdma_memory_size_bytes);

#endif /* QDMA_TRANSFERS_H_ */
