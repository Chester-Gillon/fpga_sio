/*
 * @file serial_reg.h
 * @details
 * A sub-set of the definitions from the Linux Kernel include/linux/serial_reg.h
 *
 * Copyright (C) 1992, 1994 by Theodore Ts'o.
 *
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL)
 *
 * These are the UART port assignments, expressed as offsets from the base
 * register.  These assignments should hold for any serial port based on
 * a 8250, 16450, or 16550(A).
 */

#ifndef SERIAL_REG_H_
#define SERIAL_REG_H_

/*
 * DLAB=0
 */
#define UART_RX     0   /* In:  Receive buffer */
#define UART_TX     0   /* Out: Transmit buffer */

#define UART_IER    1   /* Out: Interrupt Enable Register */

#define UART_IIR    2   /* In:  Interrupt ID Register */

#define UART_FCR    2   /* Out: FIFO Control Register */
#define UART_FCR_ENABLE_FIFO    0x01 /* Enable the FIFO */
#define UART_FCR_CLEAR_RCVR 0x02 /* Clear the RCVR FIFO */
#define UART_FCR_CLEAR_XMIT 0x04 /* Clear the XMIT FIFO */

#define UART_LCR    3   /* Out: Line Control Register */
/*
 * Note: if the word length is 5 bits (UART_LCR_WLEN5), then setting
 * UART_LCR_STOP will select 1.5 stop bits, not 2 stop bits.
 */
#define UART_LCR_DLAB       0x80 /* Divisor latch access bit */
#define UART_LCR_SBC        0x40 /* Set break control */
#define UART_LCR_SPAR       0x20 /* Stick parity (?) */
#define UART_LCR_EPAR       0x10 /* Even parity select */
#define UART_LCR_PARITY     0x08 /* Parity Enable */
#define UART_LCR_STOP       0x04 /* Stop bits: 0=1 bit, 1=2 bits */
#define UART_LCR_WLEN5      0x00 /* Wordlength: 5 bits */
#define UART_LCR_WLEN6      0x01 /* Wordlength: 6 bits */
#define UART_LCR_WLEN7      0x02 /* Wordlength: 7 bits */
#define UART_LCR_WLEN8      0x03 /* Wordlength: 8 bits */

/*
 * Access to some registers depends on register access / configuration
 * mode.
 */
#define UART_LCR_CONF_MODE_A    UART_LCR_DLAB   /* Configutation mode A */
#define UART_LCR_CONF_MODE_B    0xBF        /* Configutation mode B */

#define UART_MCR    4   /* Out: Modem Control Register */
#define UART_MCR_LOOP       0x10 /* Enable loopback test mode */

#define UART_LSR    5   /* In:  Line Status Register */
#define UART_LSR_FIFOE      0x80 /* Fifo error */
#define UART_LSR_TEMT       0x40 /* Transmitter empty */
#define UART_LSR_THRE       0x20 /* Transmit-hold-register empty */
#define UART_LSR_BI     0x10 /* Break interrupt indicator */
#define UART_LSR_FE     0x08 /* Frame error indicator */
#define UART_LSR_PE     0x04 /* Parity error indicator */
#define UART_LSR_OE     0x02 /* Overrun error indicator */
#define UART_LSR_DR     0x01 /* Receiver data ready */
#define UART_LSR_BRK_ERROR_BITS 0x1E /* BI, FE, PE, OE bits */

#define UART_MSR    6   /* In:  Modem Status Register */

#define UART_SCR    7   /* I/O: Scratch Register */

/*
 * DLAB=1
 */
#define UART_DLL    0   /* Out: Divisor Latch Low */
#define UART_DLM    1   /* Out: Divisor Latch High */

/*
 * LCR=0xBF (or DLAB=1 for 16C660)
 */
#define UART_EFR    2   /* I/O: Extended Features Register */
#define UART_EFR_ECB        0x10 /* Enhanced control bit */

/*
 * These register definitions are for the 16C950
 */
#define UART_ASR    0x01    /* Additional Status Register */
#define UART_RFL    0x03    /* Receive FIFO Level */
#define UART_TFL    0x04    /* Transmit FIFO Level */
#define UART_ICR    0x05    /* Index Control Register */

/* The 16950 ICR registers */
#define UART_ACR    0x00    /* Additional Control Register */
#define UART_TCR    0x02    /* Times Clock Register */
#define UART_ID1    0x08    /* ID #1 */
#define UART_ID2    0x09    /* ID #2 */
#define UART_ID3    0x0A    /* ID #3 */
#define UART_REV    0x0B    /* Revision */

/*
 * The 16C950 Additional Control Register
 */
#define UART_ACR_ICRRD  0x40    /* ICR Read enable */
#define UART_ACR_ASE    0x80    /* Additional Status Enable */

#endif /* SERIAL_REG_H_ */
