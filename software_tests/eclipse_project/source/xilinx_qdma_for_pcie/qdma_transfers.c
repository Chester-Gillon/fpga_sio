/*
 * @file qdma_transfers.c
 * @date 3 Jan 2026
 * @author Chester Gillon
 * @brief Implements a library for performing transfers using the Xilinx QDMA Subsystem for PCI Express, using VFIO
 * @details
 *   The initial implementation was created to test a QDMA Subsystem using memory mapped transfers, with a soft QDMA.
 *   I.e. doesn't support all the QDMA features.
 *   Currently only support physical functions.
 */

#include "qdma_transfers.h"
#include "qdma_pf_registers.h"

#include <string.h>
#include <stdio.h>


static const char *const qdma_rtl_version_names[] =
{
    [QDMA_RTL_PATCH] = "RTL Patch",
    [QDMA_RTL_BASE ] = "RTL Base",
    [QDMA_RTL_NONE ] = "RTL Unknown"
};

static const char *const qdma_device_type_names[] =
{
    [QDMA_DEVICE_SOFT       ] = "Soft IP",
    [QDMA_DEVICE_VERSAL_CPM4] = "Versal CPM4 Hard IP",
    [QDMA_DEVICE_VERSAL_CPM5] = "Versal Hard CPM5",
    [QDMA_DEVICE_NONE       ] = "Unknown"
};

static const char *const qdma_ip_type_names[] =
{
    [QDMA_VERSAL_HARD_IP] = "Versal Hard IP",
    [QDMA_VERSAL_SOFT_IP] = "Versal Soft IP",
    [QDMA_SOFT_IP       ] = "QDMA Soft IP",
    [EQDMA_SOFT_IP      ] = NULL, /* Also depends upon IP version */
    [QDMA_NONE_IP       ] = "Unknown"
};

static const char *const qdma_vivado_release_names[] =
{
    [QDMA_VIVADO_2018_3] = "vivado 2018.3",
    [QDMA_VIVADO_2019_1] = "vivado 2019.1",
    [QDMA_VIVADO_2019_2] = "vivado 2019.2",
    [QDMA_VIVADO_2020_1] = "vivado 2020.1",
    [QDMA_VIVADO_2020_2] = "vivado 2020.2",
    [QDMA_VIVADO_2021_1] = "vivado 2021.1",
    [QDMA_VIVADO_2022_1] = "vivado 2022.1",
    [QDMA_VIVADO_NONE  ] = "Unknown"
};


/**
 * @brief Identify a QDMA device, obtaining version information and capabilities of the device
 * @param[out] qdma_device Initialised context for the QDMA device
 * @param[in,out] vfio_device The underlying VFIO device
 * @param[in] qdma_bridge_bar Which BAR contains the QDMA control registers
 * @param[in] qdma_memory_size_bytes When non-zero the amount of memory addressed by the QDMA Subsystem
 * @param[in] The base address of the memory addressable by the QDMA Subsystem
 * @return Returns true if have identified a QDMA device, or false otherwise
 */
bool qdma_identify_device (qdma_device_context_t *const qdma_device,
                           vfio_device_t *const vfio_device, const uint32_t qdma_bridge_bar,
                           const size_t qdma_memory_base_address,const size_t qdma_memory_size_bytes)
{
    bool success;
    const size_t qdma_control_registers_base_offset = 0x00000;
    const size_t qdma_control_registers_frame_size  = 0x40000;

    /* Map the control registers */
    memset (qdma_device, 0, sizeof (*qdma_device));
    qdma_device->vfio_device = vfio_device;
    qdma_device->qdma_memory_base_address = qdma_memory_base_address;
    qdma_device->qdma_memory_size_bytes = qdma_memory_size_bytes;
    qdma_device->control_registers = map_vfio_registers_block (qdma_device->vfio_device, qdma_bridge_bar,
            qdma_control_registers_base_offset, qdma_control_registers_frame_size);
    success = qdma_device->control_registers != NULL;

    /* Check the QDMA IP identifier */
    if (success)
    {
        const uint32_t config_block_reg = read_reg32 (qdma_device->control_registers, QDMA_OFFSET_CONFIG_BLOCK_ID);
        const uint32_t ip_unique_id = vfio_extract_field_u32 (config_block_reg, QDMA_CONFIG_BLOCK_ID_MASK);

        success = ip_unique_id == QDMA_IDENTIFIER;
    }

    if (success)
    {
        const uint32_t version_reg_val = read_reg32 (qdma_device->control_registers, QDMA_OFFSET_GLBL2_MISC_CAP);
        const uint32_t rtl_version = vfio_extract_field_u32 (version_reg_val, QDMA_GLBL2_RTL_VERSION_MASK);
        const uint32_t vivado_release_id = vfio_extract_field_u32 (version_reg_val, QDMA_GLBL2_VIVADO_RELEASE_MASK);
        const uint32_t device_type = vfio_extract_field_u32 (version_reg_val, QDMA_GLBL2_DEVICE_ID_MASK);
        const uint32_t ip_type = vfio_extract_field_u32 (version_reg_val, QDMA_GLBL2_VERSAL_IP_MASK);

        switch (rtl_version)
        {
        case 0:
            qdma_device->version_info.rtl_version = QDMA_RTL_BASE;
            break;
        case 1:
            qdma_device->version_info.rtl_version = QDMA_RTL_PATCH;
            break;
        default:
            qdma_device->version_info.rtl_version = QDMA_RTL_NONE;
            break;
        }
        snprintf (qdma_device->version_info.qdma_rtl_version_str, sizeof (qdma_device->version_info.qdma_rtl_version_str), "%s",
                qdma_rtl_version_names[qdma_device->version_info.rtl_version]);

        switch (device_type)
        {
        case 0:
            qdma_device->version_info.device_type = QDMA_DEVICE_SOFT;
            break;
        case 1:
            qdma_device->version_info.device_type = QDMA_DEVICE_VERSAL_CPM4;
            break;
        case 2:
            qdma_device->version_info.device_type = QDMA_DEVICE_VERSAL_CPM5;
            break;
        default:
            qdma_device->version_info.device_type = QDMA_DEVICE_NONE;
            break;
        }
        snprintf (qdma_device->version_info.qdma_device_type_str, sizeof (qdma_device->version_info.qdma_device_type_str), "%s",
                qdma_device_type_names[qdma_device->version_info.device_type]);

        if (qdma_device->version_info.device_type == QDMA_DEVICE_SOFT)
        {
            switch (ip_type)
            {
            case 0:
                qdma_device->version_info.ip_type = QDMA_SOFT_IP;
                break;
            case 1:
            case 2:
                /* For QDMA4.0 and QDMA5.0, HW design and register map is same except some performance optimisations */
                qdma_device->version_info.ip_type = EQDMA_SOFT_IP;
                break;
            default:
                qdma_device->version_info.ip_type = QDMA_NONE_IP;
            }
        }
        else
        {
            switch (ip_type)
            {
            case 0:
                qdma_device->version_info.ip_type = QDMA_VERSAL_HARD_IP;
                break;
            case 1:
                qdma_device->version_info.ip_type = QDMA_VERSAL_SOFT_IP;
                break;
            default:
                qdma_device->version_info.ip_type = QDMA_NONE_IP;
            }
        }

        const char *ip_type_name = qdma_ip_type_names[qdma_device->version_info.ip_type];
        if (qdma_device->version_info.ip_type == EQDMA_SOFT_IP)
        {
            switch (ip_type)
            {
            case EQDMA_IP_VERSION_4:
                ip_type_name = "EQDMA4.0 Soft IP";
                break;

            case EQDMA_IP_VERSION_5:
                ip_type_name = "EQDMA5.0 Soft IP";
                break;

            default:
                ip_type_name = qdma_ip_type_names[QDMA_NONE_IP];
                break;
            }
        }
        snprintf (qdma_device->version_info.qdma_ip_type_str, sizeof (qdma_device->version_info.qdma_ip_type_str), "%s", ip_type_name);

        if (qdma_device->version_info.ip_type == QDMA_SOFT_IP)
        {
            switch (vivado_release_id)
            {
            case 0:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2018_3;
                break;
            case 1:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2019_1;
                break;
            case 2:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2019_2;
                break;
            default:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_NONE;
                break;
            }
        }
        else if (qdma_device->version_info.ip_type == EQDMA_SOFT_IP)
        {
            switch (vivado_release_id)
            {
            case 0:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2020_1;
                break;
            case 1:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2020_2;
                break;
            case 2:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2022_1;
                break;
            default:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_NONE;
                break;
            }
        }
        else if (qdma_device->version_info.device_type == QDMA_DEVICE_VERSAL_CPM5)
        {
            switch (vivado_release_id)
            {
            case 0:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2021_1;
                break;
            case 1:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2022_1;
                break;
            default:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_NONE;
                break;
            }
        }
        else
        {
            /* Versal case */
            switch (vivado_release_id)
            {
            case 0:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_2019_2;
                break;
            default:
                qdma_device->version_info.vivado_release = QDMA_VIVADO_NONE;
                break;
            }
        }
        snprintf (qdma_device->version_info.qdma_vivado_release_id_str, sizeof (qdma_device->version_info.qdma_vivado_release_id_str), "%s",
                qdma_vivado_release_names[qdma_device->version_info.vivado_release]);
    }

    return success;
}
