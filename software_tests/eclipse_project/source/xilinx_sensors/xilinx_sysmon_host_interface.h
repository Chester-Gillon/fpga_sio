/*
 * @file xilinx_sysmon_host_interface.h
 * @date 30 Jul 2024
 * @author Chester Gillon
 * @brief Defines the interface for the Xilinx "UltraScale Architecture System Monitor (SYSMON)", from the point of view of the host.
 * @details
 *  This is subset of register definitions used for user space access via VFIO.
 *  Details taken from https://docs.amd.com/v/u/en-US/pg185-system-management-wiz
 *
 *  Have not included the registers which are only in SYSMONE1 in UltraScale devices, since was written for UltraScale+ devices.
 */

#ifndef SYSMON_HOST_INTERFACE_H_
#define SYSMON_HOST_INTERFACE_H_

/* 10-bit Most Significant Bit (MSB) justified result of on-device temperature measurement is stored in this register. */
#define SYSMON_TEMPERATURE_OFFSET                     0x400

/* The 10-bit MSB justified result of on-device VCCINT supply monitor measurement is stored in this register. */
#define SYSMON_VCCINT_OFFSET                          0x404

/* The 10-bit MSB justified result of on-device VCCAUX Data supply monitor measurement is stored in this register */
#define SYSMON_VCCAUX_OFFSET                          0x408

/* When read: The 10-bit MSB justified result of A/D conversion on the dedicated analog input channel (Vp/Vn) is stored
 * in this register.
 *
 * When written: Write to this register resets the SYSMON hard macro. No specific data is required. */
#define SYSMON_VP_VN_OFFSET                           0x40C

/* The 10-bit MSB justified result of A/D conversion on the reference input VREFP is stored in this register */
#define SYSMON_VREFP_OFFSET                           0x410

/* The 10-bit MSB justified result of A/D conversion on the reference input VREFN is stored in this register */
#define SYSMON_VREFN_OFFSET                           0x414

/* The 10-bit MSB justified result of A/D conversion on the reference input VBRAM is stored in this register. */
#define SYSMON_VBRAM_OFFSET                           0x418

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 0 is stored in this register. */
#define SYSMON_VAUX0_OFFSET                           0x440

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 1 is stored in this register. */
#define SYSMON_VAUX1_OFFSET                           0x444

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 2 is stored in this register. */
#define SYSMON_VAUX2_OFFSET                           0x448

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 3 is stored in this register. */
#define SYSMON_VAUX3_OFFSET                           0x44C

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 4 is stored in this register. */
#define SYSMON_VAUX4_OFFSET                           0x450

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 5 is stored in this register. */
#define SYSMON_VAUX5_OFFSET                           0x454

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 6 is stored in this register. */
#define SYSMON_VAUX6_OFFSET                           0x458

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 7 is stored in this register. */
#define SYSMON_VAUX7_OFFSET                           0x45C

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 8 is stored in this register. */
#define SYSMON_VAUX8_OFFSET                           0x460

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 9 is stored in this register. */
#define SYSMON_VAUX9_OFFSET                           0x464

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 10 is stored in this register. */
#define SYSMON_VAUX10_OFFSET                          0x468

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 11 is stored in this register. */
#define SYSMON_VAUX11_OFFSET                          0x46C

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 12 is stored in this register. */
#define SYSMON_VAUX12_OFFSET                          0x470

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 13 is stored in this register. */
#define SYSMON_VAUX13_OFFSET                          0x474

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 14 is stored in this register. */
#define SYSMON_VAUX14_OFFSET                          0x478

/* The 10-bit MSB justified result of A/D conversion on the auxiliary analog input 15 is stored in this register. */
#define SYSMON_VAUX15_OFFSET                          0x47C

/* The 10-bit MSB justified maximum temperature measurement. */
#define SYSMON_MAX_TEMP_OFFSET                        0x480

/* The 10-bit MSB justified maximum VCCINT measurement. */
#define SYSMON_MAX_VCCINT_OFFSET                      0x484

/* The 10-bit MSB justified maximum VCCAUX measurement. */
#define SYSMON_MAX_VCCAUX_OFFSET                      0x488

/* The 10-bit MSB justified maximum VBRAM measurement */
#define SYSMON_MAX_VBRAM_OFFSET                       0x48C

/* The 10-bit MSB justified minimum temperature measurement. */
#define SYSMON_MIN_TEMP_OFFSET                        0x490

/* The 10-bit MSB justified minimum VCCINT measurement. */
#define SYSMON_MIN_VCCINT_OFFSET                      0x494

/* The 10-bit MSB justified minimum VCCAUX measurement. */
#define SYSMON_MIN_VCCAUX_OFFSET                      0x498

/* The 10-bit MSB justified minimum VBRAM measurement. */
#define SYSMON_MIN_VBRAM_OFFSET                       0x49C

/* The 16-bit register gives general status information of ALARM, Over Temperature (OT), disable information of SYSMON and
   information about whether the SYSMON is using internal reference voltage or external reference voltage. */
#define SYSMON_FLAG_REGISTER_OFFSET                   0x4FC

/* SYSMON configuration registers */
#define SYSMON_CONFIGURATION_REGISTER_0_OFFSET        0x500
#define SYSMON_CONFIGURATION_REGISTER_1_OFFSET        0x504
#define SYSMON_CONFIGURATION_REGISTER_2_OFFSET        0x508
#define SYSMON_CONFIGURATION_REGISTER_3_OFFSET        0x50C
#define SYSMON_CONFIGURATION_REGISTER_4_OFFSET        0x510

/* ADC channel selection, where a bit set means enabled */
#define SYSMON_CHANNEL_SELECTION_LOWER_OFFSET         0x520
#define SYSMON_CHANNEL_SELECTION_UPPER_OFFSET         0x524
#define SYSMON_CHANNEL_SELECTION_USER_OFFSET          0x518

/* ADC slow channel selection, where a bit set means enabled.
 * These addresses are not listed in PG185, and were determined by using the SYSMON Macro Register Addresses in UG580
 * and applying the "AXI4-Lite address mapping to Hard Macro Register Address" in PG185. */
#define SYSMON_SLOW_CHANNEL_SELECTION_LOWER_OFFSET    0x5E8
#define SYSMON_SLOW_CHANNEL_SELECTION_UPPER_OFFSET    0x5EC
#define SYSMON_SLOW_CHANNEL_SELECTION_USER_OFFSET     0x5F0

/* ADC channel averaging, where a bit set means enabled */
#define SYSMON_CHANNEL_AVERAGING_LOWER_OFFSET         0x528
#define SYSMON_CHANNEL_AVERAGING_UPPER_OFFSET         0x52C
#define SYSMON_CHANNEL_AVERAGING_USER_OFFSET          0x51C

/* ADC channel analog-input mode. Bit clear is unipolar, bit set is bipolar */
#define SYSMON_CHANNEL_ANALOG_INPUT_MODE_LOWER_OFFSET 0x530
#define SYSMON_CHANNEL_ANALOG_INPUT_MODE_UPPER_OFFSET 0x534

/* ADC channel acquisition time. Bit set increases the acquisition time. */
#define SYSMON_CHANNEL_ACQUISITION_TIME_LOWER_OFFSET  0x538
#define SYSMON_CHANNEL_ACQUISITION_TIME_UPPER_OFFSET  0x53C

/* The 10-bit MSB justified result of the on-chip V USER0 supply monitor measurement is stored at this location. */
#define SYSMON_VUSER0_OFFSET                          0x600

/* The 10-bit MSB justified result of the on-chip V USER1 supply monitor measurement is stored at this location. */
#define SYSMON_VUSER1_OFFSET                          0x604

/* The 10-bit MSB justified result of the on-chip V USER2 supply monitor measurement is stored at this location. */
#define SYSMON_VUSER2_OFFSET                          0x608

/* The 10-bit MSB justified result of the on-chip V USER3 supply monitor measurement is stored at this location. */
#define SYSMON_VUSER3_OFFSET                          0x60C

/* Maximum V USER0 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MAX_VUSER0_OFFSET                      0x680

/* Maximum V USER1 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MAX_VUSER1_OFFSET                      0x684

/* Maximum V USER2 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MAX_VUSER2_OFFSET                      0x688

/* Maximum V USER3 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MAX_VUSER3_OFFSET                      0x68C

/* Minimum V USER0 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MIN_VUSER0_OFFSET                      0x6A0

/* Minimum V USER1 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MIN_VUSER1_OFFSET                      0x6A4

/* Minimum V USER2 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MIN_VUSER2_OFFSET                      0x6A8

/* Minimum V USER3 measurement recorded since power-up or the last System Monitor reset. */
#define SYSMON_MIN_VUSER3_OFFSET                      0x6AC

/* Configuration register for the Analog Bus */
#define SYSMON_ANALOG_BUS_CONFIGURATION_OFFSET        0x514


/* Offset from the above registers for each SYSMON slave in a Stacked Silicon Interconnect (SSI) device */
#define SYSMON_PER_SLAVE_OFFSET 0x800

#endif /* SYSMON_HOST_INTERFACE_H_ */
