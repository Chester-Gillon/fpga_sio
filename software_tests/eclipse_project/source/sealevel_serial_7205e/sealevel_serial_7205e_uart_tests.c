/*
 * @file sealevel_serial_7205e_uart_tests.c
 * @date 19 Feb 2023
 * @author Chester Gillon
 * @brief Perform internal UART loopback tests on a Sealevel COMM+2.LPCIe board (7205e), using VFIO
 * @details This is a version of https://github.com/Chester-Gillon/plx_poll_mode_driver/blob/master/plx_poll_mode_driver/plx_poll_mode_driver.c
 *          which uses VFIO to access the device, rather than a Plx Kernel module and user space library.
 *
 *          Used the following as references:
 *          - https://www.sealevel.com/wp-content/uploads/2016/05/7205e-User-Manual.pdf as the user manual for the 7205e card
 *          - https://www.fastcomproducts.com/data_sheets/OX16C950B_DS.pdf as the datasheet for the OX16C950B UART on the 7205e
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <time.h>
#include <unistd.h>

#include "vfio_access.h"
#include "serial_reg.h"
#include "transfer_timing.h"


/* The number of 16C950 UARTs on the Sealevel COMM+2.LPCIe board (7205e) */
#define NUM_UARTS 2

/* For a 16C950 */
#define UART_FIFO_DEPTH 128

/* The maximum number of blocks of bytes which can be written to the transmit FIFO,
 * waiting to be looped back into the receive FIFO.
 * Set to allow overlapped transmission and reception. */
#define MAX_QUEUED_BLOCKS 2

/* The number of written to the transmit FIFO / read from the receive FIFO together */
#define UART_BLOCK_SIZE_BYTES (UART_FIFO_DEPTH / MAX_QUEUED_BLOCKS)

/* Test duration in number of blocks */
#define TEST_DURATION_BLOCKS 16384

/* Test duration in number of total bytes sent and received */
#define TEST_DURATION_BYTES (TEST_DURATION_BLOCKS * UART_BLOCK_SIZE_BYTES)
#define TEST_DURATION_WORDS (TEST_DURATION_BYTES / sizeof (uint32_t))


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
    /* While waiting for the receive FIFO to fill with the contents of the next receive block used to detect changes in
     * the receive FIFO level. */
    uint8_t previous_rx_fifo_level;
    /* While waiting for the receive FIFO to fill with the contents of the next receive block used to record the range
     * of changes seen. The OX16C950B datasheet contains the following:
     *   "As the UART clock is asynchronous with respect to the processor, it is possible for the levels to
     *    change during a read of these FIFO levels. It is therefore recommended that the levels are read twice and compared
     *    to check that the values obtained are valid."
     *
     * This program doesn't validate the receive FIFO by waiting until reads two values the same, but instead collects
     * statistics to indicate if the receive FIFO level appears to go "backwards" unexpectedly.
     */
    int32_t rx_fifo_level_change_min;
    int32_t rx_fifo_level_change_max;
} uart_port_t;


/* Used to track the state of performing a UART test */
typedef enum
{
    /* Test is currently running */
    UART_TEST_RUNNING,
    /* Test has failed with an error */
    UART_TEST_FAILED,
    /* Test has completed successfully */
    UART_TEST_COMPLETE
} uart_test_state_t;

/* Structure which contains the context used for a UART test */
typedef struct
{
    /* The UART ports used for the test */
    uart_port_t *tx_port;
    uart_port_t *rx_port;
    /* Current state of the test */
    uart_test_state_t test_state;
    /* Absolute time at which the test times-out for the current block */
    struct timespec block_timeout;
    /* Used to advance the expected receive test pattern */
    uint32_t rx_test_pattern;
    /* The number of blocks which have been queued for transmission.
     * To prevent an overrun during the test the value of (num_tx_blocks - num_rx_blocks) isn't allowed to exceed MAX_QUEUED_BLOCKS */
    uint32_t num_tx_blocks;
    /* The number of blocks which have been received */
    uint32_t num_rx_blocks;
    /* Array of size TEST_DURATION_BYTES used to populate the test pattern transmitted for the test */
    uint8_t *tx_buffer;
    uint64_t tx_buffer_iova;
    /* Array of size TEST_DURATION_BYTES used to receive the test pattern for the test */
    uint8_t *rx_buffer;
    uint64_t rx_buffer_iova;
    /* Array of size UART_BLOCK_SIZE_BYTES used to store and check the line status register for current receive block */
    uint8_t *rx_lsr_block;
    uint64_t rx_lsr_block_iova;
} uart_test_context_t;


/* The timeout used for the test. Made a global so may be changed if single stepping in the debugger */
static int test_timeout_secs = 1;


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
 * @brief Enable or disable the Additional Status Read from a 16C950 UART
 * @details When Additional Status Read is enabled:
 *          - The MCR and LCR registers are no longer readable but remain writable, and the TFL and RFL
 *            registers replace them in the memory map for read operations.
 *          - The IER register is replaced by the ASR register for all operations.
 * @param[in/out] port Which UART to set the ASR enable state for
 * @param[in] enable When true ASR is to be enabled
 */
static void serial_set_additional_status_read (uart_port_t *const port, const bool enable)
{
    serial_icr_write (port, UART_ACR, enable ? port->acr | UART_ACR_ASE : port->acr);
}


/**
 * @brief Enable or disable Internal Loopback for a UART
 * @param[in/out] port Which UART to set the Internal Loopback state for
 * @param[in] enable When true Internal Loopback is to be enabled
 */
static void serial_set_internal_loopback (uart_port_t *const port, const bool enable)
{
    serial_out (port, UART_MCR, enable ? UART_MCR_LOOP : 0);
}


/**
 * @brief Read the current receive FIFO level for a UART, updating statistics on the amount of change in the level
 * @details Assumes serial_set_additional_status_read() has been called for port to enable ASR.
 * @param[in/out] port The UART port to read the receive FIFO level for
 * @return The current receive FIFO level
 */
static uint8_t serial_read_rx_fifo_level (uart_port_t *const port)
{
    const uint8_t rx_fifo_level = serial_in (port, UART_RFL);
    const int32_t rx_fifo_level_change = (int32_t) rx_fifo_level - port->previous_rx_fifo_level;

    if (rx_fifo_level_change < port->rx_fifo_level_change_min)
    {
        port->rx_fifo_level_change_min = rx_fifo_level_change;
    }
    if (rx_fifo_level_change > port->rx_fifo_level_change_max)
    {
        port->rx_fifo_level_change_max = rx_fifo_level_change;
    }
    port->previous_rx_fifo_level = rx_fifo_level;

    return rx_fifo_level;
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
 */
static void set_uart_operational_mode (uart_port_t *const port)
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

    /* For the tests enable the Additional Status Read, to allow reading of the Rx FIFO level with a single register read */
    const bool enable_asr = true;
    serial_set_additional_status_read (port, enable_asr);

    /* Initialise tracking of changes to the Rx FIFO level */
    port->previous_rx_fifo_level = serial_in (port, UART_RFL);
    port->rx_fifo_level_change_min = INT32_MAX;
    port->rx_fifo_level_change_max = INT32_MIN;
}


/**
 * @brief Reset the timeout for a UART test when progress is made
 * @param[in/out] context The test context to reset the timeout for
 */
static void test_timeout_reset (uart_test_context_t *const context)
{
    clock_gettime (CLOCK_MONOTONIC, &context->block_timeout);
    context->block_timeout.tv_sec += test_timeout_secs;
}


/**
 * @brief Check for a timeout during a UART test
 * @details If a timeout has occurred report an error message, and change the test state to indicate a failure
 * @param[in/out] check_for_test_timeout The test context to check if has timed out
 * @param[in] description Describes what has timed out, for reporting in an error message
 */
static void check_for_test_timeout (uart_test_context_t *const context, const char *const description)
{
    struct timespec now;

    clock_gettime (CLOCK_MONOTONIC, &now);
    if ((now.tv_sec > context->block_timeout.tv_sec) ||
            ((now.tv_sec == context->block_timeout.tv_sec) && (now.tv_nsec > context->block_timeout.tv_nsec)))
    {
        printf ("FAIL: Timeout waiting for %s : tx BAR=%d rx BAR=%d num_tx_blocks=%u num_rx_block=%u rx_fifo_level=%u\n",
                description,
                context->tx_port->bar_index, context->rx_port->bar_index,
                context->num_tx_blocks, context->num_rx_blocks,
                serial_in (context->rx_port, UART_RFL));
        context->test_state = UART_TEST_FAILED;
    }
}


/**
 * @brief Reset the context for one UART to the start of the next test
 * @param[in/out] context The context to reset
 * @param[in/out] seed The seed used to populate the transmit test pattern
 */
static void test_context_reset (uart_test_context_t *const context, uint32_t *const seed)
{
    uint32_t *const tx_words = (uint32_t *) context->tx_buffer;

    context->test_state = UART_TEST_RUNNING;
    context->num_tx_blocks = 0u;
    context->num_rx_blocks = 0u;
    context->rx_test_pattern = *seed;

    memset (context->rx_buffer, 0, TEST_DURATION_BYTES);
    for (uint32_t word_index = 0; word_index < TEST_DURATION_WORDS; word_index++)
    {
        tx_words[word_index] = *seed;
        linear_congruential_generator (seed);
    }

    test_timeout_reset (context);
}


/**
 * @brief Called during a UART test following receipt of the next block, to check for any receive errors in the block.
 * @details Checks the UART line status register for errors reported by the UART, the actual receive byte are checked
 *          once the entire test pattern has been transmitted and received.
 *          Also determine when the transmit and reception for the test is complete.
 * @params[in/out] context The UART test context
 */
static void check_rx_block_uart_errors (uart_test_context_t *const context)
{
    for (uint32_t block_index = 0; (context->test_state == UART_TEST_RUNNING) && (block_index < UART_BLOCK_SIZE_BYTES); block_index++)
    {
        const uint32_t byte_count = (context->num_rx_blocks * UART_BLOCK_SIZE_BYTES) + block_index;
        const uint8_t lsr = context->rx_lsr_block[block_index];

        if ((lsr & UART_LSR_DR) == 0)
        {
            printf ("FAIL: BAR %d lsr 0x%x doesn't indicate data ready at byte count %u\n",
                    context->rx_port->bar_index, lsr, byte_count);
            context->test_state = UART_TEST_FAILED;
        }
        else if ((lsr & UART_LSR_BRK_ERROR_BITS) != 0)
        {
            printf ("FAIL: BAR %d lsr errors 0x%x at byte count %u\n",
                    context->rx_port->bar_index, lsr, byte_count);
            context->test_state = UART_TEST_FAILED;
        }
    }

    if (context->test_state == UART_TEST_RUNNING)
    {
        context->rx_port->previous_rx_fifo_level -= UART_BLOCK_SIZE_BYTES;

        context->num_rx_blocks++;
        if (context->num_rx_blocks == TEST_DURATION_BLOCKS)
        {
            context->test_state = UART_TEST_COMPLETE;
        }
    }
}


/**
 * @brief Sequence running the UART loopback test for one context when using PIO to transmit/receive
 * @details This updates the test context, transmitting and receiving as required until either the test has completed or failed.
 *          Attempts to overlap transmission with receipt to maximise the overall test throughput.
 * @params[in/out] context The UART test context
 */
static void sequence_uart_loopback_test_pio (uart_test_context_t *const context)
{
    uint32_t block_index;

    /* When not all blocks have been transmitted, and the receive FIFO won't overrun, transmit the next block of bytes */
    while ((context->num_tx_blocks < TEST_DURATION_BLOCKS) && ((context->num_tx_blocks - context->num_rx_blocks) < MAX_QUEUED_BLOCKS))
    {
        const uint8_t *const tx_block_bytes = &context->tx_buffer[context->num_tx_blocks * UART_BLOCK_SIZE_BYTES];

        for (block_index = 0; block_index < UART_BLOCK_SIZE_BYTES; block_index++)
        {
            serial_out (context->tx_port, UART_TX, tx_block_bytes[block_index]);
        }
        context->num_tx_blocks++;
    }

    /* Check for receive from the UART. This can either:
     * - Fail the test.
     * - Determine when the test has completed.
     */
    if (context->num_tx_blocks > context->num_rx_blocks)
    {
        const uint8_t rx_fifo_level = serial_read_rx_fifo_level (context->rx_port);

        if (rx_fifo_level >= UART_BLOCK_SIZE_BYTES)
        {
            uint8_t *const rx_block_bytes = &context->rx_buffer[context->num_rx_blocks * UART_BLOCK_SIZE_BYTES];

            for (block_index = 0; block_index < UART_BLOCK_SIZE_BYTES; block_index++)
            {
                context->rx_lsr_block[block_index] = serial_in (context->rx_port, UART_LSR);
                rx_block_bytes[block_index] = serial_in (context->rx_port, UART_RX);
            }

            check_rx_block_uart_errors (context);
            test_timeout_reset (context);
        }
        else
        {
            check_for_test_timeout (context, "waiting for Rx block using PIO");
        }
    }
}


/**
 * @brief Perform a UART loopback test
 * @param[in/out] contexts The UART contexts to perform the test
 * @param[in/out] seed The seed used to generate and check transmit test pattern
 * @param[in] internal_loopback If true internal loopback is being used, or external loopback if false.
 *                              Used to describe the test. The caller has already configure the UARTs.
 */
static void perform_uart_loopback_test (uart_test_context_t contexts[const NUM_UARTS],
                                        uint32_t *const seed, const bool internal_loopback)
{
    unsigned int context_index;
    bool test_running;
    transfer_timing_t timing;
    char description[PATH_MAX];

    /* Initialise for test, which creates the complete transmit test pattern */
    for (context_index = 0; context_index < NUM_UARTS; context_index++)
    {
        test_context_reset (&contexts[context_index], seed);
    }
    snprintf (description, sizeof (description), "%d UART loopback with %s loopback",
            NUM_UARTS, internal_loopback ? "internal" : "external");
    initialise_transfer_timing (&timing, description, TEST_DURATION_BYTES);

    /* Run the test until all UARTs complete the test or fail */
    transfer_time_start (&timing);
    do
    {
        test_running = false;
        for (context_index = 0; context_index < NUM_UARTS; context_index++)
        {
            uart_test_context_t *const context = &contexts[context_index];

            if (context->test_state == UART_TEST_RUNNING)
            {
                sequence_uart_loopback_test_pio (context);
                if (context->test_state == UART_TEST_RUNNING)
                {
                    test_running = true;
                }
            }
        }
    } while (test_running);
    transfer_time_stop (&timing);

    /* Verify the contents of the received test pattern */
    uint32_t num_completed_contexts = 0;
    for (context_index = 0; context_index < NUM_UARTS; context_index++)
    {
        uart_test_context_t *const context = &contexts[context_index];
        const uint32_t *const rx_words = (const uint32_t *) context->rx_buffer;

        for (uint32_t word_index = 0; (context->test_state == UART_TEST_COMPLETE) && (word_index < TEST_DURATION_WORDS); word_index++)
        {
            if (rx_words[word_index] != context->rx_test_pattern)
            {
                printf ("FAIL: BAR %d Rx word %u actual=0x%x, expected=0x%x\n",
                        context->rx_port->bar_index, word_index, rx_words[word_index], context->rx_test_pattern);
                context->test_state = UART_TEST_FAILED;
            }
            linear_congruential_generator (&context->rx_test_pattern);
        }

        if (context->test_state == UART_TEST_COMPLETE)
        {
            num_completed_contexts++;
        }
    }

    /* If the tests were successful, display the timing statistics */
    if (num_completed_contexts == NUM_UARTS)
    {
        display_transfer_timing_statistics (&timing);
    }
}


/**
 * @brief Sequence the UART tests, using VFIO
 * @param[in/out] vfio_devices The opened VFIO devices
 * @param[in] device_index Which VFIO device containing UARTs to test
 * @param[in] test_external_loopback When true enables testing using an external loopback connection
 */
static void perform_uart_tests (vfio_devices_t *const vfio_devices, const uint32_t device_index, const bool test_external_loopback)
{
    vfio_device_t *const vfio_device = &vfio_devices->devices[device_index];
    vfio_dma_mapping_t vfio_mapping;
    uart_port_t ports[NUM_UARTS];
    uart_test_context_t contexts[NUM_UARTS];
    unsigned int port_index;
    unsigned int context_index;
    bool internal_loopback;
    uint32_t seed;

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

    /* Allocate DMA addressable space for the UART test contexts.
     * The allocated size needs to be page aligned to prevent VFIO_IOMMU_MAP_DMA failing with EPERM */
    const size_t page_size = (size_t) getpagesize ();
    const size_t per_context_iova_size =
            TEST_DURATION_BYTES    + /* tx_buffer */
            TEST_DURATION_BYTES    + /* rx_buffer */
            UART_BLOCK_SIZE_BYTES;   /* rx_lsr_block */
    const size_t required_iova_size = NUM_UARTS * per_context_iova_size;
    const size_t aligned_iova_size = ((required_iova_size + page_size - 1) / page_size) * page_size;
    allocate_vfio_dma_mapping (vfio_devices, &vfio_mapping, NUM_UARTS * aligned_iova_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);
    if (vfio_mapping.buffer.vaddr == NULL)
    {
        exit (EXIT_FAILURE);
    }
    memset (contexts, 0, sizeof (contexts));
    for (context_index = 0; context_index < NUM_UARTS; context_index++)
    {
        uart_test_context_t *const context = &contexts[context_index];

        context->tx_buffer = vfio_dma_mapping_allocate_space (&vfio_mapping, TEST_DURATION_BYTES, &context->tx_buffer_iova);
        context->rx_buffer = vfio_dma_mapping_allocate_space (&vfio_mapping, TEST_DURATION_BYTES, &context->rx_buffer_iova);
        context->rx_lsr_block = vfio_dma_mapping_allocate_space (&vfio_mapping, UART_BLOCK_SIZE_BYTES, &context->rx_lsr_block_iova);
        if ((context->tx_buffer == NULL) || (context->rx_buffer == NULL) || (context->rx_lsr_block == NULL))
        {
            exit (EXIT_FAILURE);
        }
    }

    printf ("Performing tests with UART registers mapped into virtual address space using VFIO\n");

    /* Perform tests which detect the type of UART. This exits the process if the detection fails. */
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        autoconfig (&ports[port_index]);
    }

    /* Initialise the UARTs */
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        set_uart_operational_mode (&ports[port_index]);
    }

    /* Select internal loopback for the UARTS, where the each port loops back to itself */
    internal_loopback = true;
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        uart_port_t *const port = &ports[port_index];

        serial_set_internal_loopback (port, internal_loopback);
        contexts[port_index].tx_port = port;
        contexts[port_index].rx_port = port;
    }

    /* Perform a test using internal loopback and PIO */
    seed = 1;
    printf ("Performing test using PIO and internal loopback\n");
    perform_uart_loopback_test (contexts, &seed, internal_loopback);

    if (test_external_loopback)
    {
        /* Select external loopback for the UARTS, where the each port is looped back external to the other port.
         * With the Sealevel COMM+2.LPCIe board (7205e) set to it's default switch settings to give RS-422 mode use the
         * following connections on the DB25 connector:
         * - Pin  3 (port 1 RD+) to pin 17 (port 2 TD+)
         * - Pin  1 (port 1 RD-) to pin 14 (port 2 TD-)
         * - Pin 13 (port 2 RD+) to pin  7 (port 1 TD+)
         * - Pin 11 (port 2 RD-) to pin  4 (port 1 TD-)
         */
        internal_loopback = false;
        for (port_index = 0; port_index < NUM_UARTS; port_index++)
        {
            serial_set_internal_loopback (&ports[port_index], internal_loopback);
            contexts[port_index].tx_port = &ports[port_index];
            contexts[port_index].rx_port = &ports[(port_index + 1) % NUM_UARTS];
        }

        /* Perform a test using external loopback and PIO */
        printf ("Performing test using PIO and external loopback\n");
        perform_uart_loopback_test (contexts, &seed, internal_loopback);
    }

    /* Report statistics on the Rx FIFO level changes */
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        const uart_port_t *const port = &ports[port_index];

        printf ("PORT BAR %d Rx FIFO level change min=%d max=%d\n",
                port->bar_index, port->rx_fifo_level_change_min, port->rx_fifo_level_change_max);
    }

    /* Disable ASR upon end of tests */
    const bool enable_asr = false;
    for (port_index = 0; port_index < NUM_UARTS; port_index++)
    {
        serial_set_additional_status_read (&ports[port_index], enable_asr);
    }

    free_vfio_dma_mapping (vfio_devices, &vfio_mapping);
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

    /* Any command line argument enables testing using external loopback mode */
    const bool test_external_loopback = argc > 1;

    /* Open the Sealevel devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* Process any Sealevel devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        perform_uart_tests (&vfio_devices, device_index, test_external_loopback);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
