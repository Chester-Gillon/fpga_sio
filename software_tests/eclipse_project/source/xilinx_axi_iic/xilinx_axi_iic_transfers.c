/*
 * @file xilinx_axi_iic_transfers.c
 * @date 22 Apr 2023
 * @author Chester Gillon
 * @brief Provides I2C transfers using the Xilinx "AXI IIC Bus Interface" which is accessed via VFIO from the host.
 * @details
 *  Uses "Standard Mode", so the transfer lengths are not limited by the 8-bit Dynamic Mode transfer length.
 *
 *  Restrictions are:
 *  a. Polls for transfer completion, so can't overlap with other work.
 *  b. Only supports 7-bit transfers.
 *  c. Only supports a I2C master. This was originally written for the I2C bus on the Trenz Electronic TEF1001-02-B2IX4-A which
 *     due to the CPLD mux between the FPGA with the IIC and the actual I2C bus doesn't allow an IIC slave as the SCL is output only.
 */

#include "xilinx_axi_iic_host_interface.h"
#include "vfio_access.h"
#include "xilinx_axi_iic_transfers.h"


/**
 * @brief Initialise the IIC controller, which will be used in "Standard Mode".
 * @param[out] controller The initialised controller context
 * @param[in] iic_regs The mapped registers for the Xilinx IIC
 * @return Indicates if the IIC controller was successfully initialised.
 */
iic_transfer_status_t iic_initialise_controller (iic_controller_context_t *const controller, uint8_t *const iic_regs)
{
    uint32_t iic_sr;
    iic_transfer_status_t status = IIC_TRANSFER_STATUS_SUCCESS;

    controller->iic_regs = iic_regs;
    controller->bus_claimed = false;
    iic_sr = read_reg32 (controller->iic_regs, IIC_STATUS_REGISTER_OFFSET);
    if ((iic_sr & IIC_SR_BB_MASK) != 0)
    {
        /* If the bus is busy then fail the initialisation.
         * @todo Could try and cause the controller to send a STOP in case the controller has been left claiming the bus
         *       from a previous run of a program. */
        status = IIC_TRANSFER_STATUS_BUS_BUSY;
    }

    return status;
}


/**
 * @brief Check if the I2C bus is in the expected state of busy or idle a transfer
 * @param[in] controller The IIC controller context to use
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the I2C bus is in the expected state
 */
static iic_transfer_status_t iic_check_bus_state_before_transfer (const iic_controller_context_t *const controller)
{
    iic_transfer_status_t status = IIC_TRANSFER_STATUS_SUCCESS;
    const uint32_t iic_sr = read_reg32 (controller->iic_regs, IIC_STATUS_REGISTER_OFFSET);
    const bool bus_busy = (iic_sr & IIC_SR_BB_MASK) != 0;

    /* The expected busy/idle state of the I2C bus depends upon if the controller is expected to have claimed the bus */
    if (controller->bus_claimed)
    {
        status = bus_busy ? IIC_TRANSFER_STATUS_SUCCESS : IIC_TRANSFER_STATUS_BUS_IDLE;
    }
    else
    {
        status = bus_busy ? IIC_TRANSFER_STATUS_BUS_BUSY : IIC_TRANSFER_STATUS_SUCCESS;
    }

    return status;
}


/**
 * @brief Clear the specified interrupts in the IIC Interrupt Status Register
 * @details It is non-destructive in that the register is read and only the interrupt specified is cleared.
 *          Clearing an interrupt acknowledges it.
 * @param[in] controller The IIC controller context to use
 * @param[in] interrupt_mask The bit mask of the interrupts to clear
 */
static void iic_clear_isr (const iic_controller_context_t *const controller, const uint32_t interrupt_mask)
{
    uint32_t iic_isr;

    iic_isr = read_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
    iic_isr &= interrupt_mask;
    write_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET, iic_isr);
}


/**
 * @brief Send the address for a 7-bit I2C slave address for either read or write operations
 * @param[in] controller The IIC controller context to use
 * @param[in] i2c_slave_address The 7-bit slave address which is the target of the operation
 * @param[in] operation IIC_TX_FIFO_READ_OPERATION or IIC_RX_FIFO_WRITE_OPERATION
 */
static void iic_send_7bit_address (iic_controller_context_t *const controller, const uint8_t i2c_slave_address,
                                   const uint32_t operation)
{
    uint32_t local_address;

    local_address = (i2c_slave_address << 1) & 0xFE;
    local_address |= operation;
    write_reg32 (controller->iic_regs, IIC_TX_FIFO_OFFSET, local_address);
}


/**
 * @brief Receive the specified data from the device that has been previously addressed on the I2C bus.
 * @details This function assumes that the 7 bit address has been sent and it should wait for the transmit of the
 *          address to complete.
 * @param[in] controller The IIC controller context to use
 * @param[in] num_bytes The number of bytes to read
 * @param[out] data The bytes read from the addresses device
 * @param[in] option Selects how the transfer will be terminated on the I2C bus
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the receive was successful.
 */
static iic_transfer_status_t iic_receive (iic_controller_context_t *const controller,
                                          const size_t num_bytes, uint8_t data[const num_bytes],
                                          const iic_transfer_option_t option)
{
    size_t remaining_bytes = num_bytes;
    uint32_t data_index = 0;
    uint32_t interrupt_status_mask;
    uint32_t iic_isr;
    uint32_t iic_cr;

    /* Attempt to receive the specified number of bytes from the I2C bus */
    while (remaining_bytes > 0)
    {
        /* Setup the mask to use for checking errors because when receiving one byte OR the last byte of a multibyte message an
         * error naturally occurs when the no ack is done to tell the slave the last byte */
        if (remaining_bytes == 1)
        {
            interrupt_status_mask = IIC_ISR_ARBITRATION_LOST_MASK | IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK;
        }
        else
        {
            interrupt_status_mask = IIC_ISR_ARBITRATION_LOST_MASK | IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK |
                    IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK;
        }

        /*  Wait for the previous transmit and the 1st receive to complete by checking the interrupt status register */
        for (;;)
        {
            iic_isr = read_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
            if ((iic_isr & IIC_ISR_RECEIVE_FIFO_FULL_MASK) != 0)
            {
                break;
            }

            /* Check the transmit error after the receive full because when sending only one byte transmit error
             * will occur because of the no ack to indicate the end of the data */
            if ((iic_isr & interrupt_status_mask) != 0)
            {
                return IIC_TRANSFER_STATUS_NO_ACK;
            }
        }

        iic_cr = read_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET);

        /* Special conditions exist for the last two bytes so check for them. Note that the control register must be setup for these
         * conditions before the data byte which was already received is read from the receive FIFO (while the bus is throttled) */
        if (remaining_bytes == 1)
        {
            if (option == IIC_TRANSFER_OPTION_STOP)
            {
                /* If the Option is to release the bus after the  last data byte, it has already been read and
                 * no ack has been done, so clear MSMS while leaving the device enabled so it can get off
                 * the IIC bus appropriately with a stop */
                write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_EN_MASK);
            }
        }

        /* Before the last byte is received, set NOACK to tell the slave IIC device that it is the end, this must be done before
         * reading the byte from the FIFO */
        if (remaining_bytes == 2)
        {
            /* Write control reg with NO ACK allowing last byte to have the No ack set to indicate to slave last byte read */
            write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, iic_cr | IIC_CR_TXAK_MASK);
        }

        /* Read in data from the FIFO and unthrottle the bus such that the next byte is read from the IIC bus */
        data[data_index] = (uint8_t) read_reg32 (controller->iic_regs, IIC_RX_FIFO_OFFSET);
        data_index++;

        if ((remaining_bytes == 1) && (option == IIC_TRANSFER_OPTION_REPEATED_START))
        {
            /* RSTA bit should be set only when the FIFO is completely Empty. */
            write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET,
                    IIC_CR_EN_MASK | IIC_CR_MSMS_MASK | IIC_CR_RSTA_MASK);
        }

        /* Clear the latched interrupt status so that it will be updated with the new state when it changes,
         * this must be done after the receive register is read */
        iic_clear_isr (controller, IIC_ISR_RECEIVE_FIFO_FULL_MASK |
                IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK |
                IIC_ISR_ARBITRATION_LOST_MASK);
        remaining_bytes--;
    }

    if (option == IIC_TRANSFER_OPTION_STOP)
    {
        /* If the Option is to release the bus after Reception of data, wait for the bus to transition to not busy before returning,
         * the IIC device cannot be disabled until this occurs. It should transition as the MSMS bit of the control register was
         * cleared before the last byte was read from the FIFO */
        do
        {
            iic_isr = read_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
        } while ((iic_isr & IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK) == 0);
    }

    return IIC_TRANSFER_STATUS_SUCCESS;
}


/**
 * @brief Send the specified buffer to the device that has been previously addressed on the IIC bus.
 * @details This function assumes that the 7 bit address has been sent and it should wait for the transmit of the
 *          address to complete.
 * @param[in] controller The IIC controller context to use
 * @param[in] num_bytes The number of bytes to write
 * @param[in] data The bytes written to the device address
 * @param[in] option Selects how the transfer will be terminated on the I2C bus
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the transmit was successful.
 */
static iic_transfer_status_t iic_send (iic_controller_context_t *const controller,
                                       const size_t num_bytes, const uint8_t data[const num_bytes],
                                       const iic_transfer_option_t option)
{
    size_t remaining_bytes = num_bytes;
    uint32_t data_index = 0;
    uint32_t iic_isr;

    /* Attempt to transmit the specified number of bytes to the I2C bus */
    while (remaining_bytes > 0)
    {
        /* Wait for the transmit to be empty before sending any more data by polling the interrupt status register */
        for (;;)
        {
            iic_isr = read_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);

            if ((iic_isr & IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK) != 0)
            {
                return IIC_TRANSFER_STATUS_BUS_IDLE;
            }
            else if ((iic_isr & IIC_ISR_ARBITRATION_LOST_MASK) != 0)
            {
                return IIC_TRANSFER_STATUS_ARBITRATION_LOST;
            }
            else if ((iic_isr & IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK) != 0)
            {
                return IIC_TRANSFER_STATUS_NO_ACK;
            }

            if ((iic_isr & IIC_ISR_TRANSMIT_FIFO_EMPTY_MASK) != 0)
            {
                break;
            }
        }

        /* If there is more than one byte to send then put the next byte to send into the transmit FIFO */
        if (remaining_bytes > 1)
        {
            write_reg32 (controller->iic_regs, IIC_TX_FIFO_OFFSET, data[data_index]);
            data_index++;
        }
        else
        {
            if (option == IIC_TRANSFER_OPTION_STOP)
            {
                /* If the Option is to release the bus after  the last data byte, Set the stop Option before sending the
                 * last byte of data so that the stop Option will be generated immediately following the data.
                 * This is done by clearing the MSMS bit in the control register. */
                write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_EN_MASK | IIC_CR_TX_MASK);
            }

            /* Put the last byte to send in the transmit FIFO */
            write_reg32 (controller->iic_regs, IIC_TX_FIFO_OFFSET, data[data_index]);
            data_index++;

            if (option == IIC_TRANSFER_OPTION_REPEATED_START)
            {
                iic_clear_isr (controller, IIC_ISR_TRANSMIT_FIFO_EMPTY_MASK);

                /* Wait for the transmit to be empty before setting RSTA bit. */
                for (;;)
                {
                    iic_isr = read_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
                    if ((iic_isr & IIC_ISR_TRANSMIT_FIFO_EMPTY_MASK) != 0)
                    {
                        /* RSTA bit should be set only when the FIFO is completely Empty. */
                        write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET,
                                IIC_CR_RSTA_MASK | IIC_CR_EN_MASK | IIC_CR_TX_MASK | IIC_CR_MSMS_MASK);
                        break;
                    }
                }
            }
        }

        /* Clear the latched interrupt status register and this must be done after the transmit FIFO has been written to
         * or it won't clear */
        iic_clear_isr (controller, IIC_ISR_TRANSMIT_FIFO_EMPTY_MASK);

        remaining_bytes--;
    }

    if (option == IIC_TRANSFER_OPTION_STOP)
    {
        /* If the Option is to release the bus after transmission of data, Wait for the bus to transition to not busy before
         * returning, the IIC device cannot be disabled until this  occurs. Note that this is different from a receive operation
         * because the stop Option causes the bus to go not busy. */
        for (;;)
        {
            iic_isr = read_reg32 (controller->iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
            if ((iic_isr & IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK) != 0)
            {
                break;
            }
        }
    }

    return IIC_TRANSFER_STATUS_SUCCESS;
}


/**
 * @brief Perform a read from the I2C bus
 * @param[in,out] controller The IIC controller context to use
 * @param[in] i2c_slave_address The 7-bit slave address to read from
 * @param[in] num_bytes The number of bytes to read
 * @param[out] data The bytes read from i2c_slave_address
 * @param[in] option Selects how the transfer will be terminated on the I2C bus
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS if the read was successful.
 */
iic_transfer_status_t iic_read (iic_controller_context_t *const controller, const uint8_t i2c_slave_address,
                                const size_t num_bytes, uint8_t data[const num_bytes],
                                const iic_transfer_option_t option)
{
    iic_transfer_status_t status;
    uint32_t iic_cr;
    uint32_t iic_sr;

    /* Check the bus state allows the transfer to be started */
    status = iic_check_bus_state_before_transfer (controller);
    if (status != IIC_TRANSFER_STATUS_SUCCESS)
    {
        return status;
    }

    /* Tx error is enabled in case the address has no device to answer with Ack.
     * When only one byte of data, must set NO ACK before address goes out therefore Tx error must not be enabled as it will go
     * off immediately and the Rx full interrupt will be checked.
     * If full, then the one byte was received and the Tx error will be disabled without indicating an error. */
    iic_clear_isr (controller, IIC_ISR_RECEIVE_FIFO_FULL_MASK |
            IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK |
            IIC_ISR_ARBITRATION_LOST_MASK);

    /* Set receive FIFO occupancy depth for 1 byte (zero based) */
    write_reg32 (controller->iic_regs, IIC_RX_FIFO_PIRQ_OFFSET, 0);

    /* Check to see if the master is already on the bus, according to the Repeated Restart bit */
    iic_cr = read_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET);
    if ((iic_cr & IIC_CR_RSTA_MASK) == 0)
    {
        /* Send the address for the read operation */
        iic_send_7bit_address (controller, i2c_slave_address, IIC_TX_FIFO_READ_OPERATION);

        /* MSMS gets set after putting data in FIFO. Start the master receive operation by setting CR Bits MSMS to Master,
         * if the buffer is only one byte, then it should not be acknowledged to indicate the end of data */
        iic_cr = IIC_CR_MSMS_MASK | IIC_CR_EN_MASK;
        if (num_bytes == 1)
        {
            iic_cr |= IIC_CR_TXAK_MASK;
        }

        /* Write out the control register to start receiving data */
        write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, iic_cr);

        /* Clear the latched interrupt status for the bus not busy bit which must be done while the bus is busy.
         * @todo This loop was based upon the XIic_Recv() function in
         *       https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/iic/src/xiic_l.c
         *
         *       There is a race condition that when step the code, and either there is no ACK or num_bytes==1
         *       that the loop doesn't sample the bus as busy and therefore gets stuck every time.
         *       The loop may also get stuck when not stepping if the process gets preempted for long enough. */
        do
        {
            iic_sr = read_reg32 (controller->iic_regs, IIC_STATUS_REGISTER_OFFSET);
        } while ((iic_sr & IIC_SR_BB_MASK) == 0);

        iic_clear_isr (controller, IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK);
    }
    else
    {
        /* Before writing 7bit slave address the Direction of Tx bit must be disabled */
        iic_cr &= ~IIC_CR_TX_MASK;
        if (num_bytes == 1)
        {
            iic_cr |= IIC_CR_TXAK_MASK;
        }
        write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, iic_cr);

        /* Already owns the Bus indicating that its a Repeated Start call.
         * 7 bit slave address, send the address for a read operation */
        iic_send_7bit_address (controller, i2c_slave_address, IIC_TX_FIFO_READ_OPERATION);
    }

    /* Try to receive the data from the I2C bus */
    status = iic_receive (controller, num_bytes, data, option);

    iic_cr = read_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET);
    if ((iic_cr & IIC_CR_RSTA_MASK) != 0)
    {
        controller->bus_claimed = true;
    }
    else
    {
        /* The receive is complete, disable the IIC device if the Option is to release the Bus after Reception of data */
        write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, 0);
        controller->bus_claimed = false;
    }

    return status;
}


/**
 * @brief Perform a write to the I2C bus
 * @param[in,out] controller The IIC controller context to use
 * @param[in] i2c_slave_address The 7-bit slave address to write to
 * @param[in] num_bytes The number of bytes to write
 * @param[in] data The bytes written to from i2c_slave_address
 * @param[in] option Selects how the transfer will be terminated on the I2C bus
 * @return Returns IIC_TRANSFER_STATUS_SUCCESS is the write was successful.
 */
iic_transfer_status_t iic_write (iic_controller_context_t *const controller, const uint8_t i2c_slave_address,
                                 const size_t num_bytes, const uint8_t data[const num_bytes],
                                 const iic_transfer_option_t option)
{
    iic_transfer_status_t status;
    uint32_t iic_cr;
    uint32_t iic_sr;

    /* Check the bus state allows the transfer to be started */
    status = iic_check_bus_state_before_transfer (controller);
    if (status != IIC_TRANSFER_STATUS_SUCCESS)
    {
        return status;
    }

    /* Check to see if already Master on the Bus.
     * If Repeated Start bit is not set send Start bit by setting MSMS bit else Send the address. */
    iic_cr = read_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET);
    if ((iic_cr & IIC_CR_RSTA_MASK) == 0)
    {
        /* Put the address into the FIFO to be sent and indicate that the operation to be performed on the bus is a write operation */
        iic_send_7bit_address (controller, i2c_slave_address, IIC_RX_FIFO_WRITE_OPERATION);

        /* Clear the latched interrupt status so that it will be updated with the new state when it changes, this
         * must be done after the address is put in the FIFO */
        iic_clear_isr (controller, IIC_ISR_TRANSMIT_FIFO_EMPTY_MASK |
                IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK |
                IIC_ISR_ARBITRATION_LOST_MASK);

        /* MSMS must be set after putting data into transmit FIFO, indicate the direction is transmit, this device is master
         * and enable the IIC device */
        write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET,
                IIC_CR_MSMS_MASK | IIC_CR_TX_MASK | IIC_CR_EN_MASK);

        /* Clear the latched interrupt status for the bus not busy bit which must be done while the bus is busy */
        do
        {
            iic_sr = read_reg32 (controller->iic_regs, IIC_STATUS_REGISTER_OFFSET);
        } while ((iic_sr & IIC_SR_BB_MASK) == 0);

        iic_clear_isr (controller, IIC_ISR_IIC_BUS_IS_NOT_BUSY_MASK);
    }
    else
    {
        /* Already owns the Bus indicating that its a Repeated Start call. 7 bit slave address, send the address for a write
         * operation and set the state to indicate the address has been sent. */
        iic_send_7bit_address (controller, i2c_slave_address, IIC_RX_FIFO_WRITE_OPERATION);
    }

    /* Send the specified data to the device on the IIC bus specified by the the address */
    status = iic_send (controller, num_bytes, data, option);

    iic_cr = read_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET);
    if ((iic_cr & IIC_CR_RSTA_MASK) != 0)
    {
        controller->bus_claimed = true;
    }
    else
    {
        /* The transmission is completed, disable the IIC device if the Option is to release the Bus after Reception of data */
        write_reg32 (controller->iic_regs, IIC_CONTROL_REGISTER_OFFSET, 0);
        controller->bus_claimed = false;
    }

    return status;
}
