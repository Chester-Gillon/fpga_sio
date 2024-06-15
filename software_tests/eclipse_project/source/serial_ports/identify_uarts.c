/*
 * @file identify_uarts.c
 * @date 14 Jun 2024
 * @author Chester Gillon
 * @brief Identify UARTs using VFIO
 * @details
 *   This was written to:
 *   1. Demonstrate using VFIO to access BARs with IO space, as well as memory mapped space
 *   2. Have two ways to identify UARTs on serial ports:
 *      a. An auto-detection of the UART type, with the same logic as the Linux Kernel serial driver, but with a cut-down
 *         set of UART types for those available in a PC to test.
 *      b. The simple "dead port" detection test performed by GRUB, which just supports the lowest common denomination
 *         of a 8250 UART.
 */

#include "vfio_access.h"

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <linux/serial_reg.h>


/* The possible serial cards which this program can identify UARTs in */
typedef enum
{
    /*  Nanjing Qinheng Microelectronics Co., Ltd. CH352/CH382 PCI/PCIe Dual Port Serial Adapter */
    SERIAL_CARD_WCH_CH382_2S,
    /* Intel Corporation C610/X99 series chipset KT Controller */
    SERIAL_CARD_X99_KT,
    /* Sealevel COMM+2.LPCIe board (7205e), which has been modified to place the BARs in as memory mapped rather than I/O */
    SERIAL_CARD_SEALEVEL_7205E,

    SERIAL_CARD_ARRAY_SIZE
} serial_cards_t;


/* The possible types of UART which this program can identify */
typedef enum
{
    UART_UNKNOWN,
    UART_16C950,
    /* /proc/tty/driver/serial in AlmaLinux Kernel 4.18.0-513.24.1.el8_9.x86_64 identifies SERIAL_CARD_WCH_CH382_2S
     * as containing a XR16850 UART.
     *
     * When running the code in autoconfig_16550a() which has been cut-down from the Kernel source, the SERIAL_CARD_WCH_CH382_2S
     * isn't identified as a XR16850 due to not detecting an Extended Features Register (EFR).
     *
     * Instead identified as a 16750
     *
     * https://wch-ic.com/products/CH382.html is the project page for the SERIAL_CARD_WCH_CH382_2S, which
     * has the download line to https://wch-ic.com/downloads/CH382DS1_PDF.html for the datasheet. The datasheet doesn't
     * describe the EFR but does say:
     *   The UART of CH382 is compatible with the industry standard 16550 or 16C750 with enhanced. The register
     *   bit marked in gray in the table is the enhanced function, and the length of FIFO buffer is extended to 256
     *   bytes, other registers refer to the description of the single serial port 16C550 or dual UARTs CH432 or octal
     *   UARTs CH438.
     */
    UART_XR16850,
    UART_16750,
    UART_16550,
    UART_16550A,
    UART_8250,
    UART_16450,

    /* Indicates the UART should be supported by GRUB, which just checks for a read/write test of the Scratch Regiser */
    UART_GRUB_SUPPORTED
} uart_type_t;

static const char *const uart_names[] =
{
    [UART_UNKNOWN       ] = "UNKNOWN",
    [UART_16C950        ] = "16C950",
    [UART_XR16850       ] = "XR16850",
    [UART_16750         ] = "16750",
    [UART_16550         ] = "16550",
    [UART_16550A        ] = "16550A",
    [UART_8250          ] = "8250",
    [UART_16450         ] = "16450",
    [UART_GRUB_SUPPORTED] = "GRUB_SUPPORTED"
};


/* Defines one serial port on a card */
typedef struct
{
    /* Which BAR the serial port is on */
    uint32_t bar_index;
    /* The byte offset within the BAR to the base of the registers for the UART */
    uint32_t base_offset;
} serial_port_definition_t;


/* Define one serial card used by this program */
#define SERIAL_CARD_MAX_PORTS 2
typedef struct
{
    /* The PCI device identity, used to open the serial card using VFIO */
    vfio_pci_device_identity_filter_t filter;
    /* The number of serial ports on the card */
    uint32_t num_serial_ports;
    /* The definition of each serial port */
    serial_port_definition_t port_definitions[SERIAL_CARD_MAX_PORTS];
} serial_card_definition_t;

/* Defines the serial cards this program can use */
static const serial_card_definition_t serial_card_definitions[SERIAL_CARD_ARRAY_SIZE] =
{
     [SERIAL_CARD_WCH_CH382_2S] =
     {
         .filter =
         {
             .vendor_id = 0x1c00,
             .device_id = 0x3253,
             .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
             .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY,
             .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
         },
         .num_serial_ports = 2,
         .port_definitions =
         {
             [0] = {.bar_index = 0, .base_offset = 0xc0},
             [1] = {.bar_index = 0, .base_offset = 0xc8}
         }
     },
     [SERIAL_CARD_X99_KT] =
     {
         .filter =
         {
             .vendor_id = 0x8086,
             .device_id = 0x8d3d,
             .subsystem_vendor_id = VFIO_PCI_DEVICE_FILTER_ANY,
             .subsystem_device_id = VFIO_PCI_DEVICE_FILTER_ANY,
             .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
         },
         .num_serial_ports = 1,
         .port_definitions =
         {
             [0] = {.bar_index = 0, .base_offset = 0}
         }
     },
     [SERIAL_CARD_SEALEVEL_7205E] =
     {
         .filter =
         {
             .vendor_id = 0x10b5,
             .device_id = 0x9056,
             .subsystem_vendor_id = 0x10b5,
             .subsystem_device_id = 0x3198,
             .dma_capability = VFIO_DEVICE_DMA_CAPABILITY_NONE
         },
         .num_serial_ports = 2,
         .port_definitions =
         {
             [0] = {.bar_index = 2, .base_offset = 0},
             [1] = {.bar_index = 3, .base_offset = 0}
         }
     }
};


/* Define the context used for accessing the UART registers for one serial port */
typedef struct
{
    /* Which port on the serial card */
    uint32_t port_index;
    /* Provides access to the serial card */
    vfio_device_t *vfio_device;
    /* Which BAR the serial port is on */
    uint32_t bar_index;
    /* The byte offset within the BAR to the base of the registers for the UART */
    uint32_t base_offset;
    /* When non-NULL the memory mapped access to the BAR containing the registers for the UART.
     * When NULL IO access is used. */
    uint8_t *bar_mapping;
    /* Tracks registers which have to be be temporarily changed without affecting operational mode */
    uint8_t acr;
    uint8_t lcr;
    /* The UART which has been identified */
    uart_type_t identified_uart;
} uart_port_t;


/* Command line argument to select which identification mechanism is performed */
static bool arg_perform_grub_serial_dead_port_detection;


/**
 * @brief Write to a UART register
 * @param[in] port Which UART to write to
 * @param[in] register_offset The register offset to write to
 * @param[in] value The register value to write
 */
static void serial_out (uart_port_t *const port, const uint32_t register_offset, const uint8_t value)
{
    const uint32_t offset = port->base_offset + register_offset;

    if (port->bar_mapping != NULL)
    {
        /* The UART registers are memory mapped into the process address space */
        write_reg8 (port->bar_mapping, offset, value);
    }
    else
    {
        /* The UART registers are in IO space, and have to accessed via a Kernel call to the VFIO driver */
        if (!vfio_write_pci_region_bytes (port->vfio_device, port->bar_index, offset, 1, &value))
        {
            printf ("Write to %s region %u offset %u failed\n", port->vfio_device->device_name, port->bar_index, offset);
            exit (EXIT_FAILURE);
        }
    }
}


/**
 * @brief Read from a UART register
 * @param[in] port Which UART to read from
 * @param[in] register_offset The register offset to read from
 * @return The register value
 */
static uint8_t serial_in (uart_port_t *const port, const uint32_t register_offset)
{
    const uint32_t offset = port->base_offset + register_offset;
    uint8_t value;

    if (port->bar_mapping != NULL)
    {
        /* The UART registers are memory mapped into the process address space */
        value = read_reg8 (port->bar_mapping, offset);
    }
    else
    {
        /* The UART registers are in IO space, and have to accessed via a Kernel call to the VFIO driver */
        if (!vfio_read_pci_region_bytes (port->vfio_device, port->bar_index, offset, 1, &value))
        {
            printf ("Read from %s region %u offset %u failed\n", port->vfio_device->device_name, port->bar_index, offset);
            exit (EXIT_FAILURE);
        }
    }

    return value;
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


/* Uart divisor latch read */
static uint32_t serial_dl_read (uart_port_t *const port)
{
    return (uint32_t) serial_in (port, UART_DLL) | ((uint32_t) serial_in (port, UART_DLM) << 8);
}


/* Uart divisor latch write */
static void serial_dl_write (uart_port_t *const port, const uint32_t value)
{
    serial_out (port, UART_DLL, value & 0xff);
    serial_out (port, UART_DLM, (value >> 8) & 0xff);
}


/*
 * Read UART ID using the divisor method - set DLL and DLM to zero
 * and the revision will be in DLL and device type in DLM.  We
 * preserve the device state across this.
 */
static uint32_t autoconfig_read_divisor_id (uart_port_t *const port)
{
    uint8_t old_lcr;
    uint32_t id;
    uint32_t old_dl;

    old_lcr = serial_in (port, UART_LCR);
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_A);
    old_dl = serial_dl_read (port);
    serial_dl_write (port, 0);
    id =  serial_dl_read (port);
    serial_dl_write (port, old_dl);

    serial_out(port, UART_LCR, old_lcr);

    return id;
}


/**
 * @brief This is a helper routine to autodetect StarTech/Exar/Oxsemi UART's
 * @param[in] port Which UART to auto-detect
 */
static void autoconfig_has_efr (uart_port_t *const port)
{
    /*
     * The 16C950 requires 0xbf to be written to the LCR to read the ID.
     */
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_B);
    if (serial_in (port, UART_EFR) == 0)
    {
        uint32_t id1, id2, id3, rev;

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
            port->identified_uart = UART_16C950;
            return;
        }

        /*
         * We check for a XR16C850 by setting DLL and DLM to 0, and then
         * reading back DLL and DLM.  The chip type depends on the DLM
         * value read back:
         *  0x10 - XR16C850 and the DLL contains the chip revision.
         *  0x12 - XR16C2850.
         *  0x14 - XR16C854.
         */
        id1 = autoconfig_read_divisor_id (port);

        id2 = id1 >> 8;
        if (id2 == 0x10 || id2 == 0x12 || id2 == 0x14) {
            port->identified_uart = UART_XR16850;
            return;
        }
    }
}

/*
 * We know that the chip has FIFOs.  Does it have an EFR?  The
 * EFR is located in the same register position as the IIR and
 * we know the top two bits of the IIR are currently set.  The
 * EFR should contain zero.  Try to read the EFR.
 */
static void autoconfig_16550a (uart_port_t *const port)
{
    uint8_t status1;
    uint8_t status2;

    port->identified_uart = UART_16550A;

    /*
     * Maybe it requires 0xbf to be written to the LCR.
     * (other ST16C650V2 UARTs, TI16C752A, etc)
     */
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_B);
    if (serial_in (port, UART_EFR) == 0)
    {
        autoconfig_has_efr (port);
        return;
    }

    /*
     * No EFR.  Try to detect a TI16750, which only sets bit 5 of
     * the IIR when 64 byte FIFO mode is enabled when DLAB is set.
     * Try setting it with and without DLAB set.  Cheap clones
     * set bit 5 without DLAB set.
     */
    serial_out (port, UART_LCR, 0);
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
    status1 = serial_in (port, UART_IIR) >> 5;
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO);
    serial_out (port, UART_LCR, UART_LCR_CONF_MODE_A);
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
    status2 = serial_in( port, UART_IIR) >> 5;
    serial_out (port, UART_FCR, UART_FCR_ENABLE_FIFO);
    serial_out (port, UART_LCR, 0);

    if ((status1 == 6) && (status2 == 7))
    {
        port->identified_uart = UART_16750;
        return;
    }
}



/*
 * We detected a chip without a FIFO.  Only two fall into
 * this category - the original 8250 and the 16450.  The
 * 16450 has a scratch register (accessible with LCR=0)
 */
static void autoconfig_8250 (uart_port_t *const port)
{
    uint8_t scratch, status1, status2;

    port->identified_uart = UART_8250;

    scratch = serial_in (port, UART_SCR);
    serial_out (port, UART_SCR, 0xa5);
    status1 = serial_in (port, UART_SCR);
    serial_out (port, UART_SCR, 0x5a);
    status2 = serial_in (port, UART_SCR);
    serial_out (port, UART_SCR, scratch);

    if ((status1 == 0xa5) && (status2 == 0x5a))
    {
        port->identified_uart = UART_16450;
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
 *          to the expected UARTs.
 * @param[in/out] port Which UART to auto-detect
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
        return;
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
        autoconfig_8250 (port);
        break;
    case 1:
        port->identified_uart = UART_UNKNOWN;
        break;
    case 2:
        port->identified_uart = UART_16550;
        break;
    case 3:
        autoconfig_16550a (port);
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
 * @brief Determine if a serial port should be detected by GRUB, which performs a write/read test on the scratch register
 * @param[in/out] port Which UART to test
 */
static void perform_grub_serial_dead_port_detection (uart_port_t *const port)
{
    uint8_t readback;

    serial_out (port, UART_SCR, 0x5a);
    readback = serial_in (port, UART_SCR);
    if (readback != 0x5a)
    {
        return;
    }
    serial_out (port, UART_SCR, 0xa5);
    readback = serial_in (port, UART_SCR);
    if (readback != 0xa5)
    {
        return;
    }

    port->identified_uart = UART_GRUB_SUPPORTED;
}


/**
 * @brief Attempt to identify the UART for a serial port.
 * @details This is done by probing the UART registers, rather than relying upon the PCI vendor / device IDs
 * @param[in/out] vfio_device Provides access to the serial port registers
 * @param[in] card_definition Defines the serial card in terms of where to find the base of the UART registers
 * @param[in] port_index Which port on the serial card to identify the UART for
 */
static void identify_serial_port_uart (vfio_device_t *const vfio_device, const serial_card_definition_t *const card_definition,
                                       const uint32_t port_index)
{
    /* Obtain access to the UART registers by VFIO.
     * Doesn't test for the BAR being IO or memory mapped address space, but rather attempts to map the BAR which will only
     * succeed if the BAR can be memory mapped, which is reported in the flags for the region. */
    uart_port_t port =
    {
        .port_index = port_index,
        .vfio_device = vfio_device,
        .bar_index = card_definition->port_definitions[port_index].bar_index,
        .base_offset = card_definition->port_definitions[port_index].base_offset,
        .identified_uart = UART_UNKNOWN
    };

    map_vfio_device_bar_before_use (vfio_device, port.bar_index);
    port.bar_mapping = vfio_device->mapped_bars[port.bar_index];

    /* Report the access mechanism in use */
    if (port.bar_mapping != NULL)
    {
        printf ("Probing port %u on device %s using memory mapping\n", port_index, vfio_device->device_name);
    }
    else if (vfio_device->regions_info_populated &&
             ((vfio_device->regions_info[port.bar_index].flags & VFIO_REGION_INFO_FLAG_READ) != 0) &&
             ((vfio_device->regions_info[port.bar_index].flags & VFIO_REGION_INFO_FLAG_WRITE) != 0))
    {
        printf ("Probing port %u on device %s using IO\n", port_index, vfio_device->device_name);
    }
    else
    {
        printf ("Unable to access port %u on device %s using VFIO\n", port_index, vfio_device->device_name);
        return;
    }

    if (arg_perform_grub_serial_dead_port_detection)
    {
       perform_grub_serial_dead_port_detection (&port);
    }
    else
    {
        autoconfig (&port);
    }

    printf ("  Identified UART: %s\n", uart_names[port.identified_uart]);
}


/**
 * @brief Parse the command line arguments
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "d:g";
    int option;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case 'g':
            arg_perform_grub_serial_dead_port_detection = true;
            break;

        case '?':
        default:
            printf ("Usage %s [-d <pci_device_location>] [-g]\n", argv[0]);
            printf ("  When -g is present performs GRUB serial dead port detection, otherwise\n");
            printf ("  performs a UART type auto-detection which is based upon a subset of the\n");
            printf ("  logic from the Linux Kernel serial port driver\n");
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


int main (int argc, char *argv[])
{
    vfio_devices_t vfio_devices;
    uint32_t card_index;
    vfio_pci_device_identity_filter_t filters[SERIAL_CARD_ARRAY_SIZE];

    parse_command_line_arguments (argc, argv);

    /* Open the devices which match the supported serial cards */
    for (card_index = 0; card_index < SERIAL_CARD_ARRAY_SIZE; card_index++)
    {
        filters[card_index] = serial_card_definitions[card_index].filter;
    }
    open_vfio_devices_matching_filter (&vfio_devices, SERIAL_CARD_ARRAY_SIZE, filters);

    /* Process the opened devices */
    for (uint32_t device_index = 0; device_index < vfio_devices.num_devices; device_index++)
    {
        vfio_device_t *const vfio_device = &vfio_devices.devices[device_index];

        for (card_index = 0; card_index < SERIAL_CARD_ARRAY_SIZE; card_index++)
        {
            const serial_card_definition_t *const card_definition = &serial_card_definitions[card_index];

            if (vfio_device_pci_filter_match (vfio_device, &card_definition->filter))
            {
                for (uint32_t port_index = 0; port_index < card_definition->num_serial_ports; port_index++)
                {
                    identify_serial_port_uart (vfio_device, card_definition, port_index);
                }
            }
        }
    }

    close_vfio_devices (&vfio_devices);

    return EXIT_SUCCESS;
}
