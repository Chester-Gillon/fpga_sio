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
#include <inttypes.h>

#include <time.h>
#include <unistd.h>

#include "vfio_access.h"
#include "serial_reg.h"
#include "transfer_timing.h"
#include "pex8311.h"


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


/* The size of a PEX8311 DMA ring used for one UART. Sized sufficiently to be able to hold:
 * - One descriptor for each receive byte in a block.
 * - One descriptor for each LSR read in a block.
 * - MAX_QUEUED_BLOCKS descriptors for a transmit, allowing for each transmit byte in a block.
 * - One descriptor to prevent the ring being entirely full.
 *
 * The worst case is when the receive mode checks the LSR, which for every byte in the receive block requires
 * a separate descriptor for the read of the receive data and LSR registers.
 *
 * Command line arguments can also limit the size of each transfer in a descriptor, which influences the number
 * of descriptors used.
 */
#define TEST_DMA_RING_SIZE (((2 + MAX_QUEUED_BLOCKS) * UART_BLOCK_SIZE_BYTES) + 1)


/* Structure to access one 16C950 UART, as a 8-bit wide device on the local bus of a PEX8311.
 * Each UART is mapped as one bar in memory space. */
typedef struct
{
    /** The index of the PCI BAR to which the UART is mapped */
    uint32_t bar_index;
    /** The virtual address which is mapped to the PCI BAR to allow direct access to the UART registers */
    uint8_t *bar_mapping;
    /* Base address of the UART on the local bus, to addressing by the PEX8311 DMA */
    uint32_t local_bus_base_address;
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
    /* For diagnostics records when the rx_fifo_level_change_min was sampled */
    uint32_t rfl_change_min_num_rx_blocks;
    uint32_t rfl_change_min_value_before;
    uint32_t rfl_change_min_value_after;
    uint32_t rfl_change_num_negative;
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

/* Used to track the state of DMA when performing UART_TEST_MODE_DMA_RING.
 * This allows overlapping of DMA in progress between each test context. */
typedef enum
{
    DMA_RING_IDLE,
    DMA_RING_TX_BLOCK_STARTED,
    DMA_RING_RX_BLOCK_STARTED
} dma_ring_state_t;

/* Used to track the state of DMA when performing UART_TEST_MODE_DMA_BLOCK.
 * This allows overlapping of DMA in progress between each test context. */
typedef enum
{
    DMA_BLOCK_IDLE,
    DMA_BLOCK_TX_DATA_STARTED,
    DMA_BLOCK_LSR_STARTED,
    DMA_BLOCK_RX_DATA_STARTED
} dma_block_state_t;

/* Structure which contains the context used for a UART test */
typedef struct
{
    /* Controls how the Line Status Register (LSR) is used during the test:
     * - When true, the LSR is read before each receive byte, and checked after each block has been received.
     * - When false the LSR is not read during the test, thus reducing the number of accesses to the UART registers.
     *   If there are receive errors should be indicated by a receive timeout and/or incorrect received bytes.
     */
    bool read_lsr;
    /* Used to perform DMA for the test when UART_TEST_MODE_DMA_RING */
    pex_dma_ring_context_t ring;
    /* Used to perform DMA for the test when UART_TEST_MODE_DMA_BLOCK */
    pex_dma_block_context_t block;
    /* The UART ports used for the test */
    uart_port_t *tx_port;
    uart_port_t *rx_port;
    /* Current state of the test */
    uart_test_state_t test_state;
    /* Used to track the state of DMA for UART_TEST_MODE_DMA_RING */
    dma_ring_state_t dma_ring_state;
    /* Used to track the state of DMA for UART_TEST_MODE_DMA_BLOCK */
    dma_block_state_t dma_block_state;
    uint32_t dma_block_remaining_bytes;
    uint32_t dma_block_tx_buffer_index;
    uint32_t dma_block_rx_buffer_index;
    uint32_t dma_block_rx_lsr_block_index;
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


/* Define the mode of a UART test, in terms of how the transmit/receive is controlled */
typedef enum
{
    /* Programmed IO with the CPU reading/writing UART registers */
    UART_TEST_MODE_PIO,
    /* Ring based DMA, using scatter-gather descriptors stored in host memory */
    UART_TEST_MODE_DMA_RING,
    /* DMA block mode, which doesn't use descriptors stored in host memory */
    UART_TEST_MODE_DMA_BLOCK,

    UART_TEST_MODE_ARRAY_SIZE
} uart_test_mode_t;

static const char *const uart_test_mode_descriptions[UART_TEST_MODE_ARRAY_SIZE] =
{
    [UART_TEST_MODE_PIO] = "PIO",
    [UART_TEST_MODE_DMA_RING] = "DMA_RING",
    [UART_TEST_MODE_DMA_BLOCK] = "DMA_BLOCK"
};


/* The timeout used for the test. Made a global so may be changed if single stepping in the debugger */
static int test_timeout_secs = 1;


/* Command line argument to enable external loopback */
static bool arg_test_external_loopback;


/* Command line argument to enable dumping of PEX8311 registers for debug */
static bool arg_dump_pex_registers;


/* Command line argument to select how many UARTs are tested */
static uint32_t arg_num_uarts_tested = NUM_UARTS;


/* Command line argument to select which test modes are enabled */
static bool arg_enabled_test_modes[UART_TEST_MODE_ARRAY_SIZE];


/* Command line argument to specify an increment added to the starting IOVA,
 * for testing handling of IOVA above the 4-GB Address Boundary space in the PEX8311 DMA. */
static uint64_t arg_iova_increment;


/* Command line argument which sets the maximum size of each DMA transfer. Lowering the value requires more DMA transfers
 * to be used by a test, to measure the effect of the overhead of setting up each transfer. */
static uint32_t arg_dma_transfer_max_size = UART_FIFO_DEPTH;


/* Command line argument which when testing multiple UARTs using DMA prevent overlapping use of DMA between channels.
 * I.e. when waits for DMA on one channel to complete before DMA on the other channel can be started. */
static bool arg_no_dma_channel_overlap;


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "edu:m:i:t:o?";
    int option;
    char junk;
    uart_test_mode_t test_mode;
    bool test_modes_specified = false;
    bool mode_found;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'e':
            arg_test_external_loopback = true;
            break;

        case 'd':
            arg_dump_pex_registers = true;
            break;

        case 'u':
            if ((sscanf (optarg, "%u%c", &arg_num_uarts_tested, &junk) != 1)
                || (arg_num_uarts_tested < 1) || (arg_num_uarts_tested > NUM_UARTS))
            {
                printf ("ERROR: Invalid number of UARTs \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'm':
            mode_found = false;
            for (test_mode = 0; test_mode < UART_TEST_MODE_ARRAY_SIZE; test_mode++)
            {
                if (strcasecmp (optarg, uart_test_mode_descriptions[test_mode]) == 0)
                {
                    arg_enabled_test_modes[test_mode] = true;
                    mode_found = true;
                }
            }
            if (!mode_found)
            {
                printf ("ERROR: Invalid test mode \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            test_modes_specified = true;
            break;

        case 'i':
            if (sscanf (optarg, "%" SCNi64 "%c", &arg_iova_increment, &junk) != 1)
            {
                printf ("ERROR: Invalid IOVA increment \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 't':
            if ((sscanf (optarg, "%" SCNu32 "%c", &arg_dma_transfer_max_size, &junk) != 1)
                    || (arg_dma_transfer_max_size < 1) || (arg_dma_transfer_max_size > UART_FIFO_DEPTH))
            {
                printf ("ERROR: Invalid MAX DMA transfer size \"%s\"\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'o':
            arg_no_dma_channel_overlap = true;
            break;

        case '?':
        default:
            printf ("Usage %s [-e] [-d] [-o] [-u <num_uarts_tested>] [-m PIO|DMA_BLOCK|DMA_RING] [-i <IOVA_increment>] [-t <MAX_DMA_transfer_size_bytes>]\n", argv[0]);
            printf ("  -e performs test using external loopback, in addition to internal loopback\n");
            printf ("  -d dumps the PEX8311 Local Configuration Space registers for debugging\n");
            printf ("  -o disables overlapping DMA between different channels\n");
            printf ("  -m may be specified more than once to specify multiple test modes\n");
            printf ("     Defaults to all test modes if not specified\n");
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }

    if (arg_test_external_loopback && (arg_num_uarts_tested != NUM_UARTS))
    {
        printf ("ERROR: All UARTs must be selected for testing to use external loopback (due to use of test connection)\n");
        exit (EXIT_FAILURE);
    }

    if (!test_modes_specified)
    {
        for (test_mode = 0; test_mode < UART_TEST_MODE_ARRAY_SIZE; test_mode++)
        {
            arg_enabled_test_modes[test_mode] = true;
        }
    }
}

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

static uint8_t serial_icr_read (uart_port_t *const port, const uint8_t offset)
{
    uint8_t value;

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
 * @param[in] rx_num_blocks Used to record when in the test rx_fifo_level_change_min was changed
 * @return The current receive FIFO level
 */
static uint8_t serial_read_rx_fifo_level (uart_port_t *const port, const uint32_t rx_num_blocks)
{
    const uint8_t rx_fifo_level = serial_in (port, UART_RFL);
    const int32_t rx_fifo_level_change = (int32_t) rx_fifo_level - port->previous_rx_fifo_level;

    if (rx_fifo_level_change < 0)
    {
        port->rfl_change_num_negative++;
    }
    if (rx_fifo_level_change < port->rx_fifo_level_change_min)
    {
        port->rx_fifo_level_change_min = rx_fifo_level_change;
        port->rfl_change_min_num_rx_blocks = rx_num_blocks;
        port->rfl_change_min_value_before = port->previous_rx_fifo_level;
        port->rfl_change_min_value_after = rx_fifo_level;
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
    port->rfl_change_min_num_rx_blocks = 0;
    port->rfl_change_min_value_before = UINT32_MAX;
    port->rfl_change_min_value_after = UINT32_MAX;
    port->rfl_change_num_negative = 0;
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
        const uint8_t receive_fifo_level = serial_in (context->rx_port, UART_RFL);
        printf ("FAIL: Timeout waiting for %s : tx BAR=%d rx BAR=%d num_tx_blocks=%u num_rx_block=%u rx_fifo_level=%u\n",
                description,
                context->tx_port->bar_index, context->rx_port->bar_index,
                context->num_tx_blocks, context->num_rx_blocks,
                receive_fifo_level);
        if (receive_fifo_level == 255)
        {
            /* This issue was seen attempting to get DMA to work. Even exiting can still cause the PC to hang, possibly
             * as vfio-pci tries to close and reset the device. For the investigation see:
             * https://gist.github.com/Chester-Gillon/8b588735c2304945e873c06aeb21e706#7-advanced-error-control
             */
            printf ("PEX8311 appears to have failed, as register read of receive FIFO level returns all-ones\n");
            printf ("Exiting in case further device accesses cause the PC to hang\n");
            exit (EXIT_FAILURE);
        }
        if (arg_dump_pex_registers)
        {
            /* If the test has been using DMA dump registers for debug */
            if (context->ring.lcs != NULL)
            {
                pex_dump_lcs_registers (context->ring.lcs, "timeout");
            }
            else if (context->block.lcs != NULL)
            {
                pex_dump_lcs_registers (context->block.lcs, "timeout");
            }
        }
        context->test_state = UART_TEST_FAILED;
    }
}


/**
 * @brief Reset the context for one UART to the start of the next test
 * @param[in/out] context The context to reset
 * @param[in/out] seed The seed used to populate the transmit test pattern
 * @param[in] read_lsr Parameter to set for the test
 */
static void test_context_reset (uart_test_context_t *const context, uint32_t *const seed, const bool read_lsr)
{
    uint32_t *const tx_words = (uint32_t *) context->tx_buffer;

    context->test_state = UART_TEST_RUNNING;
    context->dma_ring_state = DMA_RING_IDLE;
    context->dma_block_state = DMA_BLOCK_IDLE;
    context->dma_block_remaining_bytes = 0;
    context->dma_block_tx_buffer_index = 0;
    context->dma_block_rx_lsr_block_index = 0;
    context->dma_block_rx_buffer_index = 0;
    context->num_tx_blocks = 0u;
    context->num_rx_blocks = 0u;
    context->rx_test_pattern = *seed;
    context->read_lsr = read_lsr;

    memset (context->rx_buffer, 0, TEST_DURATION_BYTES);
    for (uint32_t word_index = 0; word_index < TEST_DURATION_WORDS; word_index++)
    {
        tx_words[word_index] = *seed;
        linear_congruential_generator (seed);
    }

    test_timeout_reset (context);
}


/**
 * @brief Called during a UART test following receipt of the next block
 * @details Takes actions:
 *          1. If reading the LSR is enabled, check for any receive errors in the block. Rhe actual receive byte are checked
 *             once the entire test pattern has been transmitted and received.
 *
 *          2. Determine when the transmit and reception for the test is complete.
 * @params[in/out] context The UART test context
 */
static void process_rx_block (uart_test_context_t *const context)
{
    if (context->read_lsr)
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
    }

    if (context->test_state == UART_TEST_RUNNING)
    {
        context->rx_port->previous_rx_fifo_level = (uint8_t)  (context->rx_port->previous_rx_fifo_level - UART_BLOCK_SIZE_BYTES);

        context->num_rx_blocks++;
        if (context->num_rx_blocks == TEST_DURATION_BLOCKS)
        {
            context->test_state = UART_TEST_COMPLETE;
        }
    }
}


/**
 * @brief Determine the number of blocks which can be transmitted, which won't overrun the receive FIFO
 * @param[in] context The UART test context
 * @return The number of blocks to transmit
 */
static uint32_t get_num_tx_blocks_to_queue (const uart_test_context_t *const context)
{
    uint32_t new_num_tx_blocks = context->num_tx_blocks;
    uint32_t num_tx_blocks_to_queue = 0;

    while ((new_num_tx_blocks < TEST_DURATION_BLOCKS) && ((new_num_tx_blocks - context->num_rx_blocks) < MAX_QUEUED_BLOCKS))
    {
        new_num_tx_blocks++;
        num_tx_blocks_to_queue++;
    }

    return num_tx_blocks_to_queue;
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
    uint32_t num_tx_blocks_to_queue = get_num_tx_blocks_to_queue (context);
    while (num_tx_blocks_to_queue > 0)
    {
        const uint8_t *const tx_block_bytes = &context->tx_buffer[context->num_tx_blocks * UART_BLOCK_SIZE_BYTES];

        for (block_index = 0; block_index < UART_BLOCK_SIZE_BYTES; block_index++)
        {
            serial_out (context->tx_port, UART_TX, tx_block_bytes[block_index]);
        }
        context->num_tx_blocks++;
        num_tx_blocks_to_queue--;
    }

    /* Check for receive from the UART. This can either:
     * - Fail the test.
     * - Determine when the test has completed.
     */
    if (context->num_tx_blocks > context->num_rx_blocks)
    {
        const uint8_t rx_fifo_level = serial_read_rx_fifo_level (context->rx_port, context->num_rx_blocks);

        if (rx_fifo_level >= UART_BLOCK_SIZE_BYTES)
        {
            uint8_t *const rx_block_bytes = &context->rx_buffer[context->num_rx_blocks * UART_BLOCK_SIZE_BYTES];

            for (block_index = 0; block_index < UART_BLOCK_SIZE_BYTES; block_index++)
            {
                if (context->read_lsr)
                {
                    context->rx_lsr_block[block_index] = serial_in (context->rx_port, UART_LSR);
                }
                rx_block_bytes[block_index] = serial_in (context->rx_port, UART_RX);
            }

            process_rx_block (context);
            test_timeout_reset (context);
        }
        else
        {
            check_for_test_timeout (context, "waiting for Rx block using PIO");
        }
    }
}


/**
 * @brief Sequence running the UART loopback test for one context when using a DMA ring to transmit/receive
 * @details This updates the test context, transmitting and receiving as required until either the test has completed or failed.
 *          Attempts to overlap transmission with receipt to maximise the overall test throughput.
 * @params[in/out] context The UART test context
 */
static void sequence_uart_loopback_test_dma_ring (uart_test_context_t *const context)
{
    /* Since pex_update_descriptor_in_ring() doesn't allow for descriptors to be added to the ring while DMA is in progress,
     * have to wait of the DMA to complete before can take other action which can start DMA. */
    switch (context->dma_ring_state)
    {
    case DMA_RING_IDLE:
        {
            /* Queue blocks for transmission by DMA. The size of each transfer, and therefore the number of transfers used,
             * depends upon the command line argument which limits the size of each transfer. */
            const uint32_t num_tx_blocks_to_queue = get_num_tx_blocks_to_queue (context);
            if (num_tx_blocks_to_queue > 0)
            {
                uint32_t remaining_bytes = num_tx_blocks_to_queue * UART_BLOCK_SIZE_BYTES;
                uint32_t num_bytes_queued = context->num_tx_blocks * UART_BLOCK_SIZE_BYTES;

                while (remaining_bytes > 0)
                {
                    const uint32_t transfer_size_bytes =
                            remaining_bytes < arg_dma_transfer_max_size ? remaining_bytes : arg_dma_transfer_max_size;

                    pex_update_descriptor_in_ring (&context->ring, transfer_size_bytes,
                            (uint32_t) context->tx_buffer_iova + num_bytes_queued,
                            context->tx_port->local_bus_base_address + UART_TX, PEX_LCS_DMADPRx_DIRECTION_PCI_TO_LOCAL);
                    remaining_bytes -= transfer_size_bytes;
                    num_bytes_queued += transfer_size_bytes;
                }

                context->num_tx_blocks += num_tx_blocks_to_queue;
                pex_start_dma_ring (&context->ring);
                context->dma_ring_state = DMA_RING_TX_BLOCK_STARTED;
            }

            /* When DMA is idle, queue receive DMA when a block is available in the UART receive FIFO */
            if ((context->dma_ring_state == DMA_RING_IDLE) && (context->num_tx_blocks > context->num_rx_blocks))
            {
                const uint8_t rx_fifo_level = serial_read_rx_fifo_level (context->rx_port, context->num_rx_blocks);

                if (rx_fifo_level >= UART_BLOCK_SIZE_BYTES)
                {
                    uint32_t num_bytes_queued = context->num_rx_blocks * UART_BLOCK_SIZE_BYTES;
                    if (context->read_lsr)
                    {
                        /* When reading LSR have to queue single byte transfers for the block, which alternate between
                         * reading the UART LSR and RX registers */
                        memset (context->rx_lsr_block, UART_LSR_BRK_ERROR_BITS, UART_BLOCK_SIZE_BYTES);
                        for (uint32_t block_index = 0; block_index < UART_BLOCK_SIZE_BYTES; block_index++)
                        {
                            pex_update_descriptor_in_ring (&context->ring, 1, (uint32_t) context->rx_lsr_block_iova + block_index,
                                    context->rx_port->local_bus_base_address + UART_LSR, PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI);
                            pex_update_descriptor_in_ring (&context->ring, 1, (uint32_t) context->rx_buffer_iova + num_bytes_queued,
                                    context->rx_port->local_bus_base_address + UART_RX, PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI);
                            num_bytes_queued++;
                        }
                    }
                    else
                    {
                        /* When not reading LSR, can queue transfers only to transfer from the UART RX register to the Rx buffer.
                         * The size of each transfer, and therefore the number of transfers used, depends upon the
                         * command line argument which limits the size of each transfer. */
                        uint32_t remaining_bytes = UART_BLOCK_SIZE_BYTES;

                        while (remaining_bytes > 0)
                        {
                            const uint32_t transfer_size_bytes =
                                    remaining_bytes < arg_dma_transfer_max_size ? remaining_bytes : arg_dma_transfer_max_size;

                            pex_update_descriptor_in_ring (&context->ring, transfer_size_bytes,
                                    (uint32_t) context->rx_buffer_iova + num_bytes_queued,
                                    context->rx_port->local_bus_base_address + UART_RX, PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI);
                            remaining_bytes -= transfer_size_bytes;
                            num_bytes_queued += transfer_size_bytes;
                        }
                    }

                    pex_start_dma_ring (&context->ring);
                    context->dma_ring_state = DMA_RING_RX_BLOCK_STARTED;
                }
                else
                {
                    check_for_test_timeout (context, "waiting for Rx block using DMA ring");
                }
            }
        }
        break;

    case DMA_RING_TX_BLOCK_STARTED:
        if (pex_poll_dma_ring_completion (&context->ring))
        {
            /* When Tx DMA has completed can allow further DMA operations */
            test_timeout_reset (context);
            context->dma_ring_state = DMA_RING_IDLE;
        }
        else
        {
            check_for_test_timeout (context, "DMA ring Tx completion");
        }
        break;

    case DMA_RING_RX_BLOCK_STARTED:
        if (pex_poll_dma_ring_completion (&context->ring))
        {
            /* When Rx DMA has completed process the received block. This can either:
             * - Fail the test.
             * - Determine when the test has completed.
             */
            process_rx_block (context);
            test_timeout_reset (context);
            context->dma_ring_state = DMA_RING_IDLE;
        }
        else
        {
            check_for_test_timeout (context, "DMA ring Rx completion");
        }
    }
}


/**
 * @brief When using block DMA to test the UART, start the next DMA transfer
 * @details This also uses the dma_block_remaining_bytes and dma_block_*_index variables to track how far through the
 *          current UART_BLOCK_SIZE_BYTES have got.
 * @params[in/out] context The UART test context
 */
static void sequence_dma_block_transfers (uart_test_context_t *const context)
{
    switch (context->dma_block_state)
    {
    case DMA_BLOCK_TX_DATA_STARTED:
        {
            /* Start the next DMA transfer for Tx data. The size of each transfer, and therefore the number of transfers
             * used, depends upon the command line argument which limits the size of each transfer. */
            const uint32_t transfer_size_bytes =
                    context->dma_block_remaining_bytes < arg_dma_transfer_max_size ?
                            context->dma_block_remaining_bytes : arg_dma_transfer_max_size;

            pex_start_dma_block (&context->block, transfer_size_bytes,
                    context->tx_buffer_iova + context->dma_block_tx_buffer_index,
                    context->tx_port->local_bus_base_address + UART_TX, PEX_LCS_DMADPRx_DIRECTION_PCI_TO_LOCAL);
            context->dma_block_remaining_bytes -= transfer_size_bytes;
            context->dma_block_tx_buffer_index += transfer_size_bytes;
        }
        break;

    case DMA_BLOCK_LSR_STARTED:
        {
            /* Start the next DMA transfer for one byte from the LSR register.
             * Value of dma_block_remaining_bytes is left unchanged, as used for following DMA_BLOCK_RX_DATA_STARTED state */
            const uint32_t transfer_size_bytes = 1;

            pex_start_dma_block (&context->block, transfer_size_bytes, context->rx_lsr_block_iova + context->dma_block_rx_lsr_block_index,
                    context->rx_port->local_bus_base_address + UART_LSR, PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI);
            context->dma_block_rx_lsr_block_index += transfer_size_bytes;
        }
        break;

    case DMA_BLOCK_RX_DATA_STARTED:
        {
            /* Start the next DMA transfer for Rx data. When read_lsr is enabled has to transfer a single byte,
             * otherwise use the command line argument which limits the size of each transfer. */
            const uint32_t max_transfer_size = context->read_lsr ? 1 : arg_dma_transfer_max_size;
            const uint32_t transfer_size_bytes =
                    context->dma_block_remaining_bytes < max_transfer_size ? context->dma_block_remaining_bytes : max_transfer_size;

            pex_start_dma_block (&context->block, transfer_size_bytes,
                    context->rx_buffer_iova + context->dma_block_rx_buffer_index,
                    context->rx_port->local_bus_base_address + UART_RX, PEX_LCS_DMADPRx_DIRECTION_LOCAL_TO_PCI);
            context->dma_block_remaining_bytes -= transfer_size_bytes;
            context->dma_block_rx_buffer_index += transfer_size_bytes;
        }
        break;

    default:
        printf ("Unexpected state\n");
        exit (EXIT_FAILURE);
        break;
    }
}


/**
 * @brief Sequence running the UART loopback test for one context when using DMA block mode to transmit/receive
 * @details This updates the test context, transmitting and receiving as required until either the test has completed or failed.
 *          Attempts to overlap transmission with receipt to maximise the overall test throughput.
 * @params[in/out] context The UART test context
 */
static void sequence_uart_loopback_test_dma_block (uart_test_context_t *const context)
{
    /* As block mode DMA only allows one transfer to be in progress on a DMA channel at once, have to wait for the DMA to
     * complete before can take any other action which starts DMA. */
    switch (context->dma_block_state)
    {
    case DMA_BLOCK_IDLE:
        {
            /* Starting queueing blocks for transmission by DMA */
            const uint32_t num_tx_blocks_to_queue = get_num_tx_blocks_to_queue (context);
            if (num_tx_blocks_to_queue > 0)
            {
                context->dma_block_remaining_bytes = num_tx_blocks_to_queue * UART_BLOCK_SIZE_BYTES;
                context->dma_block_state = DMA_BLOCK_TX_DATA_STARTED;
                context->num_tx_blocks += num_tx_blocks_to_queue;
                sequence_dma_block_transfers (context);
            }

            /* When DMA is idle, queue receive DMA when a block is available in the UART receive FIFO */
            if ((context->dma_block_state == DMA_BLOCK_IDLE) && (context->num_tx_blocks > context->num_rx_blocks))
            {
                const uint8_t rx_fifo_level = serial_read_rx_fifo_level (context->rx_port, context->num_rx_blocks);

                if (rx_fifo_level >= UART_BLOCK_SIZE_BYTES)
                {
                    context->dma_block_remaining_bytes = UART_BLOCK_SIZE_BYTES;
                    if (context->read_lsr)
                    {
                        context->dma_block_state = DMA_BLOCK_LSR_STARTED;
                        memset (context->rx_lsr_block, UART_LSR_BRK_ERROR_BITS, UART_BLOCK_SIZE_BYTES);
                        context->dma_block_rx_lsr_block_index = 0;
                    }
                    else
                    {
                        context->dma_block_state = DMA_BLOCK_RX_DATA_STARTED;
                    }
                    sequence_dma_block_transfers (context);
                }
                else
                {
                    check_for_test_timeout (context, "waiting for Rx block using DMA block");
                }
            }
        }
        break;

    case DMA_BLOCK_TX_DATA_STARTED:
        /* Poll for a DMA transfer of Tx data completing. When no more bytes remaining can change state */
        if (pex_poll_dma_block_completion (&context->block))
        {
            test_timeout_reset (context);
            if (context->dma_block_remaining_bytes > 0)
            {
                sequence_dma_block_transfers (context);
            }
            else
            {
                context->dma_block_state = DMA_BLOCK_IDLE;
            }
        }
        else
        {
            check_for_test_timeout (context, "DMA completion Tx data");
        }
        break;

    case DMA_BLOCK_LSR_STARTED:
        /* Poll for a DMA transfer of LSR completing. When completed starts the transfer for the corresponding Rx data byte. */
        if (pex_poll_dma_block_completion (&context->block))
        {
            test_timeout_reset (context);
            context->dma_block_state = DMA_BLOCK_RX_DATA_STARTED;
            sequence_dma_block_transfers (context);
        }
        else
        {
            check_for_test_timeout (context, "DMA completion LSR");
        }
        break;

    case DMA_BLOCK_RX_DATA_STARTED:
        if (pex_poll_dma_block_completion (&context->block))
        {
            test_timeout_reset (context);
            if (context->dma_block_remaining_bytes > 0)
            {
                if (context->read_lsr)
                {
                    context->dma_block_state = DMA_BLOCK_LSR_STARTED;
                }
                sequence_dma_block_transfers (context);
            }
            else
            {
                /* When Rx DMA has completed process the received block. This can either:
                 * - Fail the test.
                 * - Determine when the test has completed.
                 */
                process_rx_block (context);
                context->dma_block_state = DMA_BLOCK_IDLE;
            }
        }
        else
        {
            check_for_test_timeout (context, "DMA completion Rx data");
        }
        break;
    }
}


/**
 * @brief Perform a UART loopback test
 * @param[in/out] contexts The UART contexts to perform the test
 * @param[in] test_mode How the test is performed
 * @param[in/out] seed The seed used to generate and check transmit test pattern
 * @param[in] read_lsr Used to control if the Line Status Register is read and checked as part of the test.
 * @param[in] internal_loopback If true internal loopback is being used, or external loopback if false.
 *                              Used to describe the test. The caller has already configure the UARTs.
 * @param[in/out] total_tests Incremented for every call.
 * @param[in/out] total_test_failures Incremented if the test fails
 */
static void perform_uart_loopback_test (uart_test_context_t contexts[const NUM_UARTS],
                                        uart_test_mode_t test_mode,
                                        uint32_t *const seed,
                                        const bool read_lsr, const bool internal_loopback,
                                        uint32_t *const total_tests,
                                        uint32_t *const total_test_failures)
{
    unsigned int context_index;
    bool test_running;
    transfer_timing_t timing;
    char description[PATH_MAX];

    /* Initialise for test, which creates the complete transmit test pattern */
    for (context_index = 0; context_index < arg_num_uarts_tested; context_index++)
    {
        test_context_reset (&contexts[context_index], seed, read_lsr);
    }
    snprintf (description, sizeof (description), "%d UARTs, mode %s, %s LSR, using %s loopback",
            arg_num_uarts_tested,
            uart_test_mode_descriptions[test_mode],
            read_lsr ? "reading" : "ignoring",
            internal_loopback ? "internal" : "external");
    initialise_transfer_timing (&timing, description, TEST_DURATION_BYTES);
    printf ("\nTesting %s ...\n", description);

    /* Run the test until all UARTs complete the test or fail */
    transfer_time_start (&timing);
    do
    {
        test_running = false;
        for (context_index = 0; context_index < arg_num_uarts_tested; context_index++)
        {
            uart_test_context_t *const context = &contexts[context_index];

            if (context->test_state == UART_TEST_RUNNING)
            {
                switch (test_mode)
                {
                case UART_TEST_MODE_PIO:
                    sequence_uart_loopback_test_pio (context);
                    break;

                case UART_TEST_MODE_DMA_RING:
                    if (arg_no_dma_channel_overlap)
                    {
                        do
                        {
                            sequence_uart_loopback_test_dma_ring (context);
                        } while ((context->test_state == UART_TEST_RUNNING) && (context->dma_ring_state != DMA_RING_IDLE));
                    }
                    else
                    {
                        sequence_uart_loopback_test_dma_ring (context);
                    }
                    break;

                case UART_TEST_MODE_DMA_BLOCK:
                    if (arg_no_dma_channel_overlap)
                    {
                        do
                        {
                            sequence_uart_loopback_test_dma_block (context);
                        } while ((context->test_state == UART_TEST_RUNNING) && (context->dma_block_state != DMA_BLOCK_IDLE));
                    }
                    else
                    {
                        sequence_uart_loopback_test_dma_block (context);
                    }
                    break;

                default:
                    printf ("Unexpected test mode\n");
                    exit (EXIT_FAILURE);
                    break;
                }
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
    for (context_index = 0; context_index < arg_num_uarts_tested; context_index++)
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
    if (num_completed_contexts == arg_num_uarts_tested)
    {
        display_transfer_timing_statistics (&timing);
    }
    else
    {
        (*total_test_failures)++;
    }
    (*total_tests)++;
}


/**
 * @brief Sequence the UART tests, using VFIO
 * @param[in/out] vfio_devices The opened VFIO devices
 * @param[in] device_index Which VFIO device containing UARTs to test
 */
static void perform_uart_tests (vfio_devices_t *const vfio_devices, const uint32_t device_index)
{
    vfio_device_t *const vfio_device = &vfio_devices->devices[device_index];
    vfio_dma_mapping_t vfio_mapping;
    uart_port_t ports[NUM_UARTS];
    uart_test_context_t contexts[NUM_UARTS];
    unsigned int port_index;
    unsigned int context_index;
    bool internal_loopback;
    uint32_t seed;
    bool read_lsr;
    uint32_t total_tests = 0;
    uint32_t total_test_failures = 0;

    map_vfio_device_bar_before_use (vfio_device, PEX_LCS_MMIO_BAR_INDEX);
    uint8_t *const lcs = vfio_device->mapped_bars[PEX_LCS_MMIO_BAR_INDEX];
    if (lcs == NULL)
    {
        printf ("BAR %d not mapped\n", PEX_LCS_MMIO_BAR_INDEX);
    }

    if (arg_dump_pex_registers)
    {
        pex_dump_lcs_registers (lcs, "initial");
    }

    /* Initialise ports to access both UARTS on the board, mapping the BARs of the ports into the address space */
    memset (ports, 0, sizeof (ports));
    ports[0].bar_index = PEX_LOCAL_SPACE0_BAR_INDEX;
    map_vfio_device_bar_before_use (vfio_device, ports[0].bar_index);
    ports[0].bar_mapping = vfio_device->mapped_bars[ports[0].bar_index];
    ports[1].bar_index = PEX_LOCAL_SPACE1_BAR_INDEX;
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

    /* Obtain the local bus base addresses for the UARTs */
    ports[0].local_bus_base_address = read_reg32 (lcs, PEX_LCS_LAS0BA) & PEX_LCS_LASxBA_ADDR_MASK;
    ports[1].local_bus_base_address = read_reg32 (lcs, PEX_LCS_LAS1BA) & PEX_LCS_LASxBA_ADDR_MASK;

    /* Allocate DMA addressable space for the UART test contexts, including DMA descriptors.
     * The allocated size needs to be page aligned to prevent VFIO_IOMMU_MAP_DMA failing with EPERM */
    const size_t page_size = (size_t) getpagesize ();
    const size_t per_context_iova_size =
            vfio_align_cache_line_size (TEST_DURATION_BYTES)    + /* tx_buffer */
            vfio_align_cache_line_size (TEST_DURATION_BYTES)    + /* rx_buffer */
            vfio_align_cache_line_size (UART_BLOCK_SIZE_BYTES)  + /* rx_lsr_block */
            vfio_align_cache_line_size ((TEST_DMA_RING_SIZE * sizeof (pex_ring_dma_descriptor_short_format_t))); /* DMA descriptors */
    const size_t required_iova_size = arg_num_uarts_tested * per_context_iova_size;
    const size_t aligned_iova_size = ((required_iova_size + page_size - 1) / page_size) * page_size;
    allocate_vfio_dma_mapping (vfio_devices, &vfio_mapping, aligned_iova_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);
    if (vfio_mapping.buffer.vaddr == NULL)
    {
        exit (EXIT_FAILURE);
    }
    printf ("vfio_mapping iova=0x%" PRIx64 " size=0x%zx\n", vfio_mapping.iova, vfio_mapping.buffer.size);
    memset (contexts, 0, sizeof (contexts));
    for (context_index = 0; context_index < arg_num_uarts_tested; context_index++)
    {
        uart_test_context_t *const context = &contexts[context_index];

        context->tx_buffer = vfio_dma_mapping_allocate_space (&vfio_mapping, TEST_DURATION_BYTES, &context->tx_buffer_iova);
        vfio_dma_mapping_align_space (&vfio_mapping);
        context->rx_buffer = vfio_dma_mapping_allocate_space (&vfio_mapping, TEST_DURATION_BYTES, &context->rx_buffer_iova);
        vfio_dma_mapping_align_space (&vfio_mapping);
        context->rx_lsr_block = vfio_dma_mapping_allocate_space (&vfio_mapping, UART_BLOCK_SIZE_BYTES, &context->rx_lsr_block_iova);
        if ((context->tx_buffer == NULL) || (context->rx_buffer == NULL) || (context->rx_lsr_block == NULL))
        {
            exit (EXIT_FAILURE);
        }
    }

    printf ("Performing tests with UART registers mapped into virtual address space using VFIO\n");

    /* Perform tests which detect the type of UART. This exits the process if the detection fails. */
    for (port_index = 0; port_index < arg_num_uarts_tested; port_index++)
    {
        autoconfig (&ports[port_index]);
    }

    /* Initialise the UARTs */
    for (port_index = 0; port_index < arg_num_uarts_tested; port_index++)
    {
        set_uart_operational_mode (&ports[port_index]);
    }


    if (arg_dump_pex_registers)
    {
        pex_dump_lcs_registers (lcs, "UART setup");
    }

    /* Iterate over test modes */
    seed = 1;
    for (uart_test_mode_t test_mode = 0; test_mode < UART_TEST_MODE_ARRAY_SIZE; test_mode++)
    {
        /* Skip a test mode which isn't enabled */
        if (!arg_enabled_test_modes[test_mode])
        {
            continue;
        }

        /* Perform any test mode specific setup */
        switch (test_mode)
        {
        case UART_TEST_MODE_DMA_RING:
            /* Perform initialisation to use DMA, using a different DMA channel for each test context.
             * Depending upon if internal or external loopback is used for a test the DMA channel can target one or both UARTs.
             * This doesn't start any DMA. */
            if (!pex_check_ring_dma_iova_constraints (&vfio_mapping))
            {
                printf ("Skipping DMA ring test, as IOVA above 4GB address boundary\n");
                continue;
            }
            for (context_index = 0; context_index < arg_num_uarts_tested; context_index++)
            {
                pex_initialise_dma_ring (&contexts[context_index].ring, lcs, context_index, TEST_DMA_RING_SIZE, &vfio_mapping);
            }
            if (arg_dump_pex_registers)
            {
                pex_dump_lcs_registers (lcs, "DMA ring setup");
            }
            break;

        case UART_TEST_MODE_DMA_BLOCK:
            for (context_index = 0; context_index < arg_num_uarts_tested; context_index++)
            {
                pex_initialise_dma_block (&contexts[context_index].block, lcs, context_index);
            }
            if (arg_dump_pex_registers)
            {
                pex_dump_lcs_registers (lcs, "DMA block setup");
            }
            break;

        default:
            /* No specific setup for this test mode */
            break;
        }

        /* Select internal loopback for the UARTS, where the each port loops back to itself */
        internal_loopback = true;
        for (port_index = 0; port_index < arg_num_uarts_tested; port_index++)
        {
            uart_port_t *const port = &ports[port_index];

            serial_set_internal_loopback (port, internal_loopback);
            contexts[port_index].tx_port = port;
            contexts[port_index].rx_port = port;
        }

        read_lsr = true;
        perform_uart_loopback_test (contexts, test_mode, &seed, read_lsr, internal_loopback, &total_tests, &total_test_failures);
        read_lsr = false;
        perform_uart_loopback_test (contexts, test_mode, &seed, read_lsr, internal_loopback, &total_tests, &total_test_failures);

        if (arg_test_external_loopback)
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
            for (port_index = 0; port_index < arg_num_uarts_tested; port_index++)
            {
                serial_set_internal_loopback (&ports[port_index], internal_loopback);
                contexts[port_index].tx_port = &ports[port_index];
                contexts[port_index].rx_port = &ports[(port_index + 1) % arg_num_uarts_tested];
            }

            /* Perform a test using external loopback and PIO */
            read_lsr = true;
            perform_uart_loopback_test (contexts, test_mode, &seed, read_lsr, internal_loopback, &total_tests, &total_test_failures);
            read_lsr = false;
            perform_uart_loopback_test (contexts, test_mode, &seed, read_lsr, internal_loopback, &total_tests, &total_test_failures);
        }

        if (arg_dump_pex_registers)
        {
            pex_dump_lcs_registers (lcs, "test mode completion");
        }
    }

    /* Report statistics on the Rx FIFO level changes */
    for (port_index = 0; port_index < arg_num_uarts_tested; port_index++)
    {
        const uart_port_t *const port = &ports[port_index];

        printf ("PORT BAR %d Rx FIFO level change min=%d (%u->%u at num_rx_blocks %u) max=%d num_negative=%u\n",
                port->bar_index,
                port->rx_fifo_level_change_min,
                port->rfl_change_min_value_before, port->rfl_change_min_value_after, port->rfl_change_min_num_rx_blocks,
                port->rx_fifo_level_change_max, port->rfl_change_num_negative);
    }

    /* Disable ASR upon end of tests */
    const bool enable_asr = false;
    for (port_index = 0; port_index < arg_num_uarts_tested; port_index++)
    {
        serial_set_additional_status_read (&ports[port_index], enable_asr);
    }

    free_vfio_dma_mapping (vfio_devices, &vfio_mapping);

    if (total_tests > 0)
    {
        if (total_test_failures > 0)
        {
            printf ("\n%u out of %u tests FAILED\n", total_test_failures, total_tests);
        }
        else
        {
            printf ("\nAll %u tests PASSED\n", total_tests);
        }
    }
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
        .enable_bus_master = true
    };

    parse_command_line_arguments (argc, argv);

    /* Open the Sealevel devices which have an IOMMU group assigned */
    open_vfio_devices_matching_filter (&vfio_devices, 1, &filter);

    /* For test purposes allow overriding the starting IOVA value */
    vfio_devices.next_iova += arg_iova_increment;

    /* Process any Sealevel devices found */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        perform_uart_tests (&vfio_devices, device_index);
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
