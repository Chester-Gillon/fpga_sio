/*
 * @file pci_sysfs_access.h
 * @date 25 May 2024
 * @author Chester Gillon
 * @brief Functions to access PCI devices by Linux /sys/bus/pci
 */

#ifndef SOURCE_VFIO_ACCESS_PCI_SYSFS_ACCESS_H_
#define SOURCE_VFIO_ACCESS_PCI_SYSFS_ACCESS_H_

#include <stdint.h>


char *pci_sysfs_read_device_symlink_name (const uint32_t domain, const uint32_t bus, const uint32_t dev, const uint32_t func,
                                          const char *const property_name);

#endif /* SOURCE_VFIO_ACCESS_PCI_SYSFS_ACCESS_H_ */
