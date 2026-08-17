/*
 * @file cmac_drp_registers.h
 * @date 16 Aug 2026
 * @author Chester Gillon
 * @brief Defines the DRP address map of the CMAC block
 * @details
 *   The contents of this file was taken from "Table 26: DRP Map of the CMAC Block" in
 *   UltraScale+ Devices Integrated 100G Ethernet Subsystem v3.1 PG203.
 *
 *   This is only a sub-set of the registers.
 */

#ifndef CMAC_DRP_REGISTERS_H_
#define CMAC_DRP_REGISTERS_H_

#include "vfio_bitops.h"

/* Enable FCS error checking at the LBUS interface by the TX core. This input only has effect when ctl_tx_fcs_ins_enable is
   FALSE.
   - TRUE: A packet with bad FCS transmitted is binned as good.
   - FALSE: A packet with bad FCS transmitted is not binned as good.

   The error is flagged on the signals stat_tx_bad_fcs and STAT_RX_STOMPED_FCS, and the packet is transmitted as it was received.
   Statistics are reported as if there was no FCS error. */
#define CMAC_DRP_CTL_TX_IGNORE_FCS_OFFSET 0x1
#define CMAC_DRP_CTL_TX_IGNORE_FCS_MASK VFIO_BIT (0)

/* Enable FCS insertion by the TX core.
   - TRUE: 100G Ethernet subsystem calculates and adds FCS to the packet.
   - FALSE: 100G Ethernet subsystem does not add FCS to packet.

   This attribute cannot be changed dynamically between packets. */
#define CMAC_DRP_CTL_TX_FCS_INS_ENABLE_OFFSET 0x2
#define CMAC_DRP_CTL_TX_FCS_INS_ENABLE_MASK VFIO_BIT (0)

/* The ctl_tx_ipg_value defines the target average minimum Inter Packet Gap (IPG, in bytes) inserted between LBUS packets.
   Valid values are 8 to 12. The ctl_tx_ipg_value can also be programmed to a value in the 0 to 7 range, but in that case,
   it is interpreted as meaning minimal IPG, so only Terminate code word IPG is inserted; no Idles are ever added in that case -
   and that produces an average IPG of around 4 bytes when random-size packets are transmitted/ */
#define CMAC_DRP_CTL_TX_IPG_VALUE_OFFSET 0xD
#define CMAC_DRP_CTL_TX_IPG_VALUE_MASK VFIO_GENMASK_U32 (3,0)

/* Enable FCS removal by the RX core.
   - TRUE: 100G Ethernet subsystem deletes the FCS of the incoming packet.
   - FALSE: 100G Ethernet subsystem does not remove the FCS of the incoming packet.

   FCS is not deleted for packets that are less than or equal to 8 bytes long. */
#define CMAC_DRP_CTL_RX_DELETE_FCS_OFFSET 0xA5
#define CMAC_DRP_CTL_RX_DELETE_FCS_MASK VFIO_BIT (0)

/* Any packet shorter than the default value of 64 (decimal) is considered to be undersized. If a packet has a size less
   than this value, the rx_errout signal is asserted during the rx_eopout asserted cycle. Packets less than 64 bytes are
   dropped. The value of this bus must be less than or equal to the value of CTL_RX_MAX_PACKET_LEN[14:0]. */
#define CMAC_DRP_CTL_RX_MIN_PACKET_LEN_OFFSET 0xAE
#define CMAC_DRP_CTL_RX_MIN_PACKET_LEN_MASK VFIO_GENMASK_U32 (7,0)

/* Any packet longer than this value is considered to be oversized. If a packet has a size greater than this value,
   the packet is truncated to this value and the RX_ERROUT signal is asserted along with the rx_eopout signal.
   ctl_rx_max_packet_len[14] is reserved and must be set to 0.
   Packets less than 64 bytes are dropped.
   The allowed value for this bus can range from 64 to 16,383. */
#define CMAC_DRP_CTL_RX_MAX_PACKET_LEN_OFFSET 0xAF
#define CMAC_DRP_CTL_RX_MAX_PACKET_LEN_MASK VFIO_GENMASK_U32 (14,0)

#endif /* CMAC_DRP_REGISTERS_H_ */
