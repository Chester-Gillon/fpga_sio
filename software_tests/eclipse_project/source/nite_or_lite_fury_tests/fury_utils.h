/*
 * @file fury_utils.h
 * @date 28 Jan 2023
 * @author Chester Gillon
 * @brief Utilities to support testing a NiteFury or LiteFury
 */

#ifndef SOURCE_NITE_OR_LITE_FURY_TESTS_FURY_UTILS_H_
#define SOURCE_NITE_OR_LITE_FURY_TESTS_FURY_UTILS_H_

#include "vfio_access.h"

/* Used to determine if a PCI device is a NiteFury or LiteFury */
typedef enum
{
    DEVICE_NITE_FURY,
    DEVICE_LITE_FURY,
    DEVICE_OTHER
} fury_type_t;

#define FURY_AXI_PERIPHERALS_BAR 0
#define FURY_DMA_BRIDGE_BAR      2

fury_type_t identify_fury (const vfio_device_t *const vfio_device, uint32_t *const board_version);

#endif /* SOURCE_NITE_OR_LITE_FURY_TESTS_FURY_UTILS_H_ */
