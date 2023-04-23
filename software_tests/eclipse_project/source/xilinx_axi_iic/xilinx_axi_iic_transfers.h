/*
 * @file xilinx_axi_iic_transfers.h
 * @date 22 Apr 2023
 * @author Chester Gillon
 * @brief Provides I2C transfers using the Xilinx "AXI IIC Bus Interface" which is accessed via VFIO from the host.
 */

#ifndef XILINX_AXI_IIC_TRANSFERS_H_
#define XILINX_AXI_IIC_TRANSFERS_H_

#include <stdint.h>
#include <stdbool.h>

/* The status for an I2C transfer */
typedef enum
{
    /* No error detected */
    IIC_TRANSFER_STATUS_SUCCESS,
    /* The bus was unexpectedly busy, when the bus wasn't expected to be claimed by the IIC controller */
    IIC_TRANSFER_STATUS_BUS_BUSY,
    /* The bus was unexpectedly idle, when the bus was expected to be claimed by the IIC controller */
    IIC_TRANSFER_STATUS_BUS_IDLE,
    /* A transfer failed due to no acknowledgement from the address slave */
    IIC_TRANSFER_STATUS_NO_ACK
} iic_transfer_status_t;

/* The options for an I2C transfer which indicates how the transfer will terminated */
typedef enum
{
    /* An I2C STOP will be used to free the bus */
    IIC_TRANSFER_OPTION_STOP,
    /* The controller will be configured to use a repeated start for the next transfer */
    IIC_TRANSFER_OPTION_REPEATED_START
} iic_transfer_option_t;

/* The context for one IIC controller, used to perform I2C transfers */
typedef struct
{
    /* The mapped registers for the Xilinx IIC */
    uint8_t *iic_regs;
    /* Set true when a previous transfer ended without a STOP, meaning the bus is still claimed by the IIC controller
     * and therefore busy. */
    bool bus_claimed;
} iic_controller_context_t;


iic_transfer_status_t iic_initialise_controller (iic_controller_context_t *const controller, uint8_t *const iic_regs);
iic_transfer_status_t iic_read (iic_controller_context_t *const controller, const uint8_t i2c_slave_address,
                                const size_t num_bytes, uint8_t data[const num_bytes],
                                const iic_transfer_option_t option);

#endif /* XILINX_AXI_IIC_TRANSFERS_H_ */
