/*
 * @file i2c_probe.c
 * @date 25 Feb 2023
 * @author Chester Gillon
 * @brief Perform a I2C probe using the Xilinx AXI IIC Bus Interface PG090
 * @details This was written for an initial test of the /fpga_tests/i2c_probe FPGA image which just provides access to
 *          the I2C bus on the Trenz Electronic TEF1001-02-B2IX4-A.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "vfio_access.h"
#include "fpga_sio_pci_ids.h"
#include "xilinx_axi_iic_host_interface.h"


/**
 * @brief Use the Xilinx IIC to perform a single byte read from an I2C address
 * @detail This was written to probe which I2C addresses their are slaves for.
 *         The actual return byte value could be undefined depending upon the slave device, as no register address
 *         is written in the transfer.
 *
 *         The sequence of operations was derived from the "Pseudo Code for Dynamic IIC Accesses" in PG090.
 *         PG090 doesn't seem specifically clear on how to determine if there was a response by the slave or not.
 * @param[in/out] iic_regs The mapped registers for the Xilinx IIC.
 * @param[in] i2c_slave_address 7-bit slave address to try and read from.
 * @param[out] data_read The byte read from the slave device. Only valid when the function returns true.
 * @return Returns true if read a byte from i2c_slave_address, or false otherwise.
 */
static bool i2c_dynamic_byte_read (uint8_t *const iic_regs, const uint8_t i2c_slave_address, uint8_t *const data_read)
{
    bool slave_responded;
    uint32_t iic_isr;
    uint32_t iic_sr;
    uint16_t tx_fifo_word; /* Write to the Tx FIFO as a 16-bit word to activate Dynamic Mode */

    /* Clear any completion interrupt from a previous test */
    iic_isr = read_reg32 (iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
    if ((iic_isr & IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK) != 0)
    {
        write_reg32 (iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET, IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK);
    }

    /* Flush the Rx FIFO */
    iic_sr = read_reg32 (iic_regs, IIC_STATUS_REGISTER_OFFSET);
    while ((iic_sr & IIC_SR_RX_FIFO_EMPTY_MASK) == 0)
    {
        *data_read = read_reg8 (iic_regs, IIC_RX_FIFO_OFFSET);
        iic_sr = read_reg32 (iic_regs, IIC_STATUS_REGISTER_OFFSET);
    }

    /* Set the RX_FIFO depth to maximum */
    write_reg32 (iic_regs, IIC_RX_FIFO_PIRQ_OFFSET, 0xf);

    /* Reset the TX FIFO */
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_TX_FIFO_RESET_MASK);

    /* Enable the AXI IIC, remove the TX_FIFO reset, disable the general call. */
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_EN_MASK);

    /* Set start bit, device address and read access */
    tx_fifo_word = IIC_TX_FIFO_START_MASK | (i2c_slave_address << 1) | 0x01;
    write_reg16 (iic_regs, IIC_TX_FIFO_OFFSET, tx_fifo_word);

    /* Set stop bit and indicate one byte to be read */
    tx_fifo_word = IIC_TX_FIFO_STOP_MASK | 0x01;
    write_reg16 (iic_regs, IIC_TX_FIFO_OFFSET, tx_fifo_word);

    /* Wait for receive to complete, without or without error.
     * The assumption is:
     * a. When there is no ACK for the slave address, IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK
     *    is set when an error occurs transmitting the slave address.
     * b. When there is a response from the slave address, IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK
     *    is set once the read is complete. */
    do
    {
        iic_isr = read_reg32 (iic_regs, IIC_INTERRUPT_STATUS_REGISTER_OFFSET);
    } while ((iic_isr & IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK) == 0);

    /* The presence of data in the Rx FIFO is used to determine if the slave responded or not */
    iic_sr = read_reg32 (iic_regs, IIC_STATUS_REGISTER_OFFSET);
    if ((iic_sr & IIC_SR_RX_FIFO_EMPTY_MASK) == 0)
    {
        /* The slave responded with a byte */
        *data_read = read_reg8 (iic_regs, IIC_RX_FIFO_OFFSET);
        slave_responded = true;
    }
    else
    {
        /* No response from a slave */
        slave_responded = false;
    }

    return slave_responded;
}


/**
 * @brief Probe the range valid I2C 7-bit addresses to see which addresses respond
 * @details For debugging displays the value of the byte read in any response
 * @param[in/out] vfio_device The VFIO device containing the Xilinx IIC to use for the probe
 */
static void probe_i2c_addresses (vfio_device_t *const vfio_device)
{
    uint32_t iic_sr;
    uint8_t data_read;

    /* The FPGA has a single BAR with the IIC registers at offset zero in the BAR */
    const int iic_bar_index = 0;

    /* Range of valid I2C 7-bit addresses excluding reserved addresses */
    const uint8_t min_i2c_addr = 0x08;
    const uint8_t max_i2c_addr = 0x77;

    map_vfio_device_bar_before_use (vfio_device, iic_bar_index);
    if (vfio_device->mapped_bars[iic_bar_index] != NULL)
    {
        uint8_t *const iic_regs = vfio_device->mapped_bars[iic_bar_index];

        printf ("Using BAR %d in device %s of size 0x%llx\n",
                iic_bar_index, vfio_device->device_name, vfio_device->regions_info[iic_bar_index].size);

        /* The IIC in the FPGA should be the only master, so an error if bus is busy before starting the probe */
        iic_sr = read_reg32 (iic_regs, IIC_STATUS_REGISTER_OFFSET);
        if ((iic_sr & IIC_SR_BB_MASK) != 0)
        {
            printf ("I2C bus is busy, not probing\n");
            return;
        }

        for (uint8_t i2c_slave_address = min_i2c_addr; i2c_slave_address <= max_i2c_addr; i2c_slave_address++)
        {
            if (i2c_dynamic_byte_read (iic_regs, i2c_slave_address, &data_read))
            {
                printf ("Slave 0x%02x replied with data 0x%02x\n", i2c_slave_address, data_read);
            }
        }
    }
}

int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    /* Filters for the FGPA devices tested */
    const vfio_pci_device_filter_t filters[] =
    {
        {
            .vendor_id = FPGA_SIO_VENDOR_ID,
            .device_id = VFIO_PCI_DEVICE_FILTER_ANY,
            .subsystem_vendor_id = FPGA_SIO_SUBVENDOR_ID,
            .subsystem_device_id = FPGA_SIO_SUBDEVICE_ID_I2C_PROBE,
            .enable_bus_master = false
        }
    };
    const size_t num_filters = sizeof (filters) / sizeof (filters[0]);

    /* Open PCI devices supported by the test */
    open_vfio_devices_matching_filter (&vfio_devices, num_filters, filters);

    /* Perform tests on the FPGA devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        probe_i2c_addresses (vfio_device);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}