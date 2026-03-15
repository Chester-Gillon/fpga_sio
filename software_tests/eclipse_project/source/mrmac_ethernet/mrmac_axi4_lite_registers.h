/*
 * @file mrmac_axi4_lite_registers.h
 * @date 22 Feb 2026
 * @author Chester Gillon
 * @brief Defines the registers for the MRMAC Configuration registers, Status registers, and Statistics counters.
 * @details
 *   This file was created using the contents of mrmac-registers-v3-0.xlsx from
 *   https://download.amd.com/docnav/documents/ip_attachments/mrmac-registers-v3-0.zip
 *
 *   This only defines the sub-set of the registers used by the programs, and haven't attempted to define all the registers in the
 *   source spreadsheet.
 *
 *   Apart from MRMAC_CONFIGURATION_REVISION_REG_OFFSET assume all other registers apply to all ports, albeit haven't checked the
 *   spreadsheet to confirm this.
 *
 *   Have used the term "port" rather than "client", since while PG314 used both terms port is used the most.
 */

#ifndef MRMAC_AXI4_LITE_REGISTERS_H_
#define MRMAC_AXI4_LITE_REGISTERS_H_

#include "vfio_bitops.h"

/* Current hardware version of the core. This register isn't port specific. */
#define MRMAC_CONFIGURATION_REVISION_REG_OFFSET 0x0000


/* The frame size of the registers for each port. The following register are defined once, but are applicable to all ports at
 * an offset of the zero-based port number multiplied by the MRMAC_PORT_REGS_FRAME_SIZE. */
#define MRMAC_PORT_REGS_FRAME_SIZE 0x1000


#define MRMAC_MODE_REG_OFFSET  0x0008

/* Port Data Rate. For details please refer Table 2: Port Data Rate. */
#define MRMAC_CTL_DATA_RATE_MASK VFIO_GENMASK_U32 (2, 0) /* RW DRP */
#define MRMAC_CTL_DATA_RATE_10GE  0x0
#define MRMAC_CTL_DATA_RATE_25GE  0x1
#define MRMAC_CTL_DATA_RATE_40GE  0x2
#define MRMAC_CTL_DATA_RATE_50GE  0x3
#define MRMAC_CTL_DATA_RATE_100GE 0x4

/* Port Serdes width. For details please refer Table 7: GT Quad Operating Modes.
 * Haven't defined the values for this, since the meaning depends upon the port data rate. */
#define MRMAC_CTL_SERDES_WIDTH_MASK VFIO_GENMASK_U32 (6, 4) /* RW DRP */

/* Port AXI4-Stream Mode. For details please refer Table 4: MRMAC AXI4-Stream Modes.
 * Haven't defined the values for this, since the meaning depends upon the port data rate. */
#define MRMAC_CTL_AXIS_CFG_MASK VFIO_GENMASK_U32 (11, 9) /* RW DRP */


#define MRMAC_CONFIGURATION_RX_MTU_OFFSET 0x0014

/* Port Minimum Packet Length . Any packet shorter than the default value of 64 (decimal) is considered to be undersized.
 * The value of this register must be less than or equal to the value of CTL_RX_MAX_PACKET_LEN[14:0]. */
#define MRMAC_CTL_RX_MIN_PACKET_LEN_MASK VFIO_GENMASK_U32 (7, 0) /* RW DRP */

/* Port Maximum Packet Length. Any packet longer than this value is considered to be oversized.
 * The allowed value for this register can range from 64 to 16,383. */
#define MRMAC_CTL_RX_MAX_PACKET_LEN_MASK VFIO_GENMASK_U32 (30, 16) /* RW DRP */


#define MRMAC_FEC_CONFIGURATION_REG1_OFFSET 0x00D0

/* Port FEC operating mode. For details please refer PG314 Table 6: FEC Operating Modes.
 * Haven't defined the values for this, since the meaning depends upon the port data rate. */
#define MRMAC_CTL_FEC_MODE_MASK VFIO_GENMASK_U32 (3, 0) /* RW DRP */


#define MRMAC_CONFIGURATION_TX_REG1_OFFSET 0x100C

#define MRMAC_CTL_TX_ENABLE VFIO_BIT (0) /* Port TX Enable.
                                            This bit is used to enable the transmission of data when set to 1.
                                            When set to 0, only idles are transmitted by the MRMAC core.
                                            This bit should not be set to 1 until the receiver it is sending data to
                                            that is, the receiver in the other device) is fully aligned and ready to receive data
                                            (that is, the other device is not sending a remote fault condition).
                                            Otherwise, loss of data can occur.
                                            If this bit is set to 0 while a packet is being transmitted, the current packet
                                            transmission is completed and then the MRMAC core stops transmitting any more packets. */
#define MRMAC_CTL_TX_FCS_INS_ENABLE VFIO_BIT (1) /* Port  FCS insertion Enable.
                                                    Enable FCS insertion by the TX core.
                                                    1 : MRMAC core calculates and adds FCS to the packet.
                                                    0 : MRMAC core does not add FCS to the packet. */
#define MRMAC_CTL_TX_IGNORE_FCS VFIO_BIT (2) /* Port FCS error checking Enable.
                                                Enable FCS error checking at the AXI4-S interface by the TX core.
                                                This input only has effect when ctl_tx_fcs_ins_enable is 0.
                                                1 : A packet with bad FCS transmitted is binned as good.
                                                0 : A packet with bad FCS transmitted is not binned as good.
                                                The error is flagged on the registers STAT_TX_BAD_FCS, and the packet is transmitted
                                                as it was received. Statistics are reported as if there was no FCS error. */
#define MRMAC_CTL_TX_SEND_LFI VFIO_BIT (3) /* Port Transmit Local Fault Indication (LFI) code word.
                                              If this bit is set as a 1, the TX path only transmits Local Fault code words. */
#define MRMAC_CTL_TX_SEND_RFI VFIO_BIT (4) /* Port Transmit Remote Fault Indication (RFI) code word.
                                              If this bit is set as a 1, the TX path only transmits Remote Fault code words.
                                              This bit should be set to 1 until the RX path is fully aligned and is ready to accept
                                              data from the link partner. */
#define MRMAC_CTL_TX_SEND_IDLE VFIO_BIT (5) /* Port Transmit Idle code words.
                                               If this bit is sampled as a 1, the TX path only transmits Idle code words.
                                               This bit should be set to 1 when the partner device is sending
                                               Remote Fault Indication (RFI) code words. */
#define MRMAC_CTL_TX_IPG_VALUE VFIO_GENMASK_U32 (11,8) /* Port TX IPG value.
                                                          The ctl_tx_ipg_value defines the target average minimum
                                                          Inter Packet Gap (IPG, in bytes) inserted between AXI4-S packets.
                                                          Valid values are 8 to 12. The ctl_tx_ipg_value can also be programmed to
                                                          a value in the 0 to 7 range, but in that case, it is interpreted as
                                                          meaning minimal IPG, so only Terminate code word IPG is inserted;
                                                          no Idles are ever added in that case - and that produces an average IPG
                                                          of around 4 bytes when random-size packets are transmitted. */
#define MRMAC_CTL_TX_TEST_PATTERN VFIO_BIT (12) /* Port TX Test pattern generation enable for the TX core.
                                                   A value of 1 enables test mode as defined in Clause 49.
                                                   Corresponds to MDIO register bit 3.42.7 as defined in Clause 45.
                                                   Generates a scrambled idle pattern. */
#define MRMAC_CTL_TX_TEST_PATTERN_ENABLE VFIO_BIT (13) /* Port TX Test pattern enable for the TX core.
                                                          A value of 1 enables test mode.
                                                          Corresponds to MDIO register bit 3.42.2 as defined in Clause 45. */
#define MRMAC_CTL_TX_TEST_PATTERN_SELECT VFIO_BIT (14) /* Port TX Test pattern select.
                                                          Corresponds to MDIO register bit 3.42.1 as defined in Clause 45. */
#define MRMAC_CTL_TX_DATA_PATTERN_SELECT VFIO_BIT (15) /* Port TX Data pattern select.
                                                          Corresponds to MDIO register bit 3.42.0 as defined in Clause 45. */
#define MRMAC_CTL_TX_CUSTOM_PREAMBLE_ENABLE VFIO_BIT (16) /* Port TX Custom Preamble Enable.
                                                             A value of 1 enables the use of tx_preamblein as a custom preamble
                                                             instead of inserting a standard preamble. */
#define MRMAC_CTL_TX_CORRUPT_FCS_ON_ERR VFIO_GENMASK_U32 (22,21) /* Port TX Corrupt fcs on error.

                                                                    See "Aborting a Transmission" in PG314 for the encoding. */
#define MRMAC_CTL_TX_FLEXIF_SELECT VFIO_GENMASK_U32 (26,24) /* Port Flex Interface Transmit Operating Modes.
                                                               For details please refer PG314 Table 11: Flex Interface Transmit
                                                               Operating Modes */
#define MRMAC_CTL_TX_FLEXIF_INPUT_ENABLE VFIO_BIT (27) /* Port TX Flex Interface input enable.
                                                          A value of 1 enables Flex I/F input for the TX direction (disabling the
                                                          AXI4-Stream input interface) */


/* Transmit status registers:
 * - MRMAC_STAT_TX_STATUS_REG1_OFFSET is the latched version, and the comments indicate if bits latch high/low
 * - MRMAC_STAT_TX_RT_STATUS_REG1_OFFET is the real-time version */
#define MRMAC_STAT_TX_STATUS_REG1_OFFSET   0x0740
#define MRMAC_STAT_TX_RT_STATUS_REG1_OFFET 0x0748

#define MRMAC_STAT_TX_LOCAL_FAULT_MASK VFIO_BIT (0) /* LH W1C   Port TX local fault.
                                                                A value of 1 indicates the transmit decoder state machine is in the
                                                                TX_INIT state. */
#define MRMAC_STAT_TX_AXIS_UNF_MASK VFIO_BIT (7) /* LH W1C   Port TX axis underflow.
                                                             A value of 1 indicates that the AXI4-Stream interface has experienced an
                                                             underflow. */
#define MRMAC_STAT_TX_AXIS_ERR_MASK VFIO_BIT (8) /* LH W1C   Port TX axis error.
                                                             A value of 1 indicates that the AXI4-Stream interface has encountered an
                                                             error. */
#define MRMAC_STAT_TX_FLEXIF_ERR_MASK VFIO_BIT (9) /* LH W1C   Port TX flexif error.
                                                               A value of 1 indicates that the TX Flex I/F has encountered an error. */
#define MRMAC_STAT_TX_PCS_BAD_CODE_MASK VFIO_BIT (10) /* LH W1C   Port TX pcs bad code.
                                                                  A value of 1 indicates that bad PCS code was observed. */
#define MRMAC_STAT_TX_CL82_49_CONVERT_ERR_MASK VFIO_BIT (11) /* LH W1C    Port TX CL82 CL49 convert error. */
#define MRMAC_STAT_TX_FLEX_FIFO_OVF_MASK VFIO_BIT (12) /* LH W1C   Port TX flex fifo overflow.
                                                                   A value of 1 indicates that the Flex I/F FIFO has overflowed. */
#define MRMAC_STAT_TX_FLEX_FIFO_UDF_MASK VFIO_BIT (13) /* LH W1C   Port TX flex fifo underflow.
                                                                   A value of 1 indicates that the Flex I/F FIFO has underflowed. */


/* Receive status registers:
 * - MRMAC_STAT_RX_STATUS_REG1_OFFSET is the latched version, and the comments indicate if bits latch high/low
 * - MRMAC_STAT_RX_RT_STATUS_REG1_OFFSET is the real-time version */
#define MRMAC_STAT_RX_STATUS_REG1_OFFSET    0x0744
#define MRMAC_STAT_RX_RT_STATUS_REG1_OFFSET 0x074C

#define MRMAC_STAT_RX_STATUS_MASK VFIO_BIT (0) /* LL W1C   Port RX Status.
                                                           A value of 1 indicates the PCS is aligned and not in hi_ber state.
                                                           Corresponds to MDIO register bit 3.32.12 as defined in Clause 82.3. */
#define MRMAC_STAT_RX_BLOCK_LOCK_MASK VFIO_BIT (1) /* LL W1C   Port RX Block Lock */
#define MRMAC_STAT_RX_ALIGNED_MASK VFIO_BIT (2) /* LL W1C   Port RX aligned.
                                                            When stat_rx_aligned is a value of 1, all of the lanes are aligned/deskewed
                                                            and the receiver is ready to receive */
#define MRMAC_STAT_RX_MISALIGNED_MASK VFIO_BIT (3) /* LH W1C   Port RX misaligned.
                                                               When stat_rx_misaligned is a value of 1, a valid PCS Lane Marker Word
                                                               was not received on all PCS lanes simultaneously. */
#define MRMAC_STAT_RX_ALIGNED_ERR_MASK VFIO_BIT (4) /* LH W1C   Port RX aligned error.
                                                                When stat_rx_aligned_err is a value of 1, either Lane alignment failed
                                                                after several attempts, or Lane alignment was lost. */
#define MRMAC_STAT_RX_HI_BER_MASK VFIO_BIT (5) /* LH W1C   Port RX High BER.
                                                           When set to 1, the BER is too high as defined by IEEE Std 802.3.
                                                           Corresponds to MDIO register bit 3.32.1 as defined in Clause 82.3. */
#define MRMAC_STAT_RX_REMOTE_FAULT_MASK VFIO_BIT (6) /* LH W1C   Port RX remote fault.
                                                                 1: It indicates a remote fault condition was detected.
                                                                 0: A remote fault condition does not exist. */
#define MRMAC_STAT_RX_LOCAL_FAULT_MASK VFIO_BIT (7) /* LH W1C   Port RX local fault.
                                                                1: It indicates stat_rx_internal_local_fault or
                                                                   stat_rx_received_local_fault is asserted.
                                                                0 : A local fault condition does not exist. */
#define MRMAC_STAT_RX_INTERNAL_LOCAL_FAULT_MASK VFIO_BIT (8) /* LH W1C   Port RX internal local fault.
                                                                         1: It indicates a remote fault condition was detected.
                                                                         0: A remote fault condition does not exist. */
#define MRMAC_STAT_RX_RECEIVED_LOCAL_FAULT_MASK VFIO_BIT (9) /* LH W1C   Port RX received local fault.
                                                                         1: when enough local fault words are received from the link
                                                                            partner to trigger a fault condition as specified by the
                                                                            IEEE fault state machine.
                                                                         0: A received local fault condition does not exist. */
#define MRMAC_STAT_RX_BAD_CODEMASK VFIO_BIT (10) /* LH W1C   Port RX bad code.
                                                             Indicates 64B/66B code violations. This register indicates that the
                                                             RX PCS receive state machine is in the RX_E state as specified by the
                                                             IEEE Std 802.3. MDIO register 3.33:7:0 as defined in Clause 82.3. */
#define MRMAC_STAT_RX_BAD_PREAMBLE_MASK VFIO_BIT (11) /* LH W1C   Port RX bad preamble.
                                                                  This register indicates if the Ethernet packet received was preceded
                                                                  by a valid preamble. A value of 1 indicates that an invalid preamble
                                                                  was received. */
#define MRMAC_STAT_RX_BAD_SFD_MASK VFIO_BIT (12) /* LH W1C   Port RX bad SFD.
                                                             This register indicates if the Ethernet packet received was preceded by a
                                                             valid SFD. A value of 1 indicates that an invalid SFD was received. */
#define MRMAC_STAT_RX_GOT_SIGNAL_OS_MASK VFIO_BIT (13) /* LH W1C   Port RX got signal ordered set.
                                                                   Indicates that a Signal Ordered Set was received. */
#define MRMAC_STAT_RX_FLEXIF_ERR_MASK VFIO_BIT (14) /* LH W1C   Port RX flex if error.
                                                                Indicates that a Flex I/F Error has occurred and the data and flags on
                                                                the RX Flex I/F is in error. */
#define MRMAC_STAT_RX_FRAMING_ERR_MASK VFIO_BIT (15) /* LH W1C   Port RX Framing Error */
#define MRMAC_STAT_RX_SYNCED_MASK VFIO_BIT (16) /* LL W1C   Port RX Synced */
#define MRMAC_STAT_RX_SYNCED_ERR_MASK VFIO_BIT (17) /* LH W1C  Port RX Synced Error */
#define MRMAC_STAT_RX_BIP_ERR_MASK VFIO_BIT (18) /* LH W1C   Port RX BIP Error */
#define MRMAC_STAT_RX_CL49_82_CONVERT_ERR_MASK VFIO_BIT (19) /* LH W1C   Port RX CL49_82 convert error */
#define MRMAC_STAT_RX_PCS_BAD_CODE_MASK VFIO_BIT (20) /* LH W1C   Port RX pcs bad code.
                                                                  Indicates that PCS decoder received a malformed 66b code word,
                                                                  or improper code word, and transitioned to the E state. */
#define MRMAC_STAT_RX_AXIS_FIFO_OVERFLOW_MASK VFIO_BIT (21) /* LH W1C   Port RX AXIS fifo overflow.
                                                                        A value of 1 indicates that the AXI4-Stream interface has
                                                                        experienced an overflow. */
#define MCMAC_STAT_RX_AXIS_ERR_MASK VFIO_BIT (22) /* LH W1C   Port RX AXIS error.
                                                              A value of 1 indicates that the AXI4-Stream interface has experienced
                                                              an error. */
#define MRMAC_STAT_RX_INVALID_START_MASK VFIO_BIT (23) /* LH W1C   Port RX invalid start */
#define MRMAC_STAT_RX_FLEX_FIFO_OVF_MASK VFIO_BIT (24) /* LH W1C   Port RX flex fifo overflow.
                                                                   A value of 1 indicates that the Flex I/F experienced an overflow */
#define MRMAC_STAT_RX_FLEX_FIFO_UDF_MASK VFIO_BIT (25) /* LH W1C   Port RX flex fifo underflow.
                                                                   A value of 1 indicates that the Flex I/F experienced an underflow */


#endif /* MRMAC_AXI4_LITE_REGISTERS_H_ */
