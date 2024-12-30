/*
 * @file pci_sysfs_access.h
 * @date 25 May 2024
 * @author Chester Gillon
 * @brief Functions to access PCI devices by Linux /sys/bus/pci
 */

#ifndef PCI_SYSFS_ACCESS_H_
#define PCI_SYSFS_ACCESS_H_

#include <stdint.h>


char *pci_sysfs_read_device_symlink_name (const uint32_t domain, const uint32_t bus, const uint32_t dev, const uint32_t func,
                                          const char *const property_name);
char *pci_sysfs_read_physical_slot (const uint32_t domain, const uint32_t bus, const uint32_t dev);

#endif /* PCI_SYSFS_ACCESS_H_ */
