/*
 * @file xilinx_cms_host_interface.h
 * @date 25 Oct 2025
 * @author Chester Gillon
 * @brief Defines the interface to the "Xilinx Card Management Solution Subsystem (CMS Subsystem)" from the point of view of the host
 * @details
 *   https://docs.amd.com/r/en-US/pg348-cms-subsystem documents the CMS Subsystem.
 *
 *   Only the sub-set of the registers used by the xilinx_cms library are defined in this file.
 */

#ifndef XILINX_CMS_HOST_INTERFACE_H_
#define XILINX_CMS_HOST_INTERFACE_H_

/* Register map ID */
#define CMS_REG_MAP_ID_REG_OFFSET 0x0000
#define CMS_EXPECTED_REG_MAP_ID 0x74736574

/* Firmware version */
#define CMS_FW_VERSION_REG_OFFSET 0x0004


/* Software profile */
#define CMS_PROFILE_NAME_REG_OFFSET 0x0014


/* Control register. Bits without a mask are reserved. */
#define CMS_CONTROL_REG_OFFSET 0x0018
#define CMS_CONTROL_REG_HBM_TEMPERATURE_MONITORING (1u << 27) /* (1=enable, 0=disable).
                                                                 Note: This feature enables monitoring of HBM temperature sensors
                                                                 and should be enabled on Alveo U280, U50, and U55 cards. */
#define CMS_CONTROL_REG_QSFP_GPIO_ENABLE (1u << 26) /* (1=enable, 0=disable).
                                                        Note: This bit must be set on Alveo U200 and U250 cards to enable the
                                                        QSFP low speed I/O management feature. QSFP low speed I/O management is
                                                        autonomous on all other Alveo card types. */
#define CMS_CONTROL_REG_REBOOT (1u << 6) /* Set to reboot MicroBlaze. */
#define CMS_CONTROL_REG_MAILBOX_MESSAGE_STATUS (1u << 5) /* Set to 1 by host to indicate new message present in Mailbox.
                                                            Cleared to 0 by CMS when message has been processed. */
#define CMS_CONTROL_REG_RESET_ERROR_REG (1u << 1) /* Set to Reset ERROR_REG. Self clears when finished. */
#define CMS_CONTROL_REG_RESET_MAX_AVG (1u << 0) /*  Set to reset MAX and AVG sensor values, self clearing. */


/* Offset of MAILBOX inside register map */
#define CMS_HOST_MSG_OFFSET_REG_OFFSET 0x300


/* Error for mailbox */
#define CMS_HOST_MSG_ERROR_REG_OFFSET 0x0304


/* Bits 31:1 - Reserved
   Bit 0 - REG_MAP Ready ('0'=false, '1'=true)
           This bit indicates the REG_MAP is ready. Mailbox features/reading of sensor values should not be attempted until
           this bit is set. */
#define CMS_HOST_STATUS2_REG_OFFSET 0x030C
#define CMS_REG_MAP_READY_MASK      0x1


/* Definitions for mailbox */
#define CMS_MAILBOX_HEADER_WORD_OFFSET 0x0
#define CMS_MAILBOX_HEADER_OPCODE_MASK       0xFF000000
#define CMS_MAILBOX_HEADER_LENGTH_BYTES_MASK 0x00000FFF

#define CMS_OP_CARD_INFO_REQ_OPCODE            0x04
#define CMS_OP_READ_MODULE_LOW_SPEED_IO_OPCODE 0x0D

#define CMS_MAILBOX_PAYLOAD_START_OFFSET   0x4

#endif /* XILINX_CMS_HOST_INTERFACE_H_ */
