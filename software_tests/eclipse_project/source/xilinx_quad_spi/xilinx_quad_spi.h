/*
 * @file xilinx_quad_spi.h
 * @date 8 Jul 2023
 * @author Chester Gillon
 * @brief Defines an interface to Xilinx "AXI Quad Serial Peripheral Interface (SPI) core" to access the FPGA configuration flash
 */

#ifndef XILINX_QUAD_SPI_H_
#define XILINX_QUAD_SPI_H_

#include <stdint.h>
#include <stdbool.h>


/* The context for one Quad SPI controller, used to perform flash access */
typedef struct
{
    /* The mapped registers for the Xilinx Quad SPI */
    uint8_t *quad_spi_regs;
    /* The FIFO depth which has been configured in the Quad SPI core */
    uint32_t fifo_depth;
    /* The identification of the Quad SPI flash */
    uint8_t manufacturer_id;
    uint8_t memory_interface_type;
    uint8_t density;
} quad_spi_controller_context_t;


bool quad_spi_initialise_controller (quad_spi_controller_context_t *const controller, uint8_t *const quad_spi_regs);
bool quad_spi_read_identification (quad_spi_controller_context_t *const controller,
                                   uint8_t *const manufacturer_id,
                                   uint8_t *const memory_interface_type, uint8_t *const density);

#endif /* XILINX_QUAD_SPI_H_ */
