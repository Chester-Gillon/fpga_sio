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
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>

#include "xilinx_axi_iic_transfers.h"
#include "vfio_access.h"
#include "xilinx_axi_iic_host_interface.h"
#include "i2c_bit_banged.h"
#include "identify_pcie_fpga_design.h"

#ifdef HAVE_XILINX_EMBEDDEDSW
#include "xiic_l.h"
#endif


/* Command line argument which controls how the IIC is used */
typedef enum
{
    /* Standard mode access using functions in this file. */
    IIC_ACCESS_MODE_STANDARD,
    /* Dynamic mode access using functions in this file. */
    IIC_ACCESS_MODE_DYNAMIC,
    /* Uses the functions in xilinx_axi_iic_transfers. */
    IIC_ACCESS_MODE_IIC_LIB,
    /* Used bit-banged GPIO */
    IIC_ACCESS_MODE_BIT_BANGED,
#ifdef HAVE_XILINX_EMBEDDEDSW
    IIC_ACCESS_MODE_XIIC_LIB_STANDARD,
    IIC_ACCESS_MODE_XIIC_LIB_DYNAMIC,
#endif
} iic_access_mode_t;
static iic_access_mode_t arg_iic_access_mode = IIC_ACCESS_MODE_IIC_LIB;

static const char *const iic_access_mode_names[] =
{
    [IIC_ACCESS_MODE_STANDARD         ] = "standard",
    [IIC_ACCESS_MODE_DYNAMIC          ] = "dynamic",
    [IIC_ACCESS_MODE_IIC_LIB          ] = "iic_lib",
    [IIC_ACCESS_MODE_BIT_BANGED       ] = "bit_banged",
#ifdef HAVE_XILINX_EMBEDDEDSW
    [IIC_ACCESS_MODE_XIIC_LIB_STANDARD] = "xiic_lib_standard",
    [IIC_ACCESS_MODE_XIIC_LIB_DYNAMIC ] = "xiic_lib_dynamic"
#endif
};


/* Command line argument which controls the number of test iterations, to check if I2C addresses are reliably probed */
static uint32_t arg_num_iterations = 1;


/* Command line argument which controls the number of bytes read */
#define MAX_BYTES_READ 16
static uint32_t arg_num_bytes_read = 1;


/* Command line arguments which specify the range of I2C 7-bit addresses probed.
 * Default values exclude reserved addresses. */
static uint8_t arg_min_i2c_addr = 0x08;
static uint8_t arg_max_i2c_addr = 0x77;


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "m:i:n:a:?";
    int option;
    int min_i2c_addr;
    int max_i2c_addr;
    char junk;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'm':
            if (strcmp (optarg, "standard") == 0)
            {
                arg_iic_access_mode = IIC_ACCESS_MODE_STANDARD;
            }
            else if (strcmp (optarg, "dynamic") == 0)
            {
                arg_iic_access_mode = IIC_ACCESS_MODE_DYNAMIC;
            }
            else if (strcmp (optarg, "iic_lib") == 0)
            {
                arg_iic_access_mode = IIC_ACCESS_MODE_IIC_LIB;
            }
            else if (strcmp (optarg, "bit_banged") == 0)
            {
                arg_iic_access_mode = IIC_ACCESS_MODE_BIT_BANGED;
            }
#ifdef HAVE_XILINX_EMBEDDEDSW
            else if (strcmp (optarg, "xiic_lib_standard") == 0)
            {
                arg_iic_access_mode = IIC_ACCESS_MODE_XIIC_LIB_STANDARD;
            }
            else if (strcmp (optarg, "xiic_lib_dynamic") == 0)
            {
                arg_iic_access_mode = IIC_ACCESS_MODE_XIIC_LIB_DYNAMIC;
            }
#endif
            else
            {
                printf ("Error: Invalid access mode \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'i':
            if (sscanf (optarg, "%u%c", &arg_num_iterations, &junk) != 1)
            {
                printf ("Error: Invalid num_iterations \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'n':
            if ((sscanf (optarg, "%u%c", &arg_num_bytes_read, &junk) != 1) ||
                (arg_num_bytes_read < 1) || (arg_num_bytes_read > MAX_BYTES_READ))
            {
                printf ("Error: Invalid num_bytes_read \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'a':
            if ((sscanf (optarg, "%i:%i%c", &min_i2c_addr, &max_i2c_addr, &junk) != 2) ||
                (min_i2c_addr < 0) || (min_i2c_addr > 127) ||
                (max_i2c_addr < 0) || (max_i2c_addr > 127) ||
                (min_i2c_addr > max_i2c_addr))
            {
                printf ("Error: Invalid <min_i2c_addr>:<max_i2c_addr> \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_min_i2c_addr = (uint8_t) min_i2c_addr;
            arg_max_i2c_addr = (uint8_t) max_i2c_addr;
            break;

        case '?':
        default:
            printf ("Usage %s [-m standard|dynamic|iic_lib|bit_banged"
#ifdef HAVE_XILINX_EMBEDDEDSW
                    "|xiic_lib_standard|xiic_lib_dynamic"
#endif
                    "] [-i <num_iterations>] [-n <num_bytes_read>] [-a <min_i2c_addr>:<max_i2c_addr> \n", argv[0]);
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }

    if ((arg_num_bytes_read != 1) &&
        ((arg_iic_access_mode == IIC_ACCESS_MODE_STANDARD) || (arg_iic_access_mode == IIC_ACCESS_MODE_DYNAMIC)))
    {

        printf ("Error: num_bytes_read must be 1 when using standard or dynamic mode\n");
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief Use the Xilinx IIC, in dynamic mode, to perform a single byte read from an I2C address
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
    uint32_t tx_fifo_word; /* Write to the Tx FIFO as a 32-bit word to activate Dynamic Mode */

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
        *data_read = (uint8_t) read_reg32 (iic_regs, IIC_RX_FIFO_OFFSET);
        iic_sr = read_reg32 (iic_regs, IIC_STATUS_REGISTER_OFFSET);
    }

    /* Set the RX_FIFO depth to maximum */
    write_reg32 (iic_regs, IIC_RX_FIFO_PIRQ_OFFSET, 0xf);

    /* Reset the TX FIFO */
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_TX_FIFO_RESET_MASK);

    /* Enable the AXI IIC, remove the TX_FIFO reset, disable the general call. */
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_EN_MASK);

    /* Set start bit, device address and read access */
    tx_fifo_word = IIC_TX_FIFO_START_MASK | ((uint32_t) i2c_slave_address << 1) | IIC_TX_FIFO_READ_OPERATION;
    write_reg32 (iic_regs, IIC_TX_FIFO_OFFSET, tx_fifo_word);

    /* Set stop bit and indicate one byte to be read */
    tx_fifo_word = IIC_TX_FIFO_STOP_MASK | 0x01;
    write_reg32 (iic_regs, IIC_TX_FIFO_OFFSET, tx_fifo_word);

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
        *data_read = (uint8_t) read_reg32 (iic_regs, IIC_RX_FIFO_OFFSET);
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
 * @brief Use the Xilinx IIC, in standard mode, to perform a single byte read from an I2C address
 * @details When this function was first created the write to IIC_TX_FIFO_OFFSET and read from IIC_RX_FIFO_OFFSET were both
 *          done as 8-bits. However, that cause the function to intermittently lock-up in the loop waiting for
 *          IIC_ISR_TRANSMIT_ERROR_SLAVE_TRANSMIT_COMPLETE_MASK to be set.
 *
 *          When the function was changed to access IIC_TX_FIFO_OFFSET and IIC_RX_FIFO_OFFSET as 32-bits the lock-up no
 *          longer occurred. The idea to change to all 32-bit accesses was taken from the following change history comment
 *          in https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/iic/src/xiic_l.c :
 *             2.00a sdm  10/22/09 Converted all register accesses to 32 bit access.
 *
 *          Possibly the 8-bit write to the IIC_TX_FIFO_OFFSET caused the IIC to enter Dynamic Mode.
 * @param[in/out] iic_regs The mapped registers for the Xilinx IIC.
 * @param[in] i2c_slave_address 7-bit slave address to try and read from.
 * @param[out] data_read The byte read from the slave device. Only valid when the function returns true.
 * @return Returns true if read a byte from i2c_slave_address, or false otherwise.
 */
static bool i2c_standard_byte_read (uint8_t *const iic_regs, const uint8_t i2c_slave_address, uint8_t *const data_read)
{
    bool slave_responded;
    uint32_t iic_isr;
    uint32_t iic_sr;
    uint32_t iic_cr;
    uint32_t tx_fifo_word;

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
        *data_read = (uint8_t) read_reg32 (iic_regs, IIC_RX_FIFO_OFFSET);
        iic_sr = read_reg32 (iic_regs, IIC_STATUS_REGISTER_OFFSET);
    }

    /* Set the RX_FIFO depth to minimum */
    write_reg32 (iic_regs, IIC_RX_FIFO_PIRQ_OFFSET, 0);

    /* Reset the TX FIFO */
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, IIC_CR_TX_FIFO_RESET_MASK);

    /* Enable the AXI IIC, remove the TX_FIFO reset, disable the general call. */
    iic_cr = IIC_CR_EN_MASK;
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, iic_cr);

    /* Write the I2C slave address and indicate a read */
    tx_fifo_word = ((uint32_t) i2c_slave_address << 1) | IIC_TX_FIFO_READ_OPERATION;
    write_reg32 (iic_regs, IIC_TX_FIFO_OFFSET, tx_fifo_word);

    /* Leave TX clear as a receiver.
     * Set TXAK as only trying to read a single byte so need to NACK the byte.
     * Set MSMS to generate a START. */
    iic_cr |= IIC_CR_TXAK_MASK | IIC_CR_MSMS_MASK;
    write_reg32 (iic_regs, IIC_CONTROL_REGISTER_OFFSET, iic_cr);

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
        *data_read = (uint8_t) read_reg32 (iic_regs, IIC_RX_FIFO_OFFSET);
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
 * @param[in/out] design The FPGA design containing the peripherals to use for the probe
*/
static void probe_i2c_addresses (fpga_design_t *const design)
{
    uint32_t iic_sr;
    uint8_t data[MAX_BYTES_READ];
    bool slave_responded = false;
    uint8_t i2c_slave_address;
    uint32_t total_responses_per_address[I2C_MAX_NUM_7_BIT_ADDRESSES] = {0};
    iic_controller_context_t iic_controller = {0};
    bit_banged_i2c_controller_context_t bit_banged_controller = {0};
    iic_transfer_status_t transfer_status;
#ifdef HAVE_XILINX_EMBEDDEDSW
    int xiic_status;
    unsigned num_bytes_received;
#endif

    printf ("Using design %s in device %s\n", fpga_design_names[design->design_id], design->vfio_device->device_name);

    /* Perform access mode specific initialisation */
    select_i2c_controller (arg_iic_access_mode == IIC_ACCESS_MODE_BIT_BANGED, design->bit_banged_i2c_gpio_regs, &bit_banged_controller);
    switch (arg_iic_access_mode)
    {
    case IIC_ACCESS_MODE_STANDARD:
    case IIC_ACCESS_MODE_DYNAMIC:
        /* The IIC in the FPGA should be the only master, so an error if bus is busy before starting the probe.
         * Attempt one soft-reset of the IIC in case a 'glitch' from a previous run left the IIC in control of the I2C bus. */
        iic_sr = read_reg32 (design->iic_regs, IIC_STATUS_REGISTER_OFFSET);
        if ((iic_sr & IIC_SR_BB_MASK) != 0)
        {
            write_reg32 (design->iic_regs, IIC_SOFT_RESET_REGISTER_OFFSET, IIC_SOFT_RESET_KEY);
            iic_sr = read_reg32 (design->iic_regs, IIC_STATUS_REGISTER_OFFSET);
            if ((iic_sr & IIC_SR_BB_MASK) == 0)
            {
                printf ("Performed soft-reset of IIC to clear I2C bus busy\n");
            }
            else
            {
                printf ("I2C bus is busy, not probing\n");
                return;
            }
        }
        break;

    case IIC_ACCESS_MODE_IIC_LIB:
        transfer_status = iic_initialise_controller (&iic_controller, design->iic_regs);
        if (transfer_status != IIC_TRANSFER_STATUS_SUCCESS)
        {
            printf ("iic_initialise_controller() failed\n");
            return;
        }
        break;

#ifdef HAVE_XILINX_EMBEDDEDSW
    case IIC_ACCESS_MODE_XIIC_LIB_STANDARD:
        /* No initialise function in the Xilinx embeddedsw library for standard mode */
        break;

    case IIC_ACCESS_MODE_XIIC_LIB_DYNAMIC:
        xiic_status = XIic_DynInit ((UINTPTR) design->iic_regs);
        if (xiic_status != XST_SUCCESS)
        {
            printf ("XIic_DynInit() failed\n");
            return;
        }
        break;
#endif

    case IIC_ACCESS_MODE_BIT_BANGED:
        /* Handled by select_i2c_controller() call above */
        break;
    }

    for (uint32_t iteration = 1; iteration <= arg_num_iterations; iteration++)
    {
        printf ("Iteration %u of %u using IIC %s\n",
                iteration, arg_num_iterations, iic_access_mode_names[arg_iic_access_mode]);
        for (i2c_slave_address = arg_min_i2c_addr; i2c_slave_address <= arg_max_i2c_addr; i2c_slave_address++)
        {
            switch (arg_iic_access_mode)
            {
            case IIC_ACCESS_MODE_STANDARD:
                slave_responded = i2c_standard_byte_read (design->iic_regs, i2c_slave_address, data);
                break;

            case IIC_ACCESS_MODE_DYNAMIC:
                slave_responded = i2c_dynamic_byte_read (design->iic_regs, i2c_slave_address, data);
                break;

            case IIC_ACCESS_MODE_IIC_LIB:
                transfer_status = iic_read (&iic_controller, i2c_slave_address, arg_num_bytes_read, data, IIC_TRANSFER_OPTION_STOP);
                slave_responded = transfer_status == IIC_TRANSFER_STATUS_SUCCESS;
                break;

            case IIC_ACCESS_MODE_BIT_BANGED:
                slave_responded = bit_banged_i2c_read (&bit_banged_controller, i2c_slave_address, arg_num_bytes_read, data, true);
                break;

#ifdef HAVE_XILINX_EMBEDDEDSW
            case IIC_ACCESS_MODE_XIIC_LIB_STANDARD:
                num_bytes_received = XIic_Recv ((UINTPTR) design->iic_regs, i2c_slave_address, data, arg_num_bytes_read, XIIC_STOP);
                slave_responded = num_bytes_received == arg_num_bytes_read;
                break;

            case IIC_ACCESS_MODE_XIIC_LIB_DYNAMIC:
                num_bytes_received = XIic_DynRecv ((UINTPTR) design->iic_regs, i2c_slave_address, data, (u8) arg_num_bytes_read);
                slave_responded = num_bytes_received == arg_num_bytes_read;
                break;
#endif
            }

            if (slave_responded)
            {
                total_responses_per_address[i2c_slave_address]++;
                printf ("Slave 0x%02x replied with data", i2c_slave_address);
                for (uint32_t byte_index = 0; byte_index < arg_num_bytes_read; byte_index++)
                {
                    printf (" 0x%02x", data[byte_index]);
                }
                printf ("\n");
            }
        }
    }

    if (arg_num_iterations > 1)
    {
        /* Display the total number of responses to all addresses, as a summary */
        printf ("\nNumber of responses for each I2C address:\n");
        for (i2c_slave_address = arg_min_i2c_addr; i2c_slave_address <= arg_max_i2c_addr; i2c_slave_address++)
        {
            if (total_responses_per_address[i2c_slave_address] > 0)
            {
                printf ("0x%02x : %u\n", i2c_slave_address, total_responses_per_address[i2c_slave_address]);
            }
        }
    }
}


/**
 * @brief Probe the I2C addresses on both QSFP ports of the fpga_tests/XCKU5P_DUAL_QSFP_ibert_4.166 design
 * @details
 *   This design is handled as a special case since:
 *   a. It has independent IIC controllers, and the fpga_design_t structure only only contains a single iic_regs field.
 *   b. Each QSFP port has a MOD_SEL signal which must be low to enable I2C communication.
 * @param[in/out] design The FPGA design containing the peripherals to use for the probe
 */
static void probe_dual_qsfp_ports (fpga_design_t *const design)
{
    const char *const qsfp_port_names[] =
    {
        "A",
        "B"
    };
    const uint32_t num_qsfp_ports = sizeof (qsfp_port_names) / sizeof (qsfp_port_names[0]);

    for (uint32_t port_index = 0; port_index < num_qsfp_ports; port_index++)
    {
        /* Map the registers used for QSFP management */
        const uint32_t bar_index = 0;
        const size_t frame_size_per_port = 0x2000;
        const size_t overall_frame_size = num_qsfp_ports * frame_size_per_port;
        const size_t gpio_input_offset = 0x0;
        const size_t gpio_output_offset = 0x8;
        const size_t iic_offset = 0x1000;
        const size_t port_start_offset = port_index * frame_size_per_port;

        const uint8_t *const gpio_input =
                map_vfio_registers_block (design->vfio_device, bar_index, port_start_offset + gpio_input_offset, overall_frame_size);
        uint8_t *const gpio_output =
                map_vfio_registers_block (design->vfio_device, bar_index, port_start_offset + gpio_output_offset, overall_frame_size);
        design->iic_regs =
                map_vfio_registers_block (design->vfio_device, bar_index, port_start_offset + iic_offset, overall_frame_size);
        if ((gpio_input == NULL) || (gpio_output == NULL) || (design->iic_regs == NULL))
        {
            printf ("Failed to map registers for port %s\n", qsfp_port_names[port_index]);
            return;
        }

        /* Ensure the QSFP module is enabled for I2C access */
        const uint32_t mod_sel_mask = 1 << 3;
        uint32_t gpios = read_reg32 (gpio_input, 0);
        if ((gpios & mod_sel_mask) != 0)
        {
            gpios &= ~mod_sel_mask;
            write_reg32 (gpio_output, 0, gpios);
            printf ("Enabling MOD_SEL for QSFP port %s\n", qsfp_port_names[port_index]);
        }

        /* Perform the probe for the QSFP port */
        printf ("Probing QSFP port %s \n", qsfp_port_names[port_index]);
        probe_i2c_addresses (design);
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Perform tests on the FPGA designs which have the required I2C peripherals */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->design_id == FPGA_DESIGN_XCKU5P_DUAL_QSFP_IBERT)
        {
            if (arg_iic_access_mode == IIC_ACCESS_MODE_BIT_BANGED)
            {
                printf ("%s design doesn't support %s access mode\n",
                        fpga_design_names[design->design_id], iic_access_mode_names[arg_iic_access_mode]);
            }
            else
            {
                probe_dual_qsfp_ports (design);
            }
        }
        else if ((design->iic_regs != NULL) && (design->bit_banged_i2c_gpio_regs != NULL))
        {
            probe_i2c_addresses (design);
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
