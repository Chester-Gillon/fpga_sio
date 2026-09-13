This is a modified version of U200_100G_ether_duplex which attempted to allow a receive packet error to be indicated to the XMDA
by:
1. Setting Propagate_Parity on the XDMA.
2. Adding xmda_ethernet_error_to_parity_error between the Ethernet packet receive streams and the XMDA C2H streams.
   This calculates the parity for each byte of the packet.
   Valid parity is calculated, except if the final beat of a received packet has the tuser bit set indicating a receive
   packet error then invalid (inverted) parity in output.
3. Adding xmda_parity_error_to_ethernet_error between the XDMA C2H streams and the Ethernet packet transmit streams.
   This checks the parity of each byte in the packet, and if any any byte fails the parity check set the tuser bit in the
   last beat of the packet which should cause the CMAC to generate a transmit packet error.

The idea was that receive Ethernet packets with a FCS, or other error, the C2H stream would get a failure with a "parity error"
reported. However, in testing when generate a condition where expect a bad receive FCS (a CMAC transmit underrun due to
https://adaptivesupport.amd.com/s/article/000034731?language=en_US) then:
a. The C2H stream didn't seem to have any error.
b. The H2C stream stopped with a timeout. Sometimes the "magic stopped" error was indicated on the descriptor.

From PG195 it's not entirely clear how a parity error on a C2H stream is expected to be indicated.
Due to the change not working as expected, have committed just the create_project.tcl script for the project to a different
directory, and not the bitstream.

Example of cmac_switch_test hanging after H2C timeout due to X2X_CHANNEL_CONTROL_IE_MAGIC_STOPPED:
$ cmac_ethernet/cmac_switch_test -n 1:0 -p 1-4 -l -r 100000 -f 65:65
Opening device 0000:31:00.0 (10ee:903f) with IOMMU group 22
Enabled bus master for 0000:31:00.0
Device 0000:31:00.0 design U200_100G_ether_duplex routes updated
Waiting to link to be ready to receive. Press Ctrl-C to abort.
Link ready
Waiting 2 seconds after link came up for switch to accept packets (latched rx_status initial 0x000000D8 last 0x00000003)
Bit rate on interface to injection switch = 100000 (Mbps)
Requested bit rate to be generated on each switch port under test = 100000.00 (Mbps)
Not limiting frame rate, as bit-rate on interface to injection switch doesn't exceed the total across all switch ports under test
Writing per-port counts to 20260913T191530_per_port_counts_linux.csv
Using design U200_100G_ether_duplex device 0000:31:00.0 Tx port 1 Rx port 0
Test interval = 10 (secs)
Frame debug enabled = No
Expect CMAC loopback = Yes
Disable CMAC port statistics = No
Min packet len (excluding FCS) = 65
Max packet len (excluding FCS) = 65
Press Ctrl-C to stop test at end of next test interval
  0000:31:00.0 H2C channel 1 failure : Timeout: channel_status=0x10 num_descriptors_started=2172 num_completed_descriptors=2124 next_started_descriptor_index=12 next_completed_descriptor_index=12 channel_id=1 direction=H2C device=0000:31:00.0
  0000:31:00.0 C2H channel 0 failure : Timeout waiting to become idle after clearing Run bit
^C^C^C


When cmac_loopback_test fails it stops after getting an incorrect length C2H receive packet and no errors reported for the channel status:
$ cmac_ethernet/cmac_loopback_test -n 0:1
Opening device 0000:31:00.0 (10ee:903f) with IOMMU group 22
Enabled bus master for 0000:31:00.0
Device 0000:31:00.0 design U200_100G_ether_duplex routes updated
Waiting to link to be ready to receive.
Link ready (latched rx_status initial 0x000000D8 last 0x00000003)
Testing U200_100G_ether_duplex Tx port 0 Rx Port 1 with 9537 packet lengths (including FCS) from 64 to 9600 bytes
U200_100G_ether_duplex port 0 statistics (over 0.001 secs):
  CYCLE_COUNT                :          211107
  TX_TOTAL_PACKETS           :              59
  TX_TOTAL_GOOD_PACKETS      :              59
  TX_TOTAL_BYTES             :            5639
  TX_TOTAL_GOOD_BYTES        :            5487
  TX_PACKET_64_BYTES         :               1
  TX_PACKET_65_127_BYTES     :              58
  TX_UNICAST                 :              59
  RX_RSFEC_CW_INC            :           12794
U200_100G_ether_duplex port 1 statistics (over 0.001 secs):
  CYCLE_COUNT                :          211102
  RX_BAD_CODE                :               9
  RX_TOTAL_PACKETS           :              60
  RX_TOTAL_GOOD_PACKETS      :              59
  RX_TOTAL_BYTES             :            5623
  RX_TOTAL_GOOD_BYTES        :            5487
  RX_PACKET_64_BYTES         :               1
  RX_PACKET_65_127_BYTES     :              58
  RX_PACKET_128_255_BYTES    :               1
  RX_BAD_FCS                 :               1
  RX_PACKET_BAD_FCS          :               1
  RX_UNICAST                 :              59
  RX_RSFEC_CW_INC            :           12794
Total byte including FCS: 5610
Loopback test: FAIL
  0000:31:00.0 C2H channel 1 failure : Rx transfer_len=132, expected 119  C2H channel_status=0x00000000  C2H channel_status=0x00000000


cmac_loopback_test only queues one H2C transfer at a time, and stops queuing H2C transfers after the first receive packet failure.
Whereas cmac_switch_test continues to try and queue H2C transfers.

If software was used to try and transmit packets with a bad FCS, by getting the software to disable FCS insertion by the CMAC
and then the software populating a good or bad FCS. That should allow a deterministic test of which receive packets have a
FCS error (provided avoid the packet lengths for which the transmit packet FIFO can corrupt).


Other notes about creating this modification:
1. After adding the xmda_ethernet_error_to_parity_error and xmda_parity_error_to_ethernet_error the timing wasn't met,
   on 322 MHz clocks used by the CMAC.
2. Adding AXI Stream Register Slices either side of xmda_ethernet_error_to_parity_error and xmda_parity_error_to_ethernet_error
   didn't allow timing to be met. Did that since xmda_ethernet_error_to_parity_error and xmda_parity_error_to_ethernet_error have
   combitorial logic on the TDATA values and wasn't sure if the additional logic was causing the timing issue.
   Albeit the additional logic was on the XDMA clock of 250MHz for which the timing was met.
3. Removing the the System ILA which are monitoring the input to the transmit packet FIFOs, at the XDMA clock of 250MHz
   allowed timing to be met.
4. Removing the AXI Stream Register Slices still allowed timing to be met.

From briefly looking at the implementation log, possibly adding additional logic on the XMDA clock was causing "router congestion"
which which caused problems routing the logic for the higher frequency CMAC.

