/*
 * @file xilinx_xadc_host_interface.h
 * @date 25 Jun 2023
 * @author Chester Gillon
 * @brief Defines the interface to the Xilinx "Analog-to-Digital Converter (XADC)", from the point of view of the host.
 * @details
 *  This is subset of register definitions used for user space access via VFIO.
 *  Details taken from https://www.xilinx.com/support/documentation/ip_documentation/xadc_wiz/v3_3/pg091-xadc-wiz.pdf
 *
 *  Have not included the registers which are only available on Zynq-7000 devices since was written for use with
 *  Artix-7 ot Kintex-7 devices.
 */

#ifndef XILINX_XADC_HOST_INTERFACE_H_
#define XILINX_XADC_HOST_INTERFACE_H_

/* The 12-bit Most Significant Bit (MSB) justified result of on-device temperature measurement is stored in this register. */
#define XADC_TEMPERATURE_OFFSET                         0x200

/* The 12-bit MSB justified result of on-device VCCINT supply monitor measurement is stored in this register. */
#define XADC_VCCINT_OFFSET                              0x204

/* The 12-bit MSB justified result of on-device VCCAUX Data supply monitor measurement is stored in this register. */
#define XADC_VCCAUX_OFFSET                              0x208

/* When read: The 12-bit MSB justified result of A/D conversion on the dedicated analog input channel (Vp/Vn) is stored in this
   register.

   When written: Write to this register resets the XADC hard macro. No specific data is required. */
#define XADC_VP_VN_OFFSET                               0x20C

/* The 12-bit MSB justified result of A/D conversion on the reference input VREFP is stored in this register. */
#define XADC_VREFP_OFFSET                               0x210

/* The 12-bit MSB justified result of A/D conversion on the reference input VREFN is stored in this register. */
#define XADC_VREFN_OFFSET                               0x214

/* The 12-bit MSB justified result of A/D conversion on the reference input VBRAM is stored in this register. */
#define XADC_VBRAM_OFFSET                               0x218

/* The calibration coefficient for the supply sensor offset of ADC A is stored in this register. */
#define XADC_SUPPLY_A_OFFSET_OFFSET                     0x220

/* The calibration coefficient for the ADC A offset calibration is stored in this register. */
#define XADC_ADC_A_OFFSET_OFFSET                        0x224

/* The calibration coefficient for the gain error of ADC A is stored in this register. */
#define XADC_ADC_A_GAIN_ERROR_OFFSET                    0x228

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 0 is stored in this register. */
#define XADC_VAUX0_OFFSET                               0x240

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 1 is stored in this register. */
#define XADC_VAUX1_OFFSET                               0x244

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 2 is stored in this register. */
#define XADC_VAUX2_OFFSET                               0x248

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 3 is stored in this register. */
#define XADC_VAUX3_OFFSET                               0x24C

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 4 is stored in this register. */
#define XADC_VAUX4_OFFSET                               0x250

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 5 is stored in this register. */
#define XADC_VAUX5_OFFSET                               0x254

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 6 is stored in this register. */
#define XADC_VAUX6_OFFSET                               0x258

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 7 is stored in this register. */
#define XADC_VAUX7_OFFSET                               0x25C

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 8 is stored in this register. */
#define XADC_VAUX8_OFFSET                               0x260

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 9 is stored in this register. */
#define XADC_VAUX9_OFFSET                               0x264

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 10 is stored in this register. */
#define XADC_VAUX10_OFFSET                              0x268

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 11 is stored in this register. */
#define XADC_VAUX11_OFFSET                              0x26C

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 12 is stored in this register. */
#define XADC_VAUX12_OFFSET                              0x270

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 13 is stored in this register. */
#define XADC_VAUX13_OFFSET                              0x274

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 14 is stored in this register. */
#define XADC_VAUX14_OFFSET                              0x278

/* The 12-bit MSB justified result of A/D conversion on the auxiliary analog input 15 is stored in this register. */
#define XADC_VAUX15_OFFSET                              0x27C

/* The 12-bit MSB justified maximum temperature measurement. */
#define XADC_MAX_TEMP_OFFSET                            0x280

/* The 12-bit MSB justified maximum VCCINT measurement. */
#define XADC_MAX_VCCINT_OFFSET                          0x284

/* The 12-bit MSB justified maximum VCCAUX measurement. */
#define XADC_MAX_VCCAUX_OFFSET                          0x288

/* The 12-bit MSB justified maximum VBRAM measurement. */
#define XADC_MAX_VBRAM_OFFSET                           0x28C

/* The 12-bit MSB justified minimum temperature measurement */
#define XADC_MIN_TEMP_OFFSET                            0x290

/* The 12-bit MSB justified minimum VCCINT measurement */
#define XADC_MIN_VCCINT_OFFSET                          0x294

/* The 12-bit MSB justified minimum VCCAUX measurement. */
#define XADC_MIN_VCCAUX_OFFSET                          0x298

/* The 12-bit MSB justified minimum VBRAM measurement. */
#define XADC_MIN_VBRAM_OFFSET                           0x29C

/* The calibration coefficient for the supply sensor offset of ADC B is stored in this register. */
#define XADC_SUPPLY_B_OFFSET_OFFSET                     0x2C0

/* The calibration coefficient for the ADC B offset calibration is stored in this register. */
#define XADC_ADC_B_OFFSET_OFFSET                        0x2C4

/* The calibration coefficient for the ADC B gain error is stored in this register. */
#define XADC_ADC_B_GAIN_ERROR_OFFSET                    0x2C8

/* XADC Configuration Registers */
#define XADC_CONFIGURATION_REGISTER_0_OFFSET            0x300
#define XADC_CONFIGURATION_REGISTER_1_OFFSET            0x304
#define XADC_CONFIGURATION_REGISTER_2_OFFSET            0x308

/* ADC channel selection, where a bit set means enabled */
#define XADC_CHANNEL_SELECTION_LOWER_OFFSET             0x320
#define XADC_CHANNEL_SELECTION_UPPER_OFFSET             0x324

/* ADC channel analog-input mode. Bit clear is unipolar, bit set is bipolar */
#define XADC_CHANNEL_ANALOG_INPUT_MODE_LOWER_OFFSET     0x330
#define XADC_CHANNEL_ANALOG_INPUT_MODE_UPPER_OFFSET     0x334

/* ADC channel acquisition time. Bit set increases the acquisition time. */
#define XADC_CHANNEL_ACQUISITION_TIME_LOWER_OFFSET      0x338
#define XADC_CHANNEL_ACQUISITION_TIME_UPPER_OFFSET      0x33C

#endif /* XILINX_XADC_HOST_INTERFACE_H_ */
