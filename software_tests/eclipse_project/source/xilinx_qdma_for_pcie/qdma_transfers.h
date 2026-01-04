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
} qdma_device_context_t;


bool qdma_identify_device (qdma_device_context_t *const qdma_device,
                           vfio_device_t *const vfio_device, const uint32_t qdma_bridge_bar,
                           const size_t qdma_memory_base_address,const size_t qdma_memory_size_bytes);

#endif /* QDMA_TRANSFERS_H_ */
