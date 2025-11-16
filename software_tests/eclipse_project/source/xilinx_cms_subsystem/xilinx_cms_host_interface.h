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


/* Read-only sensor value registers */
#define CMS_12V_PEX_MAX_REG_OFFSET 0x0020 /* 12V_PEX Max Voltage Unsigned 32b int (mV) */
#define CMS_12V_PEX_AVG_REG_OFFSET 0x0024 /* 12V_PEX Average Voltage Unsigned 32b int (mV) */
#define CMS_12V_PEX_INS_REG_OFFSET 0x0028 /* 12V_PEX Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_3V3_PEX_MAX_REG_OFFSET 0x002C /* 3V3_PEX Max Voltage Unsigned 32b int (mV) */
#define CMS_3V3_PEX_AVG_REG_OFFSET 0x0030 /* 3V3_PEX Average Voltage Unsigned 32b int (mV) */
#define CMS_3V3_PEX_INS_REG_OFFSET 0x0034 /* 3V3_PEX Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_3V3_AUX_MAX_REG_OFFSET 0x0038 /* 3V3_AUX Max Voltage Unsigned 32b int (mV) */
#define CMS_3V3_AUX_AVG_REG_OFFSET 0x003C /* 3V3_AUX Average Voltage Unsigned 32b int (mV) */
#define CMS_3V3_AUX_INS_REG_OFFSET 0x0040 /* 3V3_AUX Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_12V_AUX_MAX_REG_OFFSET 0x0044 /* 12V_AUX Max Voltage Unsigned 32b int (mV) */
#define CMS_12V_AUX_AVG_REG_OFFSET 0x0048 /* 12V_AUX Average Voltage Unsigned 32b int (mV) */
#define CMS_12V_AUX_INS_REG_OFFSET 0x004C /* 12V_AUX Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_DDR4_VPP_BTM_MAX_REG_OFFSET 0x0050 /* DDR4 VPP BTM Max Voltage Unsigned 32b int (mV) */
#define CMS_DDR4_VPP_BTM_AVG_REG_OFFSET 0x0054 /* DDR4 VPP BTM Average Voltage Unsigned 32b int (mV) */
#define CMS_DDR4_VPP_BTM_INS_REG_OFFSET 0x0058 /* DDR4 VPP BTM Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_SYS_5V5_MAX_REG_OFFSET 0x005C /* SYS_5V5 Max Voltage Unsigned 32b int (mV) */
#define CMS_SYS_5V5_AVG_REG_OFFSET 0x0060 /* SYS_5V5 Average Voltage Unsigned 32b int (mV) */
#define CMS_SYS_5V5_INS_REG_OFFSET 0x0064 /* SYS_5V5 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V2_TOP_MAX_REG_OFFSET 0x0068 /* VCC1V2_TOP Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V2_TOP_AVG_REG_OFFSET 0x006C /* VCC1V2_TOP Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V2_TOP_INS_REG_OFFSET 0x0070 /* VCC1V2_TOP Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V8_MAX_REG_OFFSET 0x0074 /* VCC1V8 Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V8_AVG_REG_OFFSET 0x0078 /* VCC1V8 Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V8_INS_REG_OFFSET 0x007C /* VCC1V8 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCC0V85_MAX_REG_OFFSET 0x0080 /* VCC0V85 Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC0V85_AVG_REG_OFFSET 0x0084 /* VCC0V85 Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC0V85_INS_REG_OFFSET 0x0088 /* VCC0V85 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_DDR4_VPP_TOP_MAX_REG_OFFSET 0x008C /* DDR4_VPP_TOP Max Voltage Unsigned 32b int (mV) */
#define CMS_DDR4_VPP_TOP_AVG_REG_OFFSET 0x0090 /* DDR4_VPP_TOP Average Voltage Unsigned 32b int (mV) */
#define CMS_DDR4_VPP_TOP_INS_REG_OFFSET 0x0094 /* DDR4_VPP_TOP Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_MGT0V9AVCC_MAX_REG_OFFSET 0x0098 /* MGT0V9AVCC Max Voltage Unsigned 32b int (mV) */
#define CMS_MGT0V9AVCC_AVG_REG_OFFSET 0x009C /* MGT0V9AVCC Average Voltage Unsigned 32b int (mV) */
#define CMS_MGT0V9AVCC_INS_REG_OFFSET 0x00A0 /* MGT0V9AVCC Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_12V_SW_MAX_REG_OFFSET 0x00A4 /* 12V_SW Max Voltage Unsigned 32b int (mV) */
#define CMS_12V_SW_AVG_REG_OFFSET 0x00A8 /* 12V_SW Average Voltage Unsigned 32b int (mV) */
#define CMS_12V_SW_INS_REG_OFFSET 0x00AC /* 12V_SW Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVTT_MAX_REG_OFFSET 0x00B0 /* MGTAVTT Max Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVTT_AVG_REG_OFFSET 0x00B4 /* MGTAVTT Average Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVTT_INS_REG_OFFSET 0x00B8 /* MGTAVTT Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V2_BTM_MAX_REG_OFFSET 0x00BC /* VCC1V2_BTM Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V2_BTM_AVG_REG_OFFSET 0x00C0 /* VCC1V2_BTM Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V2_BTM_INS_REG_OFFSET 0x00C4 /* VCC1V2_BTM Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_12VPEX_I_IN_MAX_REG_OFFSET 0x00C8 /* 12VPEX_I_IN Max Current Unsigned 32b int (mA) */
#define CMS_12VPEX_I_IN_AVG_REG_OFFSET 0x00CC /* 12VPEX_I_IN Average Current Unsigned 32b int (mA) */
#define CMS_12VPEX_I_IN_INS_REG_OFFSET 0x00D0 /* 12VPEX_I_IN Instantaneous Current Unsigned 32b int (mA) */
#define CMS_12V_AUX_I_IN_MAX_REG_OFFSET 0x00D4 /* 12V_AUX_I_IN Max Current Unsigned 32b int (mA) */
#define CMS_12V_AUX_I_IN_AVG_REG_OFFSET 0x00D8 /* 12V_AUX_I_IN Average Current Unsigned 32b int (mA) */
#define CMS_12V_AUX_I_IN_INS_REG_OFFSET 0x00DC /* 12V_AUX_I_IN Instantaneous Current Unsigned 32b int (mA) */
#define CMS_VCCINT_MAX_REG_OFFSET 0x00E0 /* VCCINT Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_AVG_REG_OFFSET 0x00E4 /* VCCINT Average Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_INS_REG_OFFSET 0x00E8 /* VCCINT Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_I_MAX_REG_OFFSET 0x00EC /* VCCINT_I Max Current Unsigned 32b int (mA) */
#define CMS_VCCINT_I_AVG_REG_OFFSET 0x00F0 /* VCCINT_I Average Current Unsigned 32b int (mA) */
#define CMS_VCCINT_I_INS_REG_OFFSET 0x00F4 /* VCCINT_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_FPGA_TEMP_MAX_REG_OFFSET 0x00F8 /* FPGA_TEMP Max Temperature Unsigned 32b int (C) */
#define CMS_FPGA_TEMP_AVG_REG_OFFSET 0x00FC /* FPGA_TEMP Average Temperature Unsigned 32b int (C) */
#define CMS_FPGA_TEMP_INS_REG_OFFSET 0x0100 /* FPGA_TEMP Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_FAN_TEMP_MAX_REG_OFFSET 0x0104 /* FAN_TEMP Max Temperature Unsigned 32b int (C) */
#define CMS_FAN_TEMP_AVG_REG_OFFSET 0x0108 /* FAN_TEMP Average Temperature Unsigned 32b int (C) */
#define CMS_FAN_TEMP_INS_REG_OFFSET 0x010C /* FAN_TEMP Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP0_MAX_REG_OFFSET 0x0110 /* DIMM_TEMP0 Max Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP0_AVG_REG_OFFSET 0x0114 /* DIMM_TEMP0 Average Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP0_INS_REG_OFFSET 0x0118 /* DIMM_TEMP0 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP1_MAX_REG_OFFSET 0x011C /* DIMM_TEMP1 Max Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP1_AVG_REG_OFFSET 0x0120 /* DIMM_TEMP1 Average Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP1_INS_REG_OFFSET 0x0124 /* DIMM_TEMP1 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP2_MAX_REG_OFFSET 0x0128 /* DIMM_TEMP2 Max Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP2_AVG_REG_OFFSET 0x012C /* DIMM_TEMP2 Average Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP2_INS_REG_OFFSET 0x0130 /* DIMM_TEMP2 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP3_MAX_REG_OFFSET 0x0134 /* DIMM_TEMP3 Max Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP3_AVG_REG_OFFSET 0x0138 /* DIMM_TEMP3 Average Temperature Unsigned 32b int (C) */
#define CMS_DIMM_TEMP3_INS_REG_OFFSET 0x013C /* DIMM_TEMP3 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP0_MAX_REG_OFFSET 0x0140 /* SE98_TEMP0 Max Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP0_AVG_REG_OFFSET 0x0144 /* SE98_TEMP0 Average temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP0_INS_REG_OFFSET 0x0148 /* SE98_TEMP0 Instantaneous temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP1_MAX_REG_OFFSET 0x014C /* SE98_TEMP1 Max Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP1_AVG_REG_OFFSET 0x0150 /* SE98_TEMP1 Average Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP1_INS_REG_OFFSET 0x0154 /* SE98_TEMP1 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP2_MAX_REG_OFFSET 0x0158 /* SE98_TEMP2 Max Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP2_AVG_REG_OFFSET 0x015C /* SE98_TEMP2 Average Temperature Unsigned 32b int (C) */
#define CMS_SE98_TEMP2_INS_REG_OFFSET 0x0160 /* SE98_TEMP2 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_FAN_SPEED_MAX_REG_OFFSET 0x0164 /* FAN_SPEED Max Speed Unsigned 32b int (RPM) */
#define CMS_FAN_SPEED_AVG_REG_OFFSET 0x0168 /* FAN_SPEED Average Speed Unsigned 32b int (RPM) */
#define CMS_FAN_SPEED_INS_REG_OFFSET 0x016C /* FAN_SPEED Instantaneous Speed Unsigned 32b int (RPM) */
#define CMS_CAGE_TEMP0_MAX_REG_OFFSET 0x0170 /* CAGE_TEMP0 Max Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP0_AVG_REG_OFFSET 0x0174 /* CAGE_TEMP0 Average Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP0_INS_REG_OFFSET 0x0178 /* CAGE_TEMP0 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP1_MAX_REG_OFFSET 0x017C /* CAGE_TEMP1 Max Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP1_AVG_REG_OFFSET 0x0180 /* CAGE_TEMP1 Average Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP1_INS_REG_OFFSET 0x0184 /* CAGE_TEMP1 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP2_MAX_REG_OFFSET 0x0188 /* CAGE_TEMP2 Max Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP2_AVG_REG_OFFSET 0x018C /* CAGE_TEMP2 Average Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP2_INS_REG_OFFSET 0x0190 /* CAGE_TEMP2 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP3_MAX_REG_OFFSET 0x0194 /* CAGE_TEMP3 Max Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP3_AVG_REG_OFFSET 0x0198 /* CAGE_TEMP3 Average Temperature Unsigned 32b int (C) */
#define CMS_CAGE_TEMP3_INS_REG_OFFSET 0x019C /* CAGE_TEMP3 Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_HBM_TEMP1_MAX_REG_OFFSET 0x0260 /* HBM1_TEMP Max Temperature Unsigned 32b int (C) */
#define CMS_HBM_TEMP1_AVG_REG_OFFSET 0x0264 /* HBM1_TEMP Average Temperature Unsigned 32b int (C) */
#define CMS_HBM_TEMP1_INS_REG_OFFSET 0x0268 /* HBM1_TEMP Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_VCC3V3_MAX_REG_OFFSET 0x026C /* VCC3V3 Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC3V3_AVG_REG_OFFSET 0x0270 /* VCC3V3 Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC3V3_INS_REG_OFFSET 0x0274 /* VCC3V3 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_3V3PEX_I_IN_MAX_REG_OFFSET 0x0278 /* 3V3PEX_I_IN Max Current Unsigned 32b int (mA) */
#define CMS_3V3PEX_I_IN_AVG_REG_OFFSET 0x027C /* 3V3PEX_I_IN Average Current Unsigned 32b int (mA) */
#define CMS_3V3PEX_I_IN_INS_REG_OFFSET 0x0280 /* 3V3PEX_I_IN Instantaneous Current Unsigned 32b int (mA) */
#define CMS_VCCINT_IO_I_MAX_REG_OFFSET 0x0284 /* VCCINT_IO_I Max Current Unsigned 32b int (mA) */
#define CMS_VCCINT_IO_I_AVG_REG_OFFSET 0x0288 /* VCCINT_IO_I Average Current. Unsigned 32b int (mA) */
#define CMS_VCCINT_IO_I_INS_REG_OFFSET 0x028C /* VCCINT_IO_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_HBM_1V2_MAX_REG_OFFSET 0x0290 /* HBM_1V2 Max Voltage Unsigned 32b int (mV) */
#define CMS_HBM_1V2_AVG_REG_OFFSET 0x0294 /* HBM_1V2 Average Voltage Unsigned 32b int (mV) */
#define CMS_HBM_1V2_INS_REG_OFFSET 0x0298 /* HBM_1V2 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VPP2V5_MAX_REG_OFFSET 0x029C /* VPP2V5 Max Voltage Unsigned 32b int (mV) */
#define CMS_VPP2V5_AVG_REG_OFFSET 0x02A0 /* VPP2V5 Average Voltage Unsigned 32b int (mV) */
#define CMS_VPP2V5_INS_REG_OFFSET 0x02A4 /* VPP2V5 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_IO_MAX_REG_OFFSET 0x02A8 /* VCCINT_IO Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_IO_AVG_REG_OFFSET 0x02AC /* VCCINT_IO Average Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_IO_INS_REG_OFFSET 0x02B0 /* VCCINT_IO Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_HBM_TEMP2_MAX_REG_OFFSET 0x02B4 /* HBM2_TEMP Max Temperature Unsigned 32b int (C) */
#define CMS_HBM_TEMP2_AVG_REG_OFFSET 0x02B8 /* HBM2_TEMP Average Temperature Unsigned 32b int (C) */
#define CMS_HBM_TEMP2_INS_REG_OFFSET 0x02BC /* HBM2_TEMP Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_12V_AUX1_MAX_REG_OFFSET 0x02C0 /* 12V_AUX1 Max Voltage Unsigned 32b int (mV) */
#define CMS_12V_AUX1_AVG_REG_OFFSET 0x02C4 /* 12V_AUX1 Average Voltage Unsigned 32b int (mV) */
#define CMS_12V_AUX1_INS_REG_OFFSET 0x02C8 /* 12V_AUX1 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_TEMP_MAX_REG_OFFSET 0x02CC /* VCCINT Max Temperature Unsigned 32b int (C) */
#define CMS_VCCINT_TEMP_AVG_REG_OFFSET 0x02D0 /* VCCINT Average Temperature Unsigned 32b int (C) */
#define CMS_VCCINT_TEMP_INS_REG_OFFSET 0x02D4 /* VCCINT Instantaneous Temperature Unsigned 32b int (C) */
#define CMS_PEX_12V_POWER_MAX_REG_OFFSET 0x02D8 /* PEX_12V Max Power Unsigned 32b int (mW) */
#define CMS_PEX_12V_POWER_AVG_REG_OFFSET 0x02DC /* PEX_12V Average Power Unsigned 32b int (mW) */
#define CMS_PEX_12V_POWER_INS_REG_OFFSET 0x02E0 /* PEX_12V Instantaneous Power Unsigned 32b int (mW) */
#define CMS_PEX_3V3_POWER_MAX_REG_OFFSET 0x02E4 /* PEX_3V3 Max Power Unsigned 32b int (mW) */
#define CMS_PEX_3V3_POWER_AVG_REG_OFFSET 0x02E8 /* PEX_3V3 Average Power Unsigned 32b int (mW) */
#define CMS_PEX_3V3_POWER_INS_REG_OFFSET 0x02EC /* PEX_3V3 Instantaneous Power Unsigned 32b int (mW) */
#define CMS_AUX_3V3_I_MAX_REG_OFFSET 0x02F0 /* AUX_3V3_I Max Current Unsigned 32b int (mA) */
#define CMS_AUX_3V3_I_AVG_REG_OFFSET 0x02F4 /* AUX_3V3_I Average Current Unsigned 32b int (mA) */
#define CMS_AUX_3V3_I_INS_REG_OFFSET 0x02F8 /* AUX_3V3_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_VCC1V2_I_MAX_REG_OFFSET 0x0314 /* VCC1V2_I Max Current Unsigned 32b int (mA) */
#define CMS_VCC1V2_I_AVG_REG_OFFSET 0x0318 /* VCC1V2_I Average Current Unsigned 32b int (mA) */
#define CMS_VCC1V2_I_INS_REG_OFFSET 0x031C /* VCC1V2_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_V12_IN_I_MAX_REG_OFFSET 0x0320 /* V12_IN_I Max Current Unsigned 32b int (mA) */
#define CMS_V12_IN_I_AVG_REG_OFFSET 0x0324 /* V12_IN_I Average Current Unsigned 32b int (mA) */
#define CMS_V12_IN_I_INS_REG_OFFSET 0x0328 /* V12_IN_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_V12_IN_AUX0_I_MAX_REG_OFFSET 0x032C /* V12_IN_AUX0_I Max Current  Unsigned 32b int (mA) */
#define CMS_V12_IN_AUX0_I_AVG_REG_OFFSET 0x0330 /* V12_IN_AUX0_I Average Current Unsigned 32b int (mA) */
#define CMS_V12_IN_AUX0_I_INS_REG_OFFSET 0x0334 /* V12_IN_AUX0_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_V12_IN_AUX1_I_MAX_REG_OFFSET 0x0338 /* V12_IN_AUX1_I Max Current Unsigned 32b int (mA) */
#define CMS_V12_IN_AUX1_I_AVG_REG_OFFSET 0x033C /* V12_IN_AUX1_I Average Current Unsigned 32b int (mA) */
#define CMS_V12_IN_AUX1_I_INS_REG_OFFSET 0x0340 /* V12_IN_AUX1_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_VCCAUX_MAX_REG_OFFSET 0x0344 /* VCCAUX Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCAUX_AVG_REG_OFFSET 0x0348 /* VCCAUX Average Voltage. Unsigned 32b int (mV) */
#define CMS_VCCAUX_INS_REG_OFFSET 0x034C /* VCCAUX Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCAUX_PMC_MAX_REG_OFFSET 0x0350 /* VCCAUX_PMC Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCAUX_PMC_AVG_REG_OFFSET 0x0354 /* VCCAUX_PMC Average Voltage Unsigned 32b int (mV) */
#define CMS_VCCAUX_PMC_INS_REG_OFFSET 0x0358 /* VCCAUX_PMC Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCRAM_MAX_REG_OFFSET 0x035C /* VCCRAM Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCRAM_AVG_REG_OFFSET 0x0360 /* VCCRAM Average Voltage Unsigned 32b int (mV) */
#define CMS_VCCRAM_INS_REG_OFFSET 0x0364 /* VCCRAM Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_POWER_MAX_REG_OFFSET 0x0374 /* VCCINT_POWER Max Power Unsigned 32b int (mW) */
#define CMS_VCCINT_POWER_AVG_REG_OFFSET 0x0378 /* VCCINT_POWER Average Power Unsigned 32b int (mW) */
#define CMS_VCCINT_POWER_INS_REG_OFFSET 0x037C /* VCCINT_POWER Instantaneous Power Unsigned 32b int (mW) */
#define CMS_VCCINT_VCU_0V9_MAX_REG_OFFSET 0x0380 /* VCCINT_VCU_0V9 Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_VCU_0V9_AVG_REG_OFFSET 0x0384 /* VCCINT_VCU_0V9 Average Voltage Unsigned 32b int (mV) */
#define CMS_VCCINT_VCU_0V9_INS_REG_OFFSET 0x0388 /* VCCINT_VCU_0V9 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_1V2_VCCIO_MAX_REG_OFFSET 0x038C /* 1V2_VCCIO Max Voltage Unsigned 32b int (mV) */
#define CMS_1V2_VCCIO_AVG_REG_OFFSET 0x0390 /* 1V2_VCCIO Average Voltage Unsigned 32b int (mV) */
#define CMS_1V2_VCCIO_INS_REG_OFFSET 0x0394 /* 1V2_VCCIO Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_GTAVCC_MAX_REG_OFFSET 0x0398 /* GTAVCC Max Voltage Unsigned 32b int (mV) */
#define CMS_GTAVCC_AVG_REG_OFFSET 0x039C /* GTAVCC Average Voltage Unsigned 32b int (mV) */
#define CMS_GTAVCC_INS_REG_OFFSET 0x03A0 /* GTAVCC Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCCSOC_MAX_REG_OFFSET 0x03B0 /* VCCSOC Max Voltage Unsigned 32b int (mV) */
#define CMS_VCCSOC_AVG_REG_OFFSET 0x03B4 /* VCCSOC Average Voltage Unsigned 32b int (mV) */
#define CMS_VCCSOC_INS_REG_OFFSET 0x03B8 /* VCCSOC Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_VCC_5V0_MAX_REG_OFFSET 0x03BC /* VCC_5V0 Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC_5V0_AVG_REG_OFFSET 0x03C0 /* VCC_5V0 Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC_5V0_INS_REG_OFFSET 0x03C4 /* VCC_5V0 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_2V5_VPP23_MAX_REG_OFFSET 0x03C8 /* 2V5_VPP23 Max Voltage Unsigned 32b int (mV) */
#define CMS_2V5_VPP23_AVG_REG_OFFSET 0x03CC /* 2V5_VPP23 Average Voltage Unsigned 32b int (mV) */
#define CMS_2V5_VPP23_INS_REG_OFFSET 0x03D0 /* 2V5_VPP23 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_GTVCC_AUX_MAX_REG_OFFSET 0x03D4 /* GTVCC_AUX Max Voltage Unsigned 32b int (mV) */
#define CMS_GTVCC_AUX_AVG_REG_OFFSET 0x03D8 /* GTVCC_AUX Average Voltage Unsigned 32b int (mV) */
#define CMS_GTVCC_AUX_INS_REG_OFFSET 0x03DC /* GTVCC_AUX Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_HBM_1V2_I_MAX_REG_OFFSET 0x0410 /* HBM_1v2_I Max Current Unsigned 32b int (mA) */
#define CMS_HBM_1V2_I_AVG_REG_OFFSET 0x0414 /* HBM_1v2_I Average Current Unsigned 32b int (mA) */
#define CMS_HBM_1V2_I_INS_REG_OFFSET 0x0418 /* HBM_1v2_I Instantaneous Current Unsigned 32b int (mA) */
#define CMS_VCC1V5_MAX_REG_OFFSET 0x041C /* VCC1V5 Max Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V5_AVG_REG_OFFSET 0x0420 /* VCC1V5 Average Voltage Unsigned 32b int (mV) */
#define CMS_VCC1V5_INS_REG_OFFSET 0x0424 /* VCC1V5 Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVCC_MAX_REG_OFFSET 0x0428 /* MGTAVCC Max Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVCC_AVG_REG_OFFSET 0x042C /* MGTAVCC Average Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVCC_INS_REG_OFFSET 0x0430 /* MGTAVCC Instantaneous Voltage Unsigned 32b int (mV) */
#define CMS_MGTAVTT_I_MAX_REG_OFFSET 0x0434 /* MGTAVCC Max Current Unsigned 32b int (mA) */
#define CMS_MGTAVTT_I_AVG_REG_OFFSET 0x0438 /* MGTAVCC Average Current Unsigned 32b int (mA) */
#define CMS_MGTAVTT_I_INS_REG_OFFSET 0x043C /* MGTAVCC Instantaneous Current Unsigned 32b int (mA) */
#define CMS_MGTAVCC_I_MAX_REG_OFFSET 0x0440 /* MGTAVCC Max Current Unsigned 32b int (mA) */
#define CMS_MGTAVCC_I_AVG_REG_OFFSET 0x0444 /* MGTAVCC Average Current Unsigned 32b int (mA) */
#define CMS_MGTAVCC_I_INS_REG_OFFSET 0x0448 /* MGTAVCC Instantaneous Current Unsigned 32b int (mA) */

/* This is single bit sensor, rather than containing a measurement. It is supported for all Alveo cards */
#define CMS_POWER_GOOD_INS_REG_OFFSET 0x0370 /* Bits 31:1 - Reserved
                                                Bit 0 : 0 = Power Good, 1 = Power Bad */
#define CMS_POWER_GOOD_INS_REG_POWER_STATUS (1u << 0)


/* Definitions for mailbox */
#define CMS_MAILBOX_HEADER_WORD_OFFSET 0x0
#define CMS_MAILBOX_HEADER_OPCODE_MASK       0xFF000000
#define CMS_MAILBOX_HEADER_LENGTH_BYTES_MASK 0x00000FFF

#define CMS_OP_CARD_INFO_REQ_OPCODE             0x04
#define CMS_OP_READ_MODULE_LOW_SPEED_IO_OPCODE  0x0D
#define CMS_OP_WRITE_MODULE_LOW_SPEED_IO_OPCODE 0x0E

#define CMS_MAILBOX_PAYLOAD_START_OFFSET   0x4


/* Definition of the CMS build information.
 * While PG348 doesn't describe the build information addresses, the source code of loadsc linked to
 * https://adaptivesupport.amd.com/s/article/73654?language=en_US has these definitions and validated the value of
 * CMS_BUILD_INFO_VIV_ID_VERSION. */
#define CMS_BUILD_INFO_VIV_ID_VERSION      0x00000000
#define CMS_BUILD_INFO_MAJOR_MINOR_VERSION 0x00000004
#define CMS_BUILD_INFO_PATCH_CORE_REVISION 0x00000008
#define CMS_BUILD_INFO_PERFORCE_CL         0x0000000C
#define CMS_BUILD_INFO_RESERVED_TAG        0x00000010
#define CMS_BUILD_INFO_SCRATCH             0x00000014


#endif /* XILINX_CMS_HOST_INTERFACE_H_ */
