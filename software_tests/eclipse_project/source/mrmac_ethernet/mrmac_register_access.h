/*
 * @file mrmac_register_access.h
 * @date 30 Mar 2026
 * @author Chester Gillon
 * @brief Provides functions to access the MRMAC Configuration registers, Status registers, and Statistics counters.
 */

#ifndef MRMAC_REGISTER_ACCESS_H_
#define MRMAC_REGISTER_ACCESS_H_

#include "mrmac_axi4_lite_registers.h"
#include "identify_pcie_fpga_design.h"


/* Specifies an iterator for operating on MRAMC ports.
 * No device filter, since can be handled via vfio_add_pci_device_location_filter() */
typedef struct
{
    /* The opened FPGA designs */
    fpga_designs_t *designs;
    /* Current design index for the iterator */
    uint32_t current_design_index;
    /* Current port on current_design_index for the iterator */
    uint32_t current_port_index;
    /* Optional filter for only a single port */
    uint32_t port_num_filter;
    bool port_num_filter_specified;
} mrmac_port_iterator_t;


extern const char *const mrmac_port_data_rate_names[];
extern const uint32_t mrmac_num_port_data_rate_names;
extern const char *const mrmac_axi4_stream_mode_names[][MRMAC_CTL_DATA_RATE_ARRAY_SIZE];
extern const uint32_t mrmac_num_axi4_stream_mode_names;
extern const char *const mrmac_gt_quad_operating_mode_names[][MRMAC_CTL_DATA_RATE_ARRAY_SIZE];
extern const uint32_t mrmac_num_gt_quad_operating_mode_names;
extern const char *const mrmac_fec_operating_mode_names[][MRMAC_CTL_FEC_MODE_ARRAY_SIZE];
extern const uint32_t mrmac_num_fec_operating_mode_names;


void display_mrmac_ports (const fpga_design_t *const design);
void mrmac_port_iterator_initialise (mrmac_port_iterator_t *const iterator, fpga_designs_t *const designs,
                                     const uint32_t port_num_filter, const bool port_num_filter_specified);
fpga_design_t *mrmac_port_iterator_next (mrmac_port_iterator_t *const iterator, uint32_t *const port_num);
void mrmac_reset_port (fpga_design_t *const design, const uint32_t port_num);

#endif /* MRMAC_REGISTER_ACCESS_H_ */
