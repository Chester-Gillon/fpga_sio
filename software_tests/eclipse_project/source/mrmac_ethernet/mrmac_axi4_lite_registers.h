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
 *   Have used the term "port" rather than "client", since while PG314 used both terms port is used the most.
 *
 *   When initially created the file used only copied the definitions for the port 0 registers in mrmac-registers-v3-0.xlsx, and
 *   removed the port 0 suffix from the names. The assumption was that the registers can be read for all ports and return
 *   "sensible" results.
 *
 *   Subsequently found the statistics counters differed between the ports, and changed the statistics counter registers to be
 *   per-port values.
 *
 *   In mrmac-registers-v3-0.xlsx there are some lane or FEC slice specific registers which are defined only in some ports,
 *   where the number of lanes / FEC slices depends upon the port data rate. Some examples are:
 *   - STAT_RX_FEC_MAP_REG1_0 for port 0, has fields for lanes 0..3
 *   - STAT_RX_FEC_MAP_REG1_2 for port 2, has fields for lanes 2..3
 */
#ifndef MRMAC_AXI4_LITE_REGISTERS_H_
#define MRMAC_AXI4_LITE_REGISTERS_H_

#include "vfio_bitops.h"

/* Current hardware version of the core. This register isn't port specific. */
#define MRMAC_CONFIGURATION_REVISION_REG_OFFSET 0x0000


/* The frame size of the registers for each port. Unless specified otherwise, the following registers are defined once,
 * at an offset of the zero-based port number multiplied by the MRMAC_PORT_REGS_FRAME_SIZE. */
#define MRMAC_PORT_REGS_FRAME_SIZE 0x1000


#define MRMAC_RESET_REG_OFFSET 0x0004

#define MRMAC_RX_SERDES_RESET VFIO_GENMASK_U32 (3, 0) /* RW Port RX Serdes reset. A write of 1 puts RX PCS lane logic into reset.
                                                         This is a per-lane reset, where the number of lanes depends upon the
                                                         port configuration. */
#define MRMAC_TX_SERDES_RESET VFIO_BIT (4) /* RW Port TX Serdes reset. A write of 1 puts TX PCS lane logic into reset. */
#define MRMAC_RX_RESET VFIO_BIT (5) /* RW Port RX core reset. A write of 1 puts the RX path in reset. */
#define MRMAC_TX_RESET VFIO_BIT (6) /* RW Port TX core reset. A write of 1 puts the TX path in reset. */
#define MRMAC_RX_FLEXIF_RESET VFIO_BIT (7) /* RW Port RX Flex IF reset. A write of 1 puts the RX Flex IF path in reset. */
#define MRMAC_RX_AXI_RESET VFIO_BIT (8) /* RW Port RX AXI4-Stream reset. A write of 1 puts the RX AXI4-Stream path in reset. */
#define MRMAC_TX_AXI_RESET VFIO_BIT (9) /* RW Port TX AXI4-Stream reset. A write of 1 puts the TX AXI4-Stream path in reset. */


#define MRMAC_MODE_REG_OFFSET  0x0008

/* Port Data Rate. For details please refer Table 2: Port Data Rate. */
#define MRMAC_CTL_DATA_RATE_MASK VFIO_GENMASK_U32 (2, 0) /* RW DRP */
#define MRMAC_CTL_DATA_RATE_ARRAY_SIZE 8
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

#define MRMAC_CTL_PCS_RX_TS_EN VFIO_BIT (12) /* RW DRP Port Enable PCS RX timestamp */
#define MRMAC_CTL_CUSTOM_TX_AMS VFIO_BIT (13) /* RW DRP Port Enable Custom TX ams.
                                                    Ctl_custom_tx_ams_N (N=0-3): When set to ‘1’, the ctl_custom_tx_ams_N control bit
                                                    allows user-defined alignment marker (AM) values to be injected for client N
                                                    frame alignment (configured through ctl_tx_vl_marker_idM, where M=0-19)
                                                    instead of standard-specified values hard-coded within the MRMAC.
                                                    Note that there is only one set of configurable AMs, but any number of clients
                                                    can be configured to use the custom AM values at the same time. */
#define MRMAC_CTL_CUSTOM_RX_AMS VFIO_BIT (14) /* RW DRP Port Enable Custom RX ams.
                                                    Ctl_custom_rx_ams_N (N=0-3): When set to ‘1’, the ctl_custom_rx_ams_N control bit
                                                    allows user-defined alignment marker (AM) values to be used for client N
                                                    frame alignment (configured through ctl_rx_vl_marker_idM, where M=0-19)
                                                    instead of standard-specified values hard-coded within the MRMAC.
                                                    Note that there is only one set of configurable AMs, but any number of clients
                                                    can be configured to use the custom AM values at the same time. */
#define MRMAC_CTL_COUNTER_EXTEND VFIO_BIT (15) /* RW DRP Extend Counter */
#define MRMAC_CTL_SERDES_PASSTHRU VFIO_BIT (16) /* RW DRP Port 0 Serdes Passthrough.
                                                      Ctl_serdes_passthru_N (N=0-3) : This is a reserved internal feature,
                                                      not fully tested, and not to be advertised to customers.
                                                      It should be held to 0 always. */
#define MRMAC_TICK_REG_MODE_SEL VFIO_BIT (30) /* RW DRP Tick Reg mode Select */


#define MRMAC_CONFIGURATION_RX_MTU_OFFSET 0x0014

/* Port Minimum Packet Length . Any packet shorter than the default value of 64 (decimal) is considered to be undersized.
 * The value of this register must be less than or equal to the value of CTL_RX_MAX_PACKET_LEN[14:0]. */
#define MRMAC_CTL_RX_MIN_PACKET_LEN_MASK VFIO_GENMASK_U32 (7, 0) /* RW DRP */

/* Port Maximum Packet Length. Any packet longer than this value is considered to be oversized.
 * The allowed value for this register can range from 64 to 16,383. */
#define MRMAC_CTL_RX_MAX_PACKET_LEN_MASK VFIO_GENMASK_U32 (30, 16) /* RW DRP */


/* For requesting a software statistics sample tick */
#define MRMAC_TICK_REG_OFFSET 0x002C
#define MRMAC_TICK_REG VFIO_BIT (0) /* RW  Port Tick Register.
                                           Writing a 1 to the Tick bit will trigger a snapshot of all the Statistics counters into
                                           their readable registers.
                                           The bit self-clears, thus only a single write is required by the user input. */


#define MRMAC_FEC_CONFIGURATION_REG1_OFFSET 0x00D0

#define MRMAC_CTL_FEC_MODE_MASK VFIO_GENMASK_U32 (3, 0) /* RW DRP Port FEC operating mode.
                                                           For details please refer PG314 Table 6: FEC Operating Modes.
                                                           Haven't defined the values for this, since the meaning depends upon
                                                           the port data rate*/
#define MRMAC_CTL_FEC_MODE_ARRAY_SIZE 16
#define MRMAC_CTL_RX_FEC_BYPASS_INDICATION VFIO_BIT (4) /* RW DRP Port RSFEC Bypass Indication. Set to ‘1’ to bypass error indication. */
#define MRMAC_CTL_RX_FEC_BYPASS_CORRECTION VFIO_BIT (5) /* RW DRP Port RSFEC Bypass Correction. RS-FEC correction enable. */
#define MRMAC_CTL_RX_FEC_TRANSCODE_CLAUSE49 VFIO_BIT (6) /* RW DRP Port RSFEC transcode caluse49.
                                                            This flag should be set to 1 for modes which require Clause 49 transcoding
                                                            (25GE, 50GE, Fibre Channel) and set to 0 for modes which require
                                                            Clause 82 transcoding (100GE). */
#define MRMAC_CTL_RX_FEC_ALIGNMENT_BYPASS VFIO_BIT (7) /* RW DRP Port RSFEC alignment Bypass */
#define MRMAC_CTL_TX_FEC_TRANSCODE_BYPASS VFIO_BIT (8) /* RW DRP Port RSFEC tx transcode Bypass. Transcoder bypass mode enable. */
#define MRMAC_CTL_RX_FEC_TRANSCODE_BYPASS VFIO_BIT (9) /* RW DRP Port RSFEC rx transcode Bypass. Transcoder bypass mode enable */
#define MRMAC_CTL_RX_FEC_CDC_BYPASS VFIO_BIT (10) /* RW DRP Port RSFEC CDC Bypass.
                                                     It determines whether the CDC buffers in the FEC alignment logic are enabled (0)
                                                     or bypassed (1). By default, they are enabled. If CDC has already been performed
                                                     outside the FEC (e.g. using the GT elastic buffers, or in the fabric) then the
                                                     clock inputs for all the lanes will be identical (same frequency and phase) and
                                                     the CDC buffers can safely be bypassed which reduces latency (by ~12ns or so).
                                                     Otherwise, if each lane has its own recovered clock then the buffers should be
                                                     enabled. This applies to all multi-lane RS-FEC modes, i.e. 50G and 100G,
                                                     including FlexO. It’s ignored in 10G, 25G and 40G configurations. */
#define MRMAC_CTL_RX_FEC_ERRIND_MODE VFIO_BIT (11) /* RW DRP Port RSFEC error ind mode.
                                                      RS-FEC error indication mode (1=IEEE compliant mode,
                                                       0=allow simultaneous correction and indication bypass) */
#define MRMAC_CTL_TX_FEC_FOUR_LANE_PMD VFIO_BIT (12) /* RW DRP Port RSFEC tx four lane PMD.
                                                        This needs to set to 1 for 100CAUI-4 with RSFEC (it should be set to 0
                                                        100GAUI-2 and other modes). */


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


/* Receive FEC status registers:
 * - MRMAC_STAT_RX_FEC_STATUS_REG_OFFSET is the latched version, and the comments indicate if bits latch high/low
 * - MRMAC_STAT_RX_FEC_RT_STATUS_REG_OFFSET is the real-time version */
#define MRMAC_STAT_RX_FEC_STATUS_REG_OFFSET    0x0784
#define MRMAC_STAT_RX_FEC_RT_STATUS_REG_OFFSET 0x0788

#define MRMAC_STAT_RX_FEC_ALIGNED VFIO_BIT (0) /* LL W1C Indicates that the FEC is aligned/deskewed and ready to receive packet data. */
#define MRMAC_STAT_RX_FEC_HI_SER VFIO_BIT (1) /* LH W1C Indicates that the number of symbol errors in a 8192-codeword window has
                                                        exceeded the threshold K (417). */
#define MRMAC_STAT_RX_FEC_LANE_LOCK VFIO_GENMASK_U32 (7,4) /* LL W1C Indicates that the FEC has achieved lane lock on the indicated lane. */


/* Transmit FEC status registers:
 * - MRMAC_STAT_TX_FEC_STATUS_REG_OFFSET is the latched version, and the comments indicate if bits latch high/low
 * - MRMAC_STAT_TX_FEC_RT_STATUS_REG_OFFSET is the real-time version */
#define MRMAC_STAT_TX_FEC_STATUS_REG_OFFSET    0x0798
#define MRMAC_STAT_TX_FEC_RT_STATUS_REG_OFFSET 0x079C

#define MRMAC_STAT_TX_FEC_PCS_LANE_ALIGN VFIO_BIT (0) /* LL W1C Indicates that all the transmit FEC lanes are aligned/ deskewed
                                                                and ready to transmit data. */
#define MRMAC_STAT_TX_FEC_PCS_BLOCK_LOCK VFIO_GENMASK_U32 (5,1) /* LL W1C Indicates that the PCS has achieved block lock on the
                                                                          corresponding lanes. */
#define MRMAC_STAT_TX_FEC_PCS_AM_LOCK VFIO_GENMASK_U32 (10,6) /* W1C Indicates that all of the PCS lanes have achieved
                                                                     Alignment Marker lock. */


/* Statistics ready */
#define MRMAC_STAT_STATISTICS_READY_OFFSET 0x07D8
#define MRMAC_STAT_STATISTICS_READY VFIO_GENMASK_U32 (1,0) /* W1C After issuing a tick to sample registers can be polled until it
                                                                  reads 0x3, which indicates the statistics are ready for reading.
                                                                  Bit 1 indicates that rx statistics are ready,
                                                                  bit 0 indicates tx statistics are ready. */

/* Statistic counters.
 *
 * These are the port specific offsets from the start of the MRMAC register space. Not all ports have the same statistics counters,
 * so can't have one set of offsets based upon MRMAC_PORT_REGS_FRAME_SIZE. The names are from mrmac-registers-v3-0.xlsx, but with
 * the addition of a MRMAC_ prefix.
 *
 * Defines only the LSB registers, since all MSB registers are at an offset of 4 more than the LSB registers.
 * Checking the port 0 registers in mrmac-registers-v3-0.xlsx shows:
 * - All LSB registers have 32-bits
 * - All MSB registers have bits 15:0
 * - So, 48 bits for each histogram register. Which matches the "Statistics Monitoring" section in PG314.
 */
#define MRMAC_STAT_TX_CYCLE_COUNT_0_LSB 0x0800
#define MRMAC_STAT_TX_FRAME_ERROR_0_LSB 0x0808
#define MRMAC_STAT_TX_TOTAL_PACKETS_0_LSB   0x0818
#define MRMAC_STAT_TX_TOTAL_GOOD_PACKETS_0_LSB  0x0820
#define MRMAC_STAT_TX_TOTAL_BYTES_0_LSB 0x0828
#define MRMAC_STAT_TX_TOTAL_GOOD_BYTES_0_LSB    0x0830
#define MRMAC_STAT_TX_PACKET_64_BYTES_0_LSB 0x0838
#define MRMAC_STAT_TX_PACKET_65_127_BYTES_0_LSB 0x0840
#define MRMAC_STAT_TX_PACKET_128_255_BYTES_0_LSB    0x0848
#define MRMAC_STAT_TX_PACKET_256_511_BYTES_0_LSB    0x0850
#define MRMAC_STAT_TX_PACKET_512_1023_BYTES_0_LSB   0x0858
#define MRMAC_STAT_TX_PACKET_1024_1518_BYTES_0_LSB  0x0860
#define MRMAC_STAT_TX_PACKET_1519_1522_BYTES_0_LSB  0x0868
#define MRMAC_STAT_TX_PACKET_1523_1548_BYTES_0_LSB  0x0870
#define MRMAC_STAT_TX_PACKET_1549_2047_BYTES_0_LSB  0x0878
#define MRMAC_STAT_TX_PACKET_2048_4095_BYTES_0_LSB  0x0880
#define MRMAC_STAT_TX_PACKET_4096_8191_BYTES_0_LSB  0x0888
#define MRMAC_STAT_TX_PACKET_8192_9215_BYTES_0_LSB  0x0890
#define MRMAC_STAT_TX_PACKET_LARGE_0_LSB    0x0898
#define MRMAC_STAT_TX_PACKET_SMALL_0_LSB    0x08A0
#define MRMAC_STAT_TX_BAD_FCS_0_LSB 0x08D0
#define MRMAC_STAT_TX_UNICAST_0_LSB 0x08E8
#define MRMAC_STAT_TX_MULTICAST_0_LSB   0x08F0
#define MRMAC_STAT_TX_BROADCAST_0_LSB   0x08F8
#define MRMAC_STAT_TX_VLAN_0_LSB    0x0900
#define MRMAC_STAT_TX_PAUSE_0_LSB   0x0908
#define MRMAC_STAT_TX_USER_PAUSE_0_LSB  0x0910
#define MRMAC_STAT_TX_TSN_PREEMPTED_PKT_0_LSB   0x0920
#define MRMAC_STAT_TX_TSN_FRAGMENT_0_LSB    0x0928
#define MRMAC_STAT_TX_PCS_BAD_CODE_0_LSB    0x0930
#define MRMAC_STAT_TX_CL82_49_CONVERT_ERR_0_LSB 0x0938
#define MRMAC_STAT_TX_ECC_ERR0_0_LSB    0x0940
#define MRMAC_STAT_TX_ECC_ERR1_0_LSB    0x0948
#define MRMAC_STAT_RX_CYCLE_COUNT_0_LSB 0x0C00
#define MRMAC_STAT_RX_BIP_ERR_0_0_LSB   0x0C08
#define MRMAC_STAT_RX_BIP_ERR_0_1_LSB   0x0C10
#define MRMAC_STAT_RX_BIP_ERR_0_2_LSB   0x0C18
#define MRMAC_STAT_RX_BIP_ERR_0_3_LSB   0x0C20
#define MRMAC_STAT_RX_BIP_ERR_0_4_LSB   0x0C28
#define MRMAC_STAT_RX_BIP_ERR_0_5_LSB   0x0C30
#define MRMAC_STAT_RX_BIP_ERR_0_6_LSB   0x0C38
#define MRMAC_STAT_RX_BIP_ERR_0_7_LSB   0x0C40
#define MRMAC_STAT_RX_BIP_ERR_0_8_LSB   0x0C48
#define MRMAC_STAT_RX_BIP_ERR_0_9_LSB   0x0C50
#define MRMAC_STAT_RX_BIP_ERR_0_10_LSB  0x0C58
#define MRMAC_STAT_RX_BIP_ERR_0_11_LSB  0x0C60
#define MRMAC_STAT_RX_BIP_ERR_0_12_LSB  0x0C68
#define MRMAC_STAT_RX_BIP_ERR_0_13_LSB  0x0C70
#define MRMAC_STAT_RX_BIP_ERR_0_14_LSB  0x0C78
#define MRMAC_STAT_RX_BIP_ERR_0_15_LSB  0x0C80
#define MRMAC_STAT_RX_BIP_ERR_0_16_LSB  0x0C88
#define MRMAC_STAT_RX_BIP_ERR_0_17_LSB  0x0C90
#define MRMAC_STAT_RX_BIP_ERR_0_18_LSB  0x0C98
#define MRMAC_STAT_RX_BIP_ERR_0_19_LSB  0x0CA0
#define MRMAC_STAT_RX_FRAMING_ERR_0_0_LSB   0x0CA8
#define MRMAC_STAT_RX_FRAMING_ERR_0_1_LSB   0x0CB0
#define MRMAC_STAT_RX_FRAMING_ERR_0_2_LSB   0x0CB8
#define MRMAC_STAT_RX_FRAMING_ERR_0_3_LSB   0x0CC0
#define MRMAC_STAT_RX_FRAMING_ERR_0_4_LSB   0x0CC8
#define MRMAC_STAT_RX_FRAMING_ERR_0_5_LSB   0x0CD0
#define MRMAC_STAT_RX_FRAMING_ERR_0_6_LSB   0x0CD8
#define MRMAC_STAT_RX_FRAMING_ERR_0_7_LSB   0x0CE0
#define MRMAC_STAT_RX_FRAMING_ERR_0_8_LSB   0x0CE8
#define MRMAC_STAT_RX_FRAMING_ERR_0_9_LSB   0x0CF0
#define MRMAC_STAT_RX_FRAMING_ERR_0_10_LSB  0x0CF8
#define MRMAC_STAT_RX_FRAMING_ERR_0_11_LSB  0x0D00
#define MRMAC_STAT_RX_FRAMING_ERR_0_12_LSB  0x0D08
#define MRMAC_STAT_RX_FRAMING_ERR_0_13_LSB  0x0D10
#define MRMAC_STAT_RX_FRAMING_ERR_0_14_LSB  0x0D18
#define MRMAC_STAT_RX_FRAMING_ERR_0_15_LSB  0x0D20
#define MRMAC_STAT_RX_FRAMING_ERR_0_16_LSB  0x0D28
#define MRMAC_STAT_RX_FRAMING_ERR_0_17_LSB  0x0D30
#define MRMAC_STAT_RX_FRAMING_ERR_0_18_LSB  0x0D38
#define MRMAC_STAT_RX_FRAMING_ERR_0_19_LSB  0x0D40
#define MRMAC_STAT_RX_BAD_CODE_0_LSB    0x0D58
#define MRMAC_STAT_RX_PCS_BAD_CODE_0_LSB    0x0D60
#define MRMAC_STAT_RX_INVALID_START_0_LSB   0x0D68
#define MRMAC_STAT_RX_FEC_CW_0_0_LSB    0x0D70
#define MRMAC_STAT_RX_FEC_CW_0_1_LSB    0x0D78
#define MRMAC_STAT_RX_FEC_CW_0_2_LSB    0x0D80
#define MRMAC_STAT_RX_FEC_CW_0_3_LSB    0x0D88
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_0_0_LSB  0x0D90
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_0_1_LSB  0x0D98
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_0_2_LSB  0x0DA0
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_0_3_LSB  0x0DA8
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_0_LSB    0x0DB0
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_1_LSB    0x0DB8
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_2_LSB    0x0DC0
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_0_3_LSB    0x0DC8
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_0_LSB  0x0DD0
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_1_LSB  0x0DD8
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_2_LSB  0x0DE0
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_0_3_LSB  0x0DE8
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_0_LSB  0x0DF0
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_1_LSB  0x0DF8
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_2_LSB  0x0E00
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_0_3_LSB  0x0E08
#define MRMAC_STAT_RX_FEC_ERR_COUNT_0_0_LSB 0x0E10
#define MRMAC_STAT_RX_FEC_ERR_COUNT_0_1_LSB 0x0E18
#define MRMAC_STAT_RX_FEC_ERR_COUNT_0_2_LSB 0x0E20
#define MRMAC_STAT_RX_FEC_ERR_COUNT_0_3_LSB 0x0E28
#define MRMAC_STAT_RX_TOTAL_PACKETS_0_LSB   0x0E30
#define MRMAC_STAT_RX_TOTAL_GOOD_PACKETS_0_LSB  0x0E38
#define MRMAC_STAT_RX_TOTAL_BYTES_0_LSB 0x0E40
#define MRMAC_STAT_RX_TOTAL_GOOD_BYTES_0_LSB    0x0E48
#define MRMAC_STAT_RX_PACKET_64_BYTES_0_LSB 0x0E50
#define MRMAC_STAT_RX_PACKET_65_127_BYTES_0_LSB 0x0E58
#define MRMAC_STAT_RX_PACKET_128_255_BYTES_0_LSB    0x0E60
#define MRMAC_STAT_RX_PACKET_256_511_BYTES_0_LSB    0x0E68
#define MRMAC_STAT_RX_PACKET_512_1023_BYTES_0_LSB   0x0E70
#define MRMAC_STAT_RX_PACKET_1024_1518_BYTES_0_LSB  0x0E78
#define MRMAC_STAT_RX_PACKET_1519_1522_BYTES_0_LSB  0x0E80
#define MRMAC_STAT_RX_PACKET_1523_1548_BYTES_0_LSB  0x0E88
#define MRMAC_STAT_RX_PACKET_1549_2047_BYTES_0_LSB  0x0E90
#define MRMAC_STAT_RX_PACKET_2048_4095_BYTES_0_LSB  0x0E98
#define MRMAC_STAT_RX_PACKET_4096_8191_BYTES_0_LSB  0x0EA0
#define MRMAC_STAT_RX_PACKET_8192_9215_BYTES_0_LSB  0x0EA8
#define MRMAC_STAT_RX_PACKET_LARGE_0_LSB    0x0EB0
#define MRMAC_STAT_RX_PACKET_SMALL_0_LSB    0x0EB8
#define MRMAC_STAT_RX_UNDERSIZE_0_LSB   0x0EC0
#define MRMAC_STAT_RX_FRAGMENT_0_LSB    0x0EC8
#define MRMAC_STAT_RX_OVERSIZE_0_LSB    0x0ED0
#define MRMAC_STAT_RX_TOOLONG_0_LSB 0x0ED8
#define MRMAC_STAT_RX_JABBER_0_LSB  0x0EE0
#define MRMAC_STAT_RX_BAD_FCS_0_LSB 0x0EE8
#define MRMAC_STAT_RX_PACKET_BAD_FCS_0_LSB  0x0EF0
#define MRMAC_STAT_RX_STOMPED_FCS_0_LSB 0x0EF8
#define MRMAC_STAT_RX_UNICAST_0_LSB 0x0F00
#define MRMAC_STAT_RX_MULTICAST_0_LSB   0x0F08
#define MRMAC_STAT_RX_BROADCAST_0_LSB   0x0F10
#define MRMAC_STAT_RX_VLAN_0_LSB    0x0F18
#define MRMAC_STAT_RX_PAUSE_0_LSB   0x0F20
#define MRMAC_STAT_RX_USER_PAUSE_0_LSB  0x0F28
#define MRMAC_STAT_RX_INRANGEERR_0_LSB  0x0F30
#define MRMAC_STAT_RX_TRUNCATED_0_LSB   0x0F38
#define MRMAC_STAT_RX_TEST_PATTERN_MISMATCH_0_LSB   0x0F40
#define MRMAC_STAT_RX_CL49_82_CONVERT_ERR_0_LSB 0x0F48
#define MRMAC_STAT_RX_TSN_PREEMPTED_PKT_0_LSB   0x0F50
#define MRMAC_STAT_RX_TSN_FRAGMENT_0_LSB    0x0F58
#define MRMAC_STAT_RX_ECC_ERR0_0_LSB    0x0F60
#define MRMAC_STAT_RX_ECC_ERR1_0_LSB    0x0F68
#define MRMAC_STAT_TX_CYCLE_COUNT_1_LSB 0x1800
#define MRMAC_STAT_TX_FRAME_ERROR_1_LSB 0x1808
#define MRMAC_STAT_TX_TOTAL_PACKETS_1_LSB   0x1818
#define MRMAC_STAT_TX_TOTAL_GOOD_PACKETS_1_LSB  0x1820
#define MRMAC_STAT_TX_TOTAL_BYTES_1_LSB 0x1828
#define MRMAC_STAT_TX_TOTAL_GOOD_BYTES_1_LSB    0x1830
#define MRMAC_STAT_TX_PACKET_64_BYTES_1_LSB 0x1838
#define MRMAC_STAT_TX_PACKET_65_127_BYTES_1_LSB 0x1840
#define MRMAC_STAT_TX_PACKET_128_255_BYTES_1_LSB    0x1848
#define MRMAC_STAT_TX_PACKET_256_511_BYTES_1_LSB    0x1850
#define MRMAC_STAT_TX_PACKET_512_1023_BYTES_1_LSB   0x1858
#define MRMAC_STAT_TX_PACKET_1024_1518_BYTES_1_LSB  0x1860
#define MRMAC_STAT_TX_PACKET_1519_1522_BYTES_1_LSB  0x1868
#define MRMAC_STAT_TX_PACKET_1523_1548_BYTES_1_LSB  0x1870
#define MRMAC_STAT_TX_PACKET_1549_2047_BYTES_1_LSB  0x1878
#define MRMAC_STAT_TX_PACKET_2048_4095_BYTES_1_LSB  0x1880
#define MRMAC_STAT_TX_PACKET_4096_8191_BYTES_1_LSB  0x1888
#define MRMAC_STAT_TX_PACKET_8192_9215_BYTES_1_LSB  0x1890
#define MRMAC_STAT_TX_PACKET_LARGE_1_LSB    0x1898
#define MRMAC_STAT_TX_PACKET_SMALL_1_LSB    0x18A0
#define MRMAC_STAT_TX_BAD_FCS_1_LSB 0x18D0
#define MRMAC_STAT_TX_UNICAST_1_LSB 0x18E8
#define MRMAC_STAT_TX_MULTICAST_1_LSB   0x18F0
#define MRMAC_STAT_TX_BROADCAST_1_LSB   0x18F8
#define MRMAC_STAT_TX_VLAN_1_LSB    0x1900
#define MRMAC_STAT_TX_PAUSE_1_LSB   0x1908
#define MRMAC_STAT_TX_USER_PAUSE_1_LSB  0x1910
#define MRMAC_STAT_TX_TSN_PREEMPTED_PKT_1_LSB   0x1920
#define MRMAC_STAT_TX_TSN_FRAGMENT_1_LSB    0x1928
#define MRMAC_STAT_TX_PCS_BAD_CODE_1_LSB    0x1930
#define MRMAC_STAT_TX_CL82_49_CONVERT_ERR_1_LSB 0x1938
#define MRMAC_STAT_TX_ECC_ERR0_1_LSB    0x1940
#define MRMAC_STAT_TX_ECC_ERR1_1_LSB    0x1948
#define MRMAC_STAT_RX_CYCLE_COUNT_1_LSB 0x1C00
#define MRMAC_STAT_RX_FRAMING_ERR_1_LSB 0x1D48
#define MRMAC_STAT_RX_BAD_CODE_1_LSB    0x1D58
#define MRMAC_STAT_RX_PCS_BAD_CODE_1_LSB    0x1D60
#define MRMAC_STAT_RX_INVALID_START_1_LSB   0x1D68
#define MRMAC_STAT_RX_FEC_CW_1_LSB  0x1D70
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_1_LSB    0x1D90
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_1_LSB  0x1DB0
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_1_LSB    0x1DD0
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_1_LSB    0x1DF0
#define MRMAC_STAT_RX_FEC_ERR_COUNT_1_LSB   0x1E10
#define MRMAC_STAT_RX_TOTAL_PACKETS_1_LSB   0x1E30
#define MRMAC_STAT_RX_TOTAL_GOOD_PACKETS_1_LSB  0x1E38
#define MRMAC_STAT_RX_TOTAL_BYTES_1_LSB 0x1E40
#define MRMAC_STAT_RX_TOTAL_GOOD_BYTES_1_LSB    0x1E48
#define MRMAC_STAT_RX_PACKET_64_BYTES_1_LSB 0x1E50
#define MRMAC_STAT_RX_PACKET_65_127_BYTES_1_LSB 0x1E58
#define MRMAC_STAT_RX_PACKET_128_255_BYTES_1_LSB    0x1E60
#define MRMAC_STAT_RX_PACKET_256_511_BYTES_1_LSB    0x1E68
#define MRMAC_STAT_RX_PACKET_512_1023_BYTES_1_LSB   0x1E70
#define MRMAC_STAT_RX_PACKET_1024_1518_BYTES_1_LSB  0x1E78
#define MRMAC_STAT_RX_PACKET_1519_1522_BYTES_1_LSB  0x1E80
#define MRMAC_STAT_RX_PACKET_1523_1548_BYTES_1_LSB  0x1E88
#define MRMAC_STAT_RX_PACKET_1549_2047_BYTES_1_LSB  0x1E90
#define MRMAC_STAT_RX_PACKET_2048_4095_BYTES_1_LSB  0x1E98
#define MRMAC_STAT_RX_PACKET_4096_8191_BYTES_1_LSB  0x1EA0
#define MRMAC_STAT_RX_PACKET_8192_9215_BYTES_1_LSB  0x1EA8
#define MRMAC_STAT_RX_PACKET_LARGE_1_LSB    0x1EB0
#define MRMAC_STAT_RX_PACKET_SMALL_1_LSB    0x1EB8
#define MRMAC_STAT_RX_UNDERSIZE_1_LSB   0x1EC0
#define MRMAC_STAT_RX_FRAGMENT_1_LSB    0x1EC8
#define MRMAC_STAT_RX_OVERSIZE_1_LSB    0x1ED0
#define MRMAC_STAT_RX_TOOLONG_1_LSB 0x1ED8
#define MRMAC_STAT_RX_JABBER_1_LSB  0x1EE0
#define MRMAC_STAT_RX_BAD_FCS_1_LSB 0x1EE8
#define MRMAC_STAT_RX_PACKET_BAD_FCS_1_LSB  0x1EF0
#define MRMAC_STAT_RX_STOMPED_FCS_1_LSB 0x1EF8
#define MRMAC_STAT_RX_UNICAST_1_LSB 0x1F00
#define MRMAC_STAT_RX_MULTICAST_1_LSB   0x1F08
#define MRMAC_STAT_RX_BROADCAST_1_LSB   0x1F10
#define MRMAC_STAT_RX_VLAN_1_LSB    0x1F18
#define MRMAC_STAT_RX_PAUSE_1_LSB   0x1F20
#define MRMAC_STAT_RX_USER_PAUSE_1_LSB  0x1F28
#define MRMAC_STAT_RX_INRANGEERR_1_LSB  0x1F30
#define MRMAC_STAT_RX_TRUNCATED_1_LSB   0x1F38
#define MRMAC_STAT_RX_TEST_PATTERN_MISMATCH_1_LSB   0x1F40
#define MRMAC_STAT_RX_CL49_82_CONVERT_ERR_1_LSB 0x1F48
#define MRMAC_STAT_RX_TSN_PREEMPTED_PKT_1_LSB   0x1F50
#define MRMAC_STAT_RX_TSN_FRAGMENT_1_LSB    0x1F58
#define MRMAC_STAT_RX_ECC_ERR0_1_LSB    0x1F60
#define MRMAC_STAT_RX_ECC_ERR1_1_LSB    0x1F68
#define MRMAC_STAT_TX_CYCLE_COUNT_2_LSB 0x2800
#define MRMAC_STAT_TX_FRAME_ERROR_2_LSB 0x2808
#define MRMAC_STAT_TX_TOTAL_PACKETS_2_LSB   0x2818
#define MRMAC_STAT_TX_TOTAL_GOOD_PACKETS_2_LSB  0x2820
#define MRMAC_STAT_TX_TOTAL_BYTES_2_LSB 0x2828
#define MRMAC_STAT_TX_TOTAL_GOOD_BYTES_2_LSB    0x2830
#define MRMAC_STAT_TX_PACKET_64_BYTES_2_LSB 0x2838
#define MRMAC_STAT_TX_PACKET_65_127_BYTES_2_LSB 0x2840
#define MRMAC_STAT_TX_PACKET_128_255_BYTES_2_LSB    0x2848
#define MRMAC_STAT_TX_PACKET_256_511_BYTES_2_LSB    0x2850
#define MRMAC_STAT_TX_PACKET_512_1023_BYTES_2_LSB   0x2858
#define MRMAC_STAT_TX_PACKET_1024_1518_BYTES_2_LSB  0x2860
#define MRMAC_STAT_TX_PACKET_1519_1522_BYTES_2_LSB  0x2868
#define MRMAC_STAT_TX_PACKET_1523_1548_BYTES_2_LSB  0x2870
#define MRMAC_STAT_TX_PACKET_1549_2047_BYTES_2_LSB  0x2878
#define MRMAC_STAT_TX_PACKET_2048_4095_BYTES_2_LSB  0x2880
#define MRMAC_STAT_TX_PACKET_4096_8191_BYTES_2_LSB  0x2888
#define MRMAC_STAT_TX_PACKET_8192_9215_BYTES_2_LSB  0x2890
#define MRMAC_STAT_TX_PACKET_LARGE_2_LSB    0x2898
#define MRMAC_STAT_TX_PACKET_SMALL_2_LSB    0x28A0
#define MRMAC_STAT_TX_BAD_FCS_2_LSB 0x28D0
#define MRMAC_STAT_TX_UNICAST_2_LSB 0x28E8
#define MRMAC_STAT_TX_MULTICAST_2_LSB   0x28F0
#define MRMAC_STAT_TX_BROADCAST_2_LSB   0x28F8
#define MRMAC_STAT_TX_VLAN_2_LSB    0x2900
#define MRMAC_STAT_TX_PAUSE_2_LSB   0x2908
#define MRMAC_STAT_TX_USER_PAUSE_2_LSB  0x2910
#define MRMAC_STAT_TX_TSN_PREEMPTED_PKT_2_LSB   0x2920
#define MRMAC_STAT_TX_TSN_FRAGMENT_2_LSB    0x2928
#define MRMAC_STAT_TX_PCS_BAD_CODE_2_LSB    0x2930
#define MRMAC_STAT_TX_CL82_49_CONVERT_ERR_2_LSB 0x2938
#define MRMAC_STAT_TX_ECC_ERR0_2_LSB    0x2940
#define MRMAC_STAT_TX_ECC_ERR1_2_LSB    0x2948
#define MRMAC_STAT_RX_CYCLE_COUNT_2_LSB 0x2C00
#define MRMAC_STAT_RX_BIP_ERR_2_0_LSB   0x2C08
#define MRMAC_STAT_RX_BIP_ERR_2_1_LSB   0x2C10
#define MRMAC_STAT_RX_BIP_ERR_2_2_LSB   0x2C18
#define MRMAC_STAT_RX_BIP_ERR_2_3_LSB   0x2C20
#define MRMAC_STAT_RX_FRAMING_ERR_2_0_LSB   0x2CA8
#define MRMAC_STAT_RX_FRAMING_ERR_2_1_LSB   0x2CB0
#define MRMAC_STAT_RX_FRAMING_ERR_2_2_LSB   0x2CB8
#define MRMAC_STAT_RX_FRAMING_ERR_2_3_LSB   0x2CC0
#define MRMAC_STAT_RX_BAD_CODE_2_LSB    0x2D58
#define MRMAC_STAT_RX_PCS_BAD_CODE_2_LSB    0x2D60
#define MRMAC_STAT_RX_INVALID_START_2_LSB   0x2D68
#define MRMAC_STAT_RX_FEC_CW_2_0_LSB    0x2D70
#define MRMAC_STAT_RX_FEC_CW_2_1_LSB    0x2D78
#define MRMAC_STAT_RX_FEC_CW_2_2_LSB    0x2D80
#define MRMAC_STAT_RX_FEC_CW_2_3_LSB    0x2D88
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_2_0_LSB  0x2D90
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_2_1_LSB  0x2D98
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_2_2_LSB  0x2DA0
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_2_3_LSB  0x2DA8
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_0_LSB    0x2DB0
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_1_LSB    0x2DB8
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_2_LSB    0x2DC0
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_2_3_LSB    0x2DC8
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_2_0_LSB  0x2DD0
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_2_1_LSB  0x2DD8
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_2_0_LSB  0x2DF0
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_2_1_LSB  0x2DF8
#define MRMAC_STAT_RX_FEC_ERR_COUNT_2_0_LSB 0x2E10
#define MRMAC_STAT_RX_FEC_ERR_COUNT_2_1_LSB 0x2E18
#define MRMAC_STAT_RX_TOTAL_PACKETS_2_LSB   0x2E30
#define MRMAC_STAT_RX_TOTAL_GOOD_PACKETS_2_LSB  0x2E38
#define MRMAC_STAT_RX_TOTAL_BYTES_2_LSB 0x2E40
#define MRMAC_STAT_RX_TOTAL_GOOD_BYTES_2_LSB    0x2E48
#define MRMAC_STAT_RX_PACKET_64_BYTES_2_LSB 0x2E50
#define MRMAC_STAT_RX_PACKET_65_127_BYTES_2_LSB 0x2E58
#define MRMAC_STAT_RX_PACKET_128_255_BYTES_2_LSB    0x2E60
#define MRMAC_STAT_RX_PACKET_256_511_BYTES_2_LSB    0x2E68
#define MRMAC_STAT_RX_PACKET_512_1023_BYTES_2_LSB   0x2E70
#define MRMAC_STAT_RX_PACKET_1024_1518_BYTES_2_LSB  0x2E78
#define MRMAC_STAT_RX_PACKET_1519_1522_BYTES_2_LSB  0x2E80
#define MRMAC_STAT_RX_PACKET_1523_1548_BYTES_2_LSB  0x2E88
#define MRMAC_STAT_RX_PACKET_1549_2047_BYTES_2_LSB  0x2E90
#define MRMAC_STAT_RX_PACKET_2048_4095_BYTES_2_LSB  0x2E98
#define MRMAC_STAT_RX_PACKET_4096_8191_BYTES_2_LSB  0x2EA0
#define MRMAC_STAT_RX_PACKET_8192_9215_BYTES_2_LSB  0x2EA8
#define MRMAC_STAT_RX_PACKET_LARGE_2_LSB    0x2EB0
#define MRMAC_STAT_RX_PACKET_SMALL_2_LSB    0x2EB8
#define MRMAC_STAT_RX_UNDERSIZE_2_LSB   0x2EC0
#define MRMAC_STAT_RX_FRAGMENT_2_LSB    0x2EC8
#define MRMAC_STAT_RX_OVERSIZE_2_LSB    0x2ED0
#define MRMAC_STAT_RX_TOOLONG_2_LSB 0x2ED8
#define MRMAC_STAT_RX_JABBER_2_LSB  0x2EE0
#define MRMAC_STAT_RX_BAD_FCS_2_LSB 0x2EE8
#define MRMAC_STAT_RX_PACKET_BAD_FCS_2_LSB  0x2EF0
#define MRMAC_STAT_RX_STOMPED_FCS_2_LSB 0x2EF8
#define MRMAC_STAT_RX_UNICAST_2_LSB 0x2F00
#define MRMAC_STAT_RX_MULTICAST_2_LSB   0x2F08
#define MRMAC_STAT_RX_BROADCAST_2_LSB   0x2F10
#define MRMAC_STAT_RX_VLAN_2_LSB    0x2F18
#define MRMAC_STAT_RX_PAUSE_2_LSB   0x2F20
#define MRMAC_STAT_RX_USER_PAUSE_2_LSB  0x2F28
#define MRMAC_STAT_RX_INRANGEERR_2_LSB  0x2F30
#define MRMAC_STAT_RX_TRUNCATED_2_LSB   0x2F38
#define MRMAC_STAT_RX_TEST_PATTERN_MISMATCH_2_LSB   0x2F40
#define MRMAC_STAT_RX_CL49_82_CONVERT_ERR_2_LSB 0x2F48
#define MRMAC_STAT_RX_TSN_PREEMPTED_PKT_2_LSB   0x2F50
#define MRMAC_STAT_RX_TSN_FRAGMENT_2_LSB    0x2F58
#define MRMAC_STAT_RX_ECC_ERR0_2_LSB    0x2F60
#define MRMAC_STAT_RX_ECC_ERR1_2_LSB    0x2F68
#define MRMAC_STAT_TX_CYCLE_COUNT_3_LSB 0x3800
#define MRMAC_STAT_TX_FRAME_ERROR_3_LSB 0x3808
#define MRMAC_STAT_TX_TOTAL_PACKETS_3_LSB   0x3818
#define MRMAC_STAT_TX_TOTAL_GOOD_PACKETS_3_LSB  0x3820
#define MRMAC_STAT_TX_TOTAL_BYTES_3_LSB 0x3828
#define MRMAC_STAT_TX_TOTAL_GOOD_BYTES_3_LSB    0x3830
#define MRMAC_STAT_TX_PACKET_64_BYTES_3_LSB 0x3838
#define MRMAC_STAT_TX_PACKET_65_127_BYTES_3_LSB 0x3840
#define MRMAC_STAT_TX_PACKET_128_255_BYTES_3_LSB    0x3848
#define MRMAC_STAT_TX_PACKET_256_511_BYTES_3_LSB    0x3850
#define MRMAC_STAT_TX_PACKET_512_1023_BYTES_3_LSB   0x3858
#define MRMAC_STAT_TX_PACKET_1024_1518_BYTES_3_LSB  0x3860
#define MRMAC_STAT_TX_PACKET_1519_1522_BYTES_3_LSB  0x3868
#define MRMAC_STAT_TX_PACKET_1523_1548_BYTES_3_LSB  0x3870
#define MRMAC_STAT_TX_PACKET_1549_2047_BYTES_3_LSB  0x3878
#define MRMAC_STAT_TX_PACKET_2048_4095_BYTES_3_LSB  0x3880
#define MRMAC_STAT_TX_PACKET_4096_8191_BYTES_3_LSB  0x3888
#define MRMAC_STAT_TX_PACKET_8192_9215_BYTES_3_LSB  0x3890
#define MRMAC_STAT_TX_PACKET_LARGE_3_LSB    0x3898
#define MRMAC_STAT_TX_PACKET_SMALL_3_LSB    0x38A0
#define MRMAC_STAT_TX_BAD_FCS_3_LSB 0x38D0
#define MRMAC_STAT_TX_UNICAST_3_LSB 0x38E8
#define MRMAC_STAT_TX_MULTICAST_3_LSB   0x38F0
#define MRMAC_STAT_TX_BROADCAST_3_LSB   0x38F8
#define MRMAC_STAT_TX_VLAN_3_LSB    0x3900
#define MRMAC_STAT_TX_PAUSE_3_LSB   0x3908
#define MRMAC_STAT_TX_USER_PAUSE_3_LSB  0x3910
#define MRMAC_STAT_TX_TSN_PREEMPTED_PKT_3_LSB   0x3920
#define MRMAC_STAT_TX_TSN_FRAGMENT_3_LSB    0x3928
#define MRMAC_STAT_TX_PCS_BAD_CODE_3_LSB    0x3930
#define MRMAC_STAT_TX_CL82_49_CONVERT_ERR_3_LSB 0x3938
#define MRMAC_STAT_TX_ECC_ERR0_3_LSB    0x3940
#define MRMAC_STAT_TX_ECC_ERR1_3_LSB    0x3948
#define MRMAC_STAT_RX_CYCLE_COUNT_3_LSB 0x3C00
#define MRMAC_STAT_RX_FRAMING_ERR_3_LSB 0x3D48
#define MRMAC_STAT_RX_BAD_CODE_3_LSB    0x3D58
#define MRMAC_STAT_RX_PCS_BAD_CODE_3_LSB    0x3D60
#define MRMAC_STAT_RX_INVALID_START_3_LSB   0x3D68
#define MRMAC_STAT_RX_FEC_CW_3_LSB  0x3D70
#define MRMAC_STAT_RX_FEC_CORRECTED_CW_3_LSB    0x3D90
#define MRMAC_STAT_RX_FEC_UNCORRECTED_CW_3_LSB  0x3DB0
#define MRMAC_STAT_RX_FEC_BIT_ERR_0TO1_3_LSB    0x3DD0
#define MRMAC_STAT_RX_FEC_BIT_ERR_1TO0_3_LSB    0x3DF0
#define MRMAC_STAT_RX_FEC_ERR_COUNT_3_LSB   0x3E10
#define MRMAC_STAT_RX_TOTAL_PACKETS_3_LSB   0x3E30
#define MRMAC_STAT_RX_TOTAL_GOOD_PACKETS_3_LSB  0x3E38
#define MRMAC_STAT_RX_TOTAL_BYTES_3_LSB 0x3E40
#define MRMAC_STAT_RX_TOTAL_GOOD_BYTES_3_LSB    0x3E48
#define MRMAC_STAT_RX_PACKET_64_BYTES_3_LSB 0x3E50
#define MRMAC_STAT_RX_PACKET_65_127_BYTES_3_LSB 0x3E58
#define MRMAC_STAT_RX_PACKET_128_255_BYTES_3_LSB    0x3E60
#define MRMAC_STAT_RX_PACKET_256_511_BYTES_3_LSB    0x3E68
#define MRMAC_STAT_RX_PACKET_512_1023_BYTES_3_LSB   0x3E70
#define MRMAC_STAT_RX_PACKET_1024_1518_BYTES_3_LSB  0x3E78
#define MRMAC_STAT_RX_PACKET_1519_1522_BYTES_3_LSB  0x3E80
#define MRMAC_STAT_RX_PACKET_1523_1548_BYTES_3_LSB  0x3E88
#define MRMAC_STAT_RX_PACKET_1549_2047_BYTES_3_LSB  0x3E90
#define MRMAC_STAT_RX_PACKET_2048_4095_BYTES_3_LSB  0x3E98
#define MRMAC_STAT_RX_PACKET_4096_8191_BYTES_3_LSB  0x3EA0
#define MRMAC_STAT_RX_PACKET_8192_9215_BYTES_3_LSB  0x3EA8
#define MRMAC_STAT_RX_PACKET_LARGE_3_LSB    0x3EB0
#define MRMAC_STAT_RX_PACKET_SMALL_3_LSB    0x3EB8
#define MRMAC_STAT_RX_UNDERSIZE_3_LSB   0x3EC0
#define MRMAC_STAT_RX_FRAGMENT_3_LSB    0x3EC8
#define MRMAC_STAT_RX_OVERSIZE_3_LSB    0x3ED0
#define MRMAC_STAT_RX_TOOLONG_3_LSB 0x3ED8
#define MRMAC_STAT_RX_JABBER_3_LSB  0x3EE0
#define MRMAC_STAT_RX_BAD_FCS_3_LSB 0x3EE8
#define MRMAC_STAT_RX_PACKET_BAD_FCS_3_LSB  0x3EF0
#define MRMAC_STAT_RX_STOMPED_FCS_3_LSB 0x3EF8
#define MRMAC_STAT_RX_UNICAST_3_LSB 0x3F00
#define MRMAC_STAT_RX_MULTICAST_3_LSB   0x3F08
#define MRMAC_STAT_RX_BROADCAST_3_LSB   0x3F10
#define MRMAC_STAT_RX_VLAN_3_LSB    0x3F18
#define MRMAC_STAT_RX_PAUSE_3_LSB   0x3F20
#define MRMAC_STAT_RX_USER_PAUSE_3_LSB  0x3F28
#define MRMAC_STAT_RX_INRANGEERR_3_LSB  0x3F30
#define MRMAC_STAT_RX_TRUNCATED_3_LSB   0x3F38
#define MRMAC_STAT_RX_TEST_PATTERN_MISMATCH_3_LSB   0x3F40
#define MRMAC_STAT_RX_CL49_82_CONVERT_ERR_3_LSB 0x3F48
#define MRMAC_STAT_RX_TSN_PREEMPTED_PKT_3_LSB   0x3F50
#define MRMAC_STAT_RX_TSN_FRAGMENT_3_LSB    0x3F58
#define MRMAC_STAT_RX_ECC_ERR0_3_LSB    0x3F60
#define MRMAC_STAT_RX_ECC_ERR1_3_LSB    0x3F68


#endif /* MRMAC_AXI4_LITE_REGISTERS_H_ */
