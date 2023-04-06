/*
 * @file sealevel_serial_7205e_uart_tests.c
 * @date 19 Feb 2023
 * @author Chester Gillon
 * @brief Perform internal UART loopback tests on a Sealevel COMM+2.LPCIe board (7205e), using VFIO
 * @details This is a version of https://github.com/Chester-Gillon/plx_poll_mode_driver/blob/master/plx_poll_mode_driver/plx_poll_mode_driver.c
 *          which uses VFIO to access the device, rather than a Plx Kernel module and user space library.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "vfio_access.h"
#include "serial_reg.h"


/* The number of 16C950 UARTs on the Sealevel COMM+2.LPCIe board (7205e) */
#define NUM_UARTS 2

/* For a 16C950 */
#define UART_FIFO_DEPTH 128


/* Structure to access one 16C950 UART, as a 8-bit wide device on the local bus of a PEX8311.
 * Each UART is mapped as one bar in memory space. */
typedef struct
{
    /** The index of the PCI BAR to which the UART is mapped */
    int bar_index;
    /** The virtual address which is mapped to the PCI BAR to allow direct access to the UART registers */
    uint8_t *bar_mapping;
    /** Tracks registers which have to be be temporarily changed without affecting operational mode */
    uint8_t acr;
    uint8_t lcr;
} uart_port_t;


/**
 * @brief Write to a UART register
 * @param[in] port Which UART to write to
 * @param[in] offset The register offset to write to
 * @param[in] value The register value to write
 */
static void serial_out (uart_port_t *const port, const uint32_t offset, const uint8_t value)
{
    write_reg8 (port->bar_mapping, offset, value);
}


/**
 * @brief Read from a UART register
 * @param[in] port Which UART to read from
 * @param[in] offset The register offset to read from
 * @return The register value
 */
static uint8_t serial_in (uart_port_t *const port, const uint32_t offset)
{
    return read_reg8 (port->bar_mapping, offset);
}


/*
 * For the 16C950
 */
static void serial_icr_write (uart_port_t *const port, const uint8_t offset, const uint8_t value)
{
    serial_out (port, UART_SCR, offset);
    serial_out (port, UART_ICR, value);
}

static unsigned int serial_icr_read (uart_port_t *const port, const uint8_t offset)
{
    unsigned int value;

    serial_icr_write (port, UART_ACR, port->acr | UART_ACR_ICRRD);
    serial_out (port, UART_SCR, offset);
    value = serial_in (port, UART_ICR);
    serial_icr_write (port, UART_ACR, port->acr);

    return value;
}


/**
 * @brief Read an additional status register which is a 16C950 specific register
 * @param[in/out] Which UART to read from
 * @param[in] offset The offset of the additional status register to read
 * @return The value of the additional status register
 */
static unsigned int serial_additional_status_read (uart_port_t *const port, const uint8_t offset)
{
    unsigned int value;

    serial_icr_write (port, UART_ACR, port->acr | UART_ACR_ASE);
    value = serial_in (port, offset);
    serial_icr_write (port, UART_ACR, port->acr);

    return value;
}


/**
 * @brief Read the ID bytes of the UART checking that find a 16C950
 * @param[in] port Which UART to auto-detect
 */
static void check_16c950_id (uart_port_t *const port)
{
    /*
     * The 16C950 requires 0xbf to be written to the LCR to read the ID.
     */
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_B);
    if (serial_in (port, UART_EFR) == 0)
    {
        uint8_t id1, id2, id3, rev;

        /*
         * Check for Oxford Semiconductor 16C950.
         */
        port->acr = 0;
        serial_out (port, UART_LCR, UART_LCR_CONF_MODE_B);
        serial_out (port, UART_EFR, UART_EFR_ECB);
        serial_out (port, UART_LCR, 0x00);
        id1 = serial_icr_read (port, UART_ID1);
        id2 = serial_icr_read (port, UART_ID2);
        id3 = serial_icr_read (port, UART_ID3);
        rev = serial_icr_read (port, UART_REV);

        if ((id1 == 0x16) && (id2 == 0xC9) && (id3 == 0x50) && (rev == 0x03))
        {
            printf ("Detected 16C950 rev B on bar_index %u\n", port->bar_index);
        }
        else
        {
            printf ("Unknown EFR device on bar_index=%u : id1=0x%x id2=0x%x id3=0x%x rec=0x%x\n",
                    port->bar_index, id1, id2, id3, rev);
            exit (EXIT_FAILURE);
        }
    }
    else
    {
        printf ("Unknown EFR trying to read ID on bar_index %u\n", port->bar_index);
        exit (EXIT_FAILURE);
    }
}


/*
 * FIFO support.
 */
static void serial8250_clear_fifos (uart_port_t *const port)
{
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO);
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO |
                UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
    serial_out (port, UART_FCR, 0);
}


/**
 * @brief Perform an auto-detection sequence, on which should be an OX16C950 UART.
 * @details This is a cutdown sequence from the Linux Kernel 8250_core.c, excluding tests not applicable
 *          to the expected UART.
 * @param[in] port Which UART to auto-detect
 */
static void autoconfig (uart_port_t *const port)
{
    uint8_t status1, scratch, scratch2, scratch3;
    uint8_t save_lcr, save_mcr;

    /* Do a simple existence test first, on the Interrupt Enable Register */
    scratch = serial_in (port, UART_IER);
    serial_out (port, UART_IER, 0);

    /*
     * Mask out IER[7:4] bits for test as some UARTs (e.g. TL
     * 16C754B) allow only to modify them if an EFR bit is set.
     */
    scratch2 = serial_in (port, UART_IER) & 0x0f;
    serial_out (port, UART_IER, 0x0F);

    scratch3 = serial_in (port, UART_IER) & 0x0f;
    serial_out (port, UART_IER, scratch);
    if ((scratch2 != 0) || (scratch3 != 0x0F))
    {
        printf ("IER test failed (%02x, %02x)\n", scratch2, scratch3);
        exit (EXIT_FAILURE);
    }

    save_mcr = serial_in (port, UART_MCR);
    save_lcr = serial_in (port, UART_LCR);

    /* Check to see if a UART is really there, by performing a loopback test on the modem status bits */
    serial_out (port, UART_MCR, UART_MCR_LOOP | 0x0A);
    status1 = serial_in (port, UART_MSR) & 0xF0;
    serial_out (port, UART_MCR, save_mcr);
    if (status1 != 0x90)
    {
        printf ("LOOP test failed (%02x)\n", status1);
    }

    /*
     * We're pretty sure there's a port here.  Lets find out what
     * type of port it is.  The IIR top two bits allows us to find
     * out if it's 8250 or 16450, 16550, 16550A or later.  This
     * determines what we test for next.
     *
     * We also initialise the EFR (if any) to zero for later.  The
     * EFR occupies the same register location as the FCR and IIR.
     */
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_B);
    serial_out (port, UART_EFR, 0);
    serial_out (port, UART_LCR, 0);

    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO);

    scratch = serial_in (port, UART_IIR) >> 6;
    switch (scratch)
    {
    case 0:
        printf ("IIR Unexpected ID of 8250\n");
        exit (EXIT_FAILURE);
        break;
    case 1:
        printf ("IIR Unknown ID\n");
        exit (EXIT_FAILURE);
        break;
    case 2:
        printf ("IIR Unexpected ID of 16550\n");
        exit (EXIT_FAILURE);
        break;
    case 3:
        check_16c950_id (port);
        break;
    }

    serial_out (port, UART_LCR, save_lcr);

    /*
     * Reset the UART.
     */
    serial_out (port, UART_MCR, save_mcr);
    serial8250_clear_fifos (port);
    serial_in (port, UART_RX);
    serial_out (port, UART_IER, 0);
}


/**
 * @brief Set a UART to operational mode for transmitting data.
 * @param[in] port Which port to initialise
 * @param[in] internal_loopback If true the UART is set to internal loopback.
 *                              If false set to use the external signals.
 */
static void set_uart_operational_mode (uart_port_t *const port, const bool internal_loopback)
{
    /* Enable 950 mode, with 128 deep FIFOs */
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_B);
    serial_out (port, UART_EFR, UART_EFR_ECB);
    serial_out (port, UART_LCR, 0x00);
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO);

    /* Set 8 data bits, 1 stop bit, no parity */
    port->lcr = UART_LCR_WLEN8;

    /* Set a divisor of one */
    serial_out (port, UART_LCR, port->lcr | UART_LCR_DLAB);
    serial_out (port, UART_DLL, 1);
    serial_out (port, UART_DLM, 0);
    serial_out (port, UART_LCR, port->lcr);

    /* Set the clock pre-scaler to 6.
     * With the 14.7456MHz oscillator, this results in a baud rate of 2.4576 Mbaud. */
    serial_icr_write (port, UART_TCR, 6);

    serial_out (port, UART_MCR, internal_loopback ? UART_MCR_LOOP : 0);
}


/**
 * @brief Perform a simple loopback test for a pair of UARTs.
 * @param[in] tx The UART used to transmit
 * @param[in] rx The UART used to receive
 */
static void loopback_test_fifo_depth (uart_port_t *const tx, uart_port_t *const rx)
{
    uint8_t byte_count;
    uint8_t lsr;
    bool rx_data_ok = true;
    uint8_t rx_data;
    uint8_t rx_fifo_level;
    uint8_t rx_fifo_level_sync;

    /* Queue for transmission the number of bytes of the Tx and Rx FIFOs.
     * This shouldn't result in a receive overrun.
     * Since fills the FIFO no need to check for free space. This is a precursor to using DMA. */
    for (byte_count = 0; byte_count < UART_FIFO_DEPTH; byte_count++)
    {
        serial_out (tx, UART_TX, byte_count);
    }

    /* Wait for the expected number of bytes in the Rx FIFO, as a precursor to using DMA.
     * As recommended by the 16C950 datasheet validates the level by checking read the same value twice
     * (since the UART clock is asynchronous with respect to the processor).
     *
     * @todo No receive timeout. */
    do
    {
        rx_fifo_level_sync = serial_additional_status_read (rx, UART_RFL);
        rx_fifo_level = serial_additional_status_read (rx, UART_RFL);
    } while ((rx_fifo_level != rx_fifo_level_sync) || (rx_fifo_level < UART_FIFO_DEPTH));

    /* Read the receive bytes from the FIFO */
    for (byte_count = 0; byte_count < UART_FIFO_DEPTH; byte_count++)
    {
        lsr = serial_in (rx, UART_LSR);
        rx_data = serial_in (rx, UART_RX);
        if ((lsr & UART_LSR_BRK_ERROR_BITS) != 0)
        {
            printf ("FAIL: lsr errors 0x%x at byte count %u\n", lsr, byte_count);
            rx_data_ok = false;
        }
        else if (rx_data != byte_count)
        {
            printf ("FAIL: At byte count %u got %u\n", byte_count, rx_data);
            rx_data_ok = false;
        }
    }

    if (rx_data_ok)
    {
        printf ("Sent %u bytes from UART at bar_index %u -> bar_index %u\n", UART_FIFO_DEPTH, tx->bar_index, rx->bar_index);
    }
}


/**
 * @brief Sequence the UART tests, using VFIO
 * @param[in/out] vfio_device The open VFIO device in which the UARTs are mapped
 */
static void perform_uart_tests (vfio_device_t *const vfio_device)
{
    uart_port_t ports[NUM_UARTS];
    unsigned int port_index;
    bool internal_loopback;

    /* Initialise ports to access both UARTS on the board, mapping the BARs of the ports into the address space */
    memset (ports, 0, sizeof (ports));
    ports[0].bar_index = 2;
    map_vfio_device_bar_before_use (vfio_device, ports[0].bar_index);
    ports[0].bar_mapping = vfio_device->mapped_bars[ports[0].bar_index];
    ports[1].bar_index = 3;
    map_vfio_device_bar_before_use (vfio_device, ports[1].bar_index);
    ports[1].bar_mapping = vfio_device->mapped_bars[ports[1].bar_index];
    if (ports[0].bar_mapping == NULL)
    {
        printf ("BAR %d not mapped\n", ports[0].bar_index);
        exit (EXIT_FAILURE);
    }
    if (ports[1].bar_mapping == NULL)
    {
        printf ("BAR %d not mapped\n", ports[1].bar_index);
        exit (EXIT_FAILURE);
    }

    printf ("Performing tests with UART registers mapped into virtual address space using VFIO\n");

    /* Perform tests which detect the type of UART */
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        autoconfig (&ports[port_index]);
    }

    /* Perform a loopback test with loopback internal to the UARTs */
    internal_loopback = true;
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        set_uart_operational_mode (&ports[port_index], internal_loopback);
    }
    loopback_test_fifo_depth (&ports[0], &ports[0]);
    loopback_test_fifo_depth (&ports[1], &ports[1]);
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;

    /* The device ID for a SIO4 board, which is what the identity of the Sealevel COMM+2.LPCIe board (7205e)
     * has been changed to as described in
     * https://github.com/Chester-Gillon/plx_poll_mode_driver/blob/master/plx_poll_mode_driver/sealevel_pex8311_addressing.txt */
    const vfio_pci_device_filter_t filter =
    {
        .vendor_id = 0x10b5,
        .device_id = 0x9056,
        .subsystem_vendor_id = 0x10b5,
        .subsystem_device_id = 0x3198,
        .enable_bus_master = false
    };

    /* Open the Sealevel devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* Process any Sealevel devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        perform_uart_tests (&vfio_devices.devices[device_index]);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
