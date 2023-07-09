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

extern const char *const fury_names[];
extern const size_t fury_ddr_sizes_bytes[];

#define FURY_AXI_PERIPHERALS_BAR 0
#define FURY_DMA_BRIDGE_BAR      2

/* Offsets in FURY_AXI_PERIPHERALS_BAR */
#define FURY_AXI_GPIO_0_BASE_OFFSET    0x0000
#define FURY_AXI_GPIO_1_BASE_OFFSET    0x1000
#define FURY_AXI_GPIO_2_BASE_OFFSET    0x2000
#define FURY_AXI_XADC_WIZ_BASE_OFFSET  0x3000
#define FURY_AXI_QUAD_SPI_BASE_OFFSET 0x10000

extern const vfio_pci_device_filter_t fury_pci_device_filters[];
extern const size_t fury_num_pci_device_filters;

fury_type_t identify_fury (vfio_device_t *const vfio_device, uint32_t *const board_version);
void display_fury_xadc_values (vfio_devices_t *const vfio_devices);
void display_open_fds (const char *const process_name);

#endif /* SOURCE_NITE_OR_LITE_FURY_TESTS_FURY_UTILS_H_ */
