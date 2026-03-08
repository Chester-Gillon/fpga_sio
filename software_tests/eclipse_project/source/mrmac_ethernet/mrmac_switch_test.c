/*
 * @file mrmac_switch_test.c
 * @date 1 Mar 2026
 * @author Chester Gillon
 * @details A variant of the https://github.com/chester-gillon/switch_test but which uses the MRMAC to
 *          transmit and receive the frames, rather than PCAP.
 */

#include "identify_pcie_fpga_design.h"
#include "mrmac_axi4_lite_registers.h"
#include "xilinx_dma_bridge_transfers.h"
#include "transfer_timing.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>

#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/time.h>


/* Define a string to report the Operating System just to report in result filenames, for commonality with the
 * pcap based program. Fixed to linux due to lack of verbs API for Windows. */
#define OS_NAME "linux"


#define NSECS_PER_SEC 1000000000LL

/* Ethernet frame types */
#define ETH_P_8021Q    0x8100
#define ETH_P_ETHERCAT 0x88A4


/* Timeout used to detect if the transmit DMA has hung.
 *
 * A timeout isn't used for receive DMA:
 * a. Receive DMA won't complete unless there are receive Ethernet frames.
 * b. Receive DMA uses c2h_stream_continuous, so the software isn't queueing transfers. */
#define XMDA_TRANSFER_TIMEOUT_SECS 5


#define ETHER_MAC_ADDRESS_LEN 6

/** Defines the unique identity used for one switch port under test.
 *  The MAC address is used by the switch under test to route traffic to the expected port.
 *  The VLAN is used by the injection switch.
 *
 *  The switch_port_number is that of the switch under test, controlled by the cabling and the VLAN assignment,
 *  i.e. for information and not set by the software.
 */
typedef struct
{
    uint32_t switch_port_number;
    uint8_t mac_addr[ETHER_MAC_ADDRESS_LEN];
    uint16_t vlan;
} port_id_t;


/** Define a locally administrated MAC address and VLAN for each possible switch port under test.
    The last octet of the MAC address is the index into the array, which is used an optimisation when looking
    up the port number of a received frame (to avoid having to search the table). */
#define NUM_DEFINED_PORTS 48
static const port_id_t test_ports[NUM_DEFINED_PORTS] =
{
    { .switch_port_number =  1, .mac_addr = {2,0,1,0,0, 0}, .vlan = 1001},
    { .switch_port_number =  2, .mac_addr = {2,0,1,0,0, 1}, .vlan = 1002},
    { .switch_port_number =  3, .mac_addr = {2,0,1,0,0, 2}, .vlan = 1003},
    { .switch_port_number =  4, .mac_addr = {2,0,1,0,0, 3}, .vlan = 1004},
    { .switch_port_number =  5, .mac_addr = {2,0,1,0,0, 4}, .vlan = 1005},
    { .switch_port_number =  6, .mac_addr = {2,0,1,0,0, 5}, .vlan = 1006},
    { .switch_port_number =  7, .mac_addr = {2,0,1,0,0, 6}, .vlan = 1007},
    { .switch_port_number =  8, .mac_addr = {2,0,1,0,0, 7}, .vlan = 1008},
    { .switch_port_number =  9, .mac_addr = {2,0,1,0,0, 8}, .vlan = 1009},
    { .switch_port_number = 10, .mac_addr = {2,0,1,0,0, 9}, .vlan = 1010},
    { .switch_port_number = 11, .mac_addr = {2,0,1,0,0,10}, .vlan = 1011},
    { .switch_port_number = 12, .mac_addr = {2,0,1,0,0,11}, .vlan = 1012},
    { .switch_port_number = 13, .mac_addr = {2,0,1,0,0,12}, .vlan = 1013},
    { .switch_port_number = 14, .mac_addr = {2,0,1,0,0,13}, .vlan = 1014},
    { .switch_port_number = 15, .mac_addr = {2,0,1,0,0,14}, .vlan = 1015},
    { .switch_port_number = 16, .mac_addr = {2,0,1,0,0,15}, .vlan = 1016},
    { .switch_port_number = 17, .mac_addr = {2,0,1,0,0,16}, .vlan = 1017},
    { .switch_port_number = 18, .mac_addr = {2,0,1,0,0,17}, .vlan = 1018},
    { .switch_port_number = 19, .mac_addr = {2,0,1,0,0,18}, .vlan = 1019},
    { .switch_port_number = 20, .mac_addr = {2,0,1,0,0,19}, .vlan = 1020},
    { .switch_port_number = 21, .mac_addr = {2,0,1,0,0,20}, .vlan = 1021},
    { .switch_port_number = 22, .mac_addr = {2,0,1,0,0,21}, .vlan = 1022},
    { .switch_port_number = 23, .mac_addr = {2,0,1,0,0,22}, .vlan = 1023},
    { .switch_port_number = 24, .mac_addr = {2,0,1,0,0,23}, .vlan = 1024},
    { .switch_port_number = 25, .mac_addr = {2,0,1,0,0,24}, .vlan = 1025},
    { .switch_port_number = 26, .mac_addr = {2,0,1,0,0,25}, .vlan = 1026},
    { .switch_port_number = 27, .mac_addr = {2,0,1,0,0,26}, .vlan = 1027},
    { .switch_port_number = 28, .mac_addr = {2,0,1,0,0,27}, .vlan = 1028},
    { .switch_port_number = 29, .mac_addr = {2,0,1,0,0,28}, .vlan = 1029},
    { .switch_port_number = 30, .mac_addr = {2,0,1,0,0,29}, .vlan = 1030},
    { .switch_port_number = 31, .mac_addr = {2,0,1,0,0,30}, .vlan = 1031},
    { .switch_port_number = 32, .mac_addr = {2,0,1,0,0,31}, .vlan = 1032},
    { .switch_port_number = 33, .mac_addr = {2,0,1,0,0,32}, .vlan = 1033},
    { .switch_port_number = 34, .mac_addr = {2,0,1,0,0,33}, .vlan = 1034},
    { .switch_port_number = 35, .mac_addr = {2,0,1,0,0,34}, .vlan = 1035},
    { .switch_port_number = 36, .mac_addr = {2,0,1,0,0,35}, .vlan = 1036},
    { .switch_port_number = 37, .mac_addr = {2,0,1,0,0,36}, .vlan = 1037},
    { .switch_port_number = 38, .mac_addr = {2,0,1,0,0,37}, .vlan = 1038},
    { .switch_port_number = 39, .mac_addr = {2,0,1,0,0,38}, .vlan = 1039},
    { .switch_port_number = 40, .mac_addr = {2,0,1,0,0,39}, .vlan = 1040},
    { .switch_port_number = 41, .mac_addr = {2,0,1,0,0,40}, .vlan = 1041},
    { .switch_port_number = 42, .mac_addr = {2,0,1,0,0,41}, .vlan = 1042},
    { .switch_port_number = 43, .mac_addr = {2,0,1,0,0,42}, .vlan = 1043},
    { .switch_port_number = 44, .mac_addr = {2,0,1,0,0,43}, .vlan = 1044},
    { .switch_port_number = 45, .mac_addr = {2,0,1,0,0,44}, .vlan = 1045},
    { .switch_port_number = 46, .mac_addr = {2,0,1,0,0,45}, .vlan = 1046},
    { .switch_port_number = 47, .mac_addr = {2,0,1,0,0,46}, .vlan = 1047},
    { .switch_port_number = 48, .mac_addr = {2,0,1,0,0,47}, .vlan = 1048},
};


/* Array which defines which of the indices in test_ports[] are tested at run-time.
 * This allows command line arguments to specify a sub-set of the possible ports to be tested. */
static uint32_t tested_port_indices[NUM_DEFINED_PORTS];
static uint32_t num_tested_port_indices;


/**
 * Defines the layout of one maximum length Ethernet frame, with a single EtherCAT datagram, for the test.
 * EtherCAT has it own EtherType which can be used to filter the received frames to only those frames.
 *
 * Layout taken from:
 *   https://www.szcomark.com/info/ethercat-frame-structure-59044613.html
 *   https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1257993099.html
 *
 * The Address is used a sequence number incremented for each test frame transmitted.
 *
 * The value of ETHERCAT_DATAGRAM_LEN results in the maximum MTU of 1500, which is formed from the EtherCAT Datagram header
 * through Working Counter inclusive.
 *
 * https://en.wikipedia.org/wiki/Ethernet_frame notes that the IEEE 802.3ac specification which added the
 * VLAN tag increased the maximum frame size by 4 octets to allow for the encapsulated VLAN tag.
 */
#define ETHERCAT_DATAGRAM_LEN 1486
typedef struct __attribute__((packed))
{
    /* Ethernet Header */
    uint8_t destination_mac_addr[ETHER_MAC_ADDRESS_LEN];
    uint8_t source_mac_addr[ETHER_MAC_ADDRESS_LEN];
    uint16_t ether_type; /* Set to indicate a VLAN */

    /* VLAN id */
    uint16_t vlan_tci;

    uint16_t vlan_ether_type;

    /* EtherCAT header */
    uint16_t Length:11;  /* Length of the EtherCAT datagram (without FCS) */
    uint16_t Reserved:1; /* Reserved, 0 */
    uint16_t Type:4;     /* Protocol type. EtherCAT slave controllers (ESCs) only support EtherCAT commands (type = 0x1). */

    /* EtherCAT Datagram header */
    uint8_t Cmd; /* EtherCAT command type */
    uint8_t Idx; /* The index is a numerical identifier used by the master to identify duplicates or lost
                  * datagrams. The EtherCAT slaves should not change the index. */
    uint32_t Address; /* Address: auto-increment, configured station address or logical address */
    uint16_t Len:11;  /* Length of the data following within this datagram */
    uint16_t R:3;     /* Reserved, 0 */
    uint16_t C:1;     /* Circulating frame:
                       * 0: Frame does not circulate
                       * 1: Frame has circulated once */
    uint16_t M:1;     /* Multiple EtherCAT datagrams
                       * 0: Last EtherCAT datagram
                       * 1: At least one further EtherCAT datagram follows */
    uint16_t IRQ;     /* EtherCAT event request register of all slave devices combined with a logical OR */
    uint8_t data[ETHERCAT_DATAGRAM_LEN]; /* Data to be read or written */
    uint16_t WKC;     /* Working Counter */
} ethercat_frame_t;


/* The total number of bit times one ethercat_frame_t occupies a network port for */
#define TEST_PACKET_TOTAL_OCTETS (7 + /* Preamble */ \
                                  1 + /* Start frame delimiter */ \
                                  sizeof (ethercat_frame_t) + \
                                  4 + /* Frame check sequence */ \
                                  12) /* Interpacket gap */
#define BITS_PER_OCTET 8
#define TEST_PACKET_BITS (TEST_PACKET_TOTAL_OCTETS * BITS_PER_OCTET)


/* Identifies one type of frame recorded by the test */
typedef enum
{
    /* A frame transmitted by the test */
    FRAME_RECORD_TX_TEST_FRAME,
    /* An EtherCAT test frame in the format which is transmitted, and which was received by the test on the
     * VLAN for the expected destination port. This frame contains an expected pending sequence number. */
    FRAME_RECORD_RX_TEST_FRAME,
    /* As per FRAME_RECORD_RX_TEST_FRAME except the received sequence number was not expected.
     * This may happen if frames get delayed such that there is no recording of the pending sequence number. */
    FRAME_RECORD_RX_UNEXPECTED_FRAME,
    /* An EtherCAT test frame in the format which is transmitted, and which was received by the test on a
     * VLAN other than that expected for the destination port.
     * This means the frame was flooded because the switch under test didn't know which port the destination MAC address
     * was for. */
    FRAME_RECORD_RX_FLOODED_FRAME,
    /* A frame received by the test, which isn't one transmitted.
       I.e. any frames which are not generated by the test program.
       While ibv_create_flow() could be used to apply a IBV_FLOW_SPEC_ETH filter to only receive EtherCAT frames
       consider it better to receive all frames so can report statistics on the number of other frames which are
       may be consuming some bandwidth on the switch ports under test. */
    FRAME_RECORD_RX_OTHER,

    FRAME_RECORD_ARRAY_SIZE
} frame_record_type_t;


/* Look up table which gives the description of each frame_record_type_t */
static const char *const frame_record_types[FRAME_RECORD_ARRAY_SIZE] =
{
    [FRAME_RECORD_TX_TEST_FRAME      ] = "Tx Test",
    [FRAME_RECORD_RX_TEST_FRAME      ] = "Rx Test",
    [FRAME_RECORD_RX_UNEXPECTED_FRAME] = "Rx Unexpected",
    [FRAME_RECORD_RX_FLOODED_FRAME   ] = "Rx Flooded",
    [FRAME_RECORD_RX_OTHER           ] = "Rx Other"
};


/* Used to record one frame transmitted or received during the test, for debugging purposes */
typedef struct
{
    /* Identifies the type of frame */
    frame_record_type_t frame_type;
    /* The relative time from the start of the test that the frame was sent or received.
       This is using the monotonic time used by the test busy-polling loop rather. */
    int64_t relative_test_time;
    /* The destination and source MAC addresses from the frame */
    uint8_t destination_mac_addr[ETHER_MAC_ADDRESS_LEN];
    uint8_t source_mac_addr[ETHER_MAC_ADDRESS_LEN];
    /* The length of the frame */
    size_t len;
    /* The ether type of the frame, the field this is extracted from depends upon if there is a VLAN */
    uint16_t ether_type;
    /* True if the frame was for a VLAN */
    bool vlan_present;
    /* When vlan_present is true identifies the VLAN this was for */
    uint16_t vlan_id;
    /* When frame_type is other than FRAME_RECORD_RX_OTHER the sequence number of the frame.
     * Allows received frames to be matched against the transmitted frame. */
    uint32_t test_sequence_number;
    /* When frame_type is other than FRAME_RECORD_RX_OTHER the source and destination port numbers of the frame,
     * based upon matching the source_mac_addr and destination_mac_addr */
    uint32_t source_port_index;
    uint32_t destination_port_index;
    /* Set true for a FRAME_RECORD_TX_TEST_FRAME for which there is no matching FRAME_RECORD_RX_TEST_FRAME */
    bool frame_missed;
} frame_record_t;


/* File used to store a copy of the output written to the console */
static FILE *console_file;


/* Command line arguments which specify the MRMAC device and port used to send/receive test frames */
static char arg_mrmac_device_location[PATH_MAX];
static uint32_t arg_mrmac_port_num;


/* Command line argument which specifies the test interval in seconds, which is the interval over which statistics are
 * accumulated and then reported. */
static int64_t arg_test_interval_secs = 10;


/* Command line argument which specifies the requested rate on each tested switch port, in Mbps */
#define DEFAULT_TESTED_PORT_MBPS 100.0
static double arg_tested_port_mbps = DEFAULT_TESTED_PORT_MBPS;


/* Command line argument which controls how the test runs:
 * - When false the test runs until requested to stop, and only reports summary information for each test interval.
 * - When true the test runs for a single test interval, recording the transmitted/received frames in memory which are written
 *   to s CSV file at the end of the test interval. */
static bool arg_frame_debug_enabled = false;


/* Used to store pending receive frames for one source / destination port combination.
 * As frames are transmitted they are stored in, and then removed once received.
 *
 * Pending receive frames are stored for each source / destination port combination since:
 * a. Frames for a given source / destination port combination shouldn't get reordered.
 * b. Upon receipt saves having to search through all pending frames.
 *
 * NOMINAL_TOTAL_PENDING_RX_FRAMES defines the total number of pending receive frames which can be stored across all
 * source / destination port combinations, and allows for both latency in the test frames being circulated around the switches
 * and any delay in the software polling for frame receipt. It's value is set based upon the initial code which fixed to test
 * 24 switch ports (23x24 so 552 combinations) and 3 pending rx frames per combination, and doubled to give some leeway.
 *
 * The actual number of pending receive frames per combination is set at run time according to the number of ports being tested.
 *
 * Missing frames get detected either:
 * a. If a single frame is missing, the missing frame is detected when the next expected frame has a later sequence number.
 * b. If multiple frames are missing, a missing frame is detected when the next transmit occurs and the
 *    pending_rx_sequence_numbers[] array is full. */
#define NOMINAL_TOTAL_PENDING_RX_FRAMES 3312
#define MIN_TESTED_PORTS 2
#define MIN_PENDING_RX_FRAMES_PER_PORT_COMBO 3
typedef struct
{
    /* The number of frames which have been transmitted and are pending for receipt */
    uint32_t num_pending_rx_frames;
    /* Index in pending_rx_sequence_numbers[] where a frame is stored after has been transmitted */
    uint32_t tx_index;
    /* Index in pending_rx_sequence_numbers[] which contains the next expected receive frame */
    uint32_t rx_index;
    /* Circular buffer used to record pending sequence numbers for receive frames */
    uint32_t *pending_rx_sequence_numbers;
    /* When frame debug is enabled points at the frame record for the FRAME_RECORD_TX_TEST_FRAME for each pending receive frame,
     * so that if a frame is not received the transmit frame record can be marked as such.
     *
     * Done this way round to avoid marking pending frames at the end of a test sequence as missing. */
    frame_record_t **tx_frame_records;
} pending_rx_frames_t;


/* Contains the statistics for test frames for one combination of source / destination ports for one test interval */
typedef struct
{
    /* The number of expected receive frames during the test interval */
    uint32_t num_valid_rx_frames;
    /* The number of missing receive frames during the test interval */
    uint32_t num_missing_rx_frames;
    /* The number of frames transmitted during the test interval */
    uint32_t num_tx_frames;
} port_frame_statistics_t;


/* Contains the statistics for test frames transmitted and received over one test interval in which the statistics
 * are accumulated. The transmit and receive counts may not match, if there are frames which are pending being received
 * at the end of the interval. */
typedef struct
{
    /* The monotonic start and end time of the test interval, to give the duration over which the statistics were accumulated */
    int64_t interval_start_time;
    int64_t interval_end_time;
    /* The counts of different types of frames during the test interval, across all ports tested */
    uint32_t frame_counts[FRAME_RECORD_ARRAY_SIZE];
    /* Receive frame counts, indexed by each [source_port][destination_port] combination */
    port_frame_statistics_t port_frame_statistics[NUM_DEFINED_PORTS][NUM_DEFINED_PORTS];
    /* Counts the total number of missing frames during the test interval */
    uint32_t total_missing_frames;
    /* The maximum value of num_pending_rx_frames which has been seen for any source / destination port combination during
     * the test. Used to collect debug information about how close to the MAX_PENDING_RX_FRAMES a test without any errors is
     * getting. This value can get to the maximum if missed frames get reported during the test when the transmission detects
     * all pending rx sequence numbers are in use.
     *
     * If Rx Unexpected frames are reported and max_pending_rx_frames is MAX_PENDING_RX_FRAMES this suggests the maximum value
     * should be increased to allow for the latency in the frames being sent by the switches and getting thtough the software. */
    uint32_t max_pending_rx_frames;
    /* Set true in the final statistics before the transmit/receive thread exits */
    bool final_statistics;
} frame_test_statistics_t;


/* Used to record frames transmitted or received for debug purposes */
typedef struct
{
    /* The allocated length of the frame_records[] array. When zero frames are not recorded */
    uint32_t allocated_length;
    /* The number of entries in the frame_records[] array which are currently populated */
    uint32_t num_frame_records;
    /* Array used to record frames */
    frame_record_t *frame_records;
} frame_records_t;


/* The context used for the thread which sends/receive the test frames. */
typedef struct
{
    /* All the FPGA designs which have been opened */
    fpga_designs_t designs;
    /* The MRMAC design used to send/receive the test frames */
    fpga_design_t *mrmac_design;
    /* Mapped to the MRMAC port Configuration registers, Status registers, and Statistics counters */
    uint8_t *mrmac_port_regs;
    /* The number of buffers allocated for transmit and receive, which can be queued for XDMA */
    uint32_t tx_num_buffers;
    uint32_t rx_num_buffers;
    /* The next sequence number to be transmitted */
    uint32_t next_tx_sequence_number;
    /* The next index into tested_port_indices[] for the next destination port to use for a transmitted frame */
    uint32_t destination_tested_port_index;
    /* The next modulo offset from destination_tested_port_index to use as the source port for a transmitted frame */
    uint32_t source_port_offset;
    /* Contains the pending receive frames, indexed by [source_port][destination_port] */
    pending_rx_frames_t pending_rx_frames[NUM_DEFINED_PORTS][NUM_DEFINED_PORTS];
    /* Used to accumulate the statistics for the current test interval */
    frame_test_statistics_t statistics;
    /* Monotonic time at which the current test interval ends, which is when the statistics are published and then reset */
    int64_t test_interval_end_time;
    /* Optionally used to record frames for debug */
    frame_records_t frame_recording;
    /* Controls the rate at which transmit frames are generated:
     * - When true a timer is used to limit the maximum rate at which frames are transmitted.
     *   This can be used when the available bandwidth on the link to the injection switch exceeds that available to distribute
     *   the total bandwidth across all ports in the switch under test.
     *
     *   E.g. if the link to the injection switch is 1G and the links to the switch under test are 100M, then if less than 10
     *   9 ports are tested then need to rate limit the transmission.
     *
     * - When false frames are transmitted as quickly as possible. */
    bool tx_rate_limited;
    /* When tx_rate_limited is true, the monotonic time between each frame transmitted */
    int64_t tx_interval;
    /* When tx_rate_limited is true, the monotonic time at which to transmit the next frame */
    int64_t tx_time_of_next_frame;
    /* The maximum number of pending receive frames for each source / destination port combination during the test */
    uint32_t pending_rx_sequence_numbers_length;
    /* Overall success of initialising and performing XMDA transfers */
    bool xdma_overall_success;
    /* Read/write mapping for the XDMA descriptors */
    vfio_dma_mapping_t descriptors_mapping;
    /* XDMA read mapping used by device for Ethernet transmission */
    vfio_dma_mapping_t h2c_data_mapping;
    /* XDMA write mapping used by device for Ethernet reception */
    vfio_dma_mapping_t c2h_data_mapping;
    /* Used to perform XMDA transfers for Ethernet transmission / reception */
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    /* Used to populate the record for one receive frame, possibly allow for receiving an Ethernet frame larger than the
     * size used for the test. */
    frame_record_t rx_frame_record;
    /* When true are waiting for an end-of-packet to be indicate in a receive buffer to indicate rx_frame_record is complete */
    bool rx_end_of_packet_pending;
} frame_tx_rx_thread_context_t;


/* Contains the information for the results summary over multiple test intervals */
typedef struct
{
    /* Filename used for the per-port counts */
    char per_port_counts_csv_filename[PATH_MAX];
    /* File to which the per-port counts are written */
    FILE *per_port_counts_csv_file;
    /* The number of test intervals which have had failures, due to missed frames */
    uint32_t num_test_intervals_with_failures;
    /* The string containing the time of the last test interval which had a failure */
    char time_of_last_failure[80];
} results_summary_t;


/* test_statistics contains the statistics from the most recent completed test interval.
 * It is written by the transmit_receive_thread, and read by the main thread to report the test progress.
 *
 * The semaphores control the access by:
 * a. The free semaphore is initialised to 1, and the populated semaphore to 0.
 * b. The main thread blocks in sem_wait (test_statistics_populated) waiting for results.
 * c. At the end of a test interval the transmit_receive_thread:
 *    - sem_wait (test_statistics_free) which should not block unless the main thread isn't keeping up with reporting
 *      the test progress.
 *    - Stores the results for the completed test interval in test_statistics
 *    - sem_post (test_statistics_populated) to wake up the main thread.
 * d. When the main thread is woken up from sem_wait(test_statistics_populated):
 *    - Reports the contents of test_statistics
 *    - sem_post (test_statistics_free) to indicate has processed test_statistics
 * e. The sequence starts again from b.
 */
static frame_test_statistics_t test_statistics;
static sem_t test_statistics_free;
static sem_t test_statistics_populated;


/* Set true in a signal handler when Ctrl-C is used to request a running test stops */
static volatile bool test_stop_requested;


/**
 * @brief Signal handler to request a running test stops
 * @param[in] sig Not used
 */
static void stop_test_handler (const int sig)
{
    test_stop_requested = true;
}


/*
 * @brief Write formatted output to the console and a log file
 * @param[in] format printf style format string
 * @param[in] ... printf arguments
 */
static void console_printf (const char *const format, ...) __attribute__((format(printf,1,2)));
static void console_printf (const char *const format, ...)
{
    va_list args;

    if (console_file != NULL)
    {
        va_start (args, format);
        vfprintf (console_file, format, args);
        va_end (args);
    }

    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}


/**
 * @brief Abort the program if an assertion fails, after displaying a message
 * @param[in] assertion Should be true to allow the program to continue.
 * @param[in] format printf style format string for error message.
 * @param[in] ... printf arguments
 */
#define CHECK_ASSERT(assertion) check_assert(assertion,#assertion)
static void check_assert (const bool assertion, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
static void check_assert (const bool assertion, const char *format, ...)
{
    if (!assertion)
    {
        va_list args;

        if (console_file != NULL)
        {
            va_start (args, format);
            fprintf (console_file, "Assertion failed : ");
            vfprintf (console_file, format, args);
            va_end (args);
            fprintf (console_file, "\n");
        }

        va_start (args, format);
        fprintf (stderr, "Assertion failed : ");
        vfprintf (stderr, format, args);
        va_end (args);
        fprintf (stderr, "\n");
        exit (EXIT_FAILURE);
    }
}


/**
 * @brief If a transfer failed, report an error to the console
 * @param[in] context The transfer context to check for errors.
 */
static void report_if_transfer_failed (const x2x_transfer_context_t *const context)
{
    if (context->failed)
    {
        console_printf ("  %s %s channel %u failure : %s%s\n",
                context->configuration.vfio_device->device_name,
                (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ? "H2C" : "C2H",
                context->configuration.channel_id,
                context->error_message,
                context->timeout_awaiting_idle_at_finalisation ? " (+timeout waiting for idle at finalisation)" : "");
    }
}


/**
 * @brief Display the program usage and then exit
 * @param[in] program_name Name of the program from argv[0]
 */
static void display_usage (const char *const program_name)
{
    printf ("Usage %s: [-i <domain>:<bus>:<dev>.<func>] -n <mrmac_port_num> [-t <duration_secs>] [-d] [-p <port_list>] [-r <rate_mbps>]\n", program_name);
    printf ("\n");
    printf ("  -i only open using VFIO specific PCI device in the event that there is more than\n");
    printf ("     one PCI device which matches the identity filters.\n");
    printf ("  -n specifies which MRMAC port to use\n");
    printf ("\n");
    printf ("  -d enables debug mode, where runs just for a single test interval and creates\n");
    printf ("     a CSV file containing the frames sent/received.\n");
    printf ("\n");
    printf ("  -t defines the duration of a test interval in seconds, over which the number\n");
    printf ("     errors is accumulated and reported.\n");
    printf ("  -p defines which switch ports to test. The <port_list> is a comma separated\n");
    printf ("     string, with each item either a single port number or a start and end port\n");
    printf ("     range delimited by a dash. E.g. 1,4-6 specifies ports 1,4,5,6.\n");
    printf ("     If not specified defaults to all %u defined ports\n", NUM_DEFINED_PORTS);
    printf ("  -r Specifies the bit rate generated on each port on the switch under test,\n");
    printf ("     as a floating point mega bits per second. Default is %g\n", DEFAULT_TESTED_PORT_MBPS);

    exit (EXIT_FAILURE);
}


/**
 * @brief store one switch port number to be tested
 * @details Exits with an error port_num fails validation checks
 * @param[in] port_num The switch port number specified on the command line which is to be tested
 */
static void store_tested_port_num (const uint32_t port_num)
{
    bool found;
    uint32_t port_index;

    /* Check the port number is defined */
    port_index = 0;
    found = false;
    while ((!found) && (port_index < NUM_DEFINED_PORTS))
    {
        if (test_ports[port_index].switch_port_number == port_num)
        {
            found = true;
        }
        else
        {
            port_index++;
        }
    }
    if (!found)
    {
        printf ("Error: Switch port %" PRIu32 " is not defined in the compiled-in list\n", port_num);
        exit (EXIT_FAILURE);
    }

    /* Check the port number hasn't been specified more than once */
    for (uint32_t tested_port_index = 0; tested_port_index < num_tested_port_indices; tested_port_index++)
    {
        if (test_ports[tested_port_indices[tested_port_index]].switch_port_number == port_num)
        {
            printf ("Error: Switch port %" PRIu32 " specified more than once\n", port_num);
            exit (EXIT_FAILURE);
        }
    }

    tested_port_indices[num_tested_port_indices] = port_index;
    num_tested_port_indices++;
}


/*
 * @brief Parse the list of switch ports to be tested, storing in tested_port_indicies[]
 * @details Exits with an error if the port list isn't valid.
 * @param[in] tested_port_indicies The string contain the list of ports from the command line
 */
static void parse_tested_port_list (const char *const port_list_in)
{
    const char *const ports_delim = ",";
    const char *const range_delim = "-";
    char *const port_list = strdup (port_list_in);
    char *port_list_saveptr = NULL;
    char *port_range_saveptr = NULL;
    char *port_list_token;
    char *port_range_token;
    uint32_t port_num;
    char junk;
    uint32_t range_start_port_num;
    uint32_t range_end_port_num;

    /* Process the list of comma separated port numbers */
    num_tested_port_indices = 0;
    port_list_token = strtok_r (port_list, ports_delim, &port_list_saveptr);
    while (port_list_token != NULL)
    {
        /* For each comma separated item extract as either a single port number, or a range of consecutive port numbers
         * separated by a dash. */
        port_range_token = strtok_r (port_list_token, range_delim, &port_range_saveptr);
        if (sscanf (port_range_token, "%" SCNu32 "%c", &port_num, &junk) != 1)
        {
            printf ("Error: %s is not a valid port number\n", port_range_token);
            exit (EXIT_FAILURE);
        }
        range_start_port_num = port_num;

        port_range_token = strtok_r (NULL, range_delim, &port_range_saveptr);
        if (port_range_token != NULL)
        {
            /* A range of port numbers specified */
            if (sscanf (port_range_token, "%" SCNu32 "%c", &port_num, &junk) != 1)
            {
                printf ("Error: %s is not a valid port number\n", port_range_token);
                exit (EXIT_FAILURE);
            }
            range_end_port_num = port_num;

            port_range_token = strtok_r (NULL, range_delim, &port_range_saveptr);
            if (port_range_token != NULL)
            {
                printf ("Error: %s unexpected spurious port range\n", port_range_token);
                exit (EXIT_FAILURE);
            }

            if (range_end_port_num < range_start_port_num)
            {
                printf ("Error: port range end %" PRIu32 " is less than port range start %" PRIu32 "\n",
                        range_end_port_num, range_start_port_num);
                exit (EXIT_FAILURE);
            }
        }
        else
        {
            /* A single port number specified */
            range_end_port_num = port_num;
        }

        for (port_num = range_start_port_num; port_num <= range_end_port_num; port_num++)
        {
            store_tested_port_num (port_num);
        }

        port_list_token = strtok_r (NULL, ports_delim, &port_list_saveptr);
    }
}


/**
 * @brief Read the command line arguments, exiting if an error in the arguments
 * @param[in] argc, argv Command line arguments passed to main
 */
static void read_command_line_arguments (const int argc, char *argv[])
{
    bool mrmac_port_num_specified = false;
    const char *const program_name = argv[0];
    const char *const optstring = "i:n:dt:p:r:";
    int option;
    char junk;
    uint32_t port_num;

    /* Default to testing all defined switch ports */
    num_tested_port_indices = 0;
    for (uint32_t port_index = 0; port_index < NUM_DEFINED_PORTS; port_index++)
    {
        tested_port_indices[num_tested_port_indices] = port_index;
        num_tested_port_indices++;
    }

    /* Process the command line arguments */
    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'i':
            snprintf (arg_mrmac_device_location, sizeof (arg_mrmac_device_location), optarg);
            break;

        case 'n':
            if ((sscanf (optarg, "%" SCNu32 "%c", &port_num, &junk) != 1) || (port_num >= NUM_MRMAC_PORTS))
            {
                printf ("Error: Invalid MRMAC port num %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_mrmac_port_num = port_num;
            mrmac_port_num_specified = true;
            break;

        case 'd':
            arg_frame_debug_enabled = true;
            break;

        case 't':
            if ((sscanf (optarg, "%" SCNi64 "%c", &arg_test_interval_secs, &junk) != 1) ||
                (arg_test_interval_secs <= 0))
            {
                printf ("Error: Invalid <duration_secs> %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case 'p':
            parse_tested_port_list (optarg);
            break;

        case 'r':
            if (sscanf (optarg, "%lf%c", &arg_tested_port_mbps, &junk) != 1)
            {
                printf ("Error: Invalid <rate_mbps> %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;

        case '?':
        default:
            display_usage (program_name);
            break;
        }

        option = getopt (argc, argv, optstring);
    }

    /* Check the expected arguments have been provided */
    if (!mrmac_port_num_specified)
    {
        printf ("Error: The MRMAC port must be specified\n\n");
        display_usage (program_name);
    }

    if (num_tested_port_indices < MIN_TESTED_PORTS)
    {
        printf ("Error: A minimum of %d ports must be tested\n\n", MIN_TESTED_PORTS);
        display_usage (program_name);
    }

    if (optind < argc)
    {
        printf ("Error: Unexpected nonoption (first %s)\n\n", argv[optind]);
        display_usage (program_name);
    }
}


/**
 * @brief Get the configured speed of the MRMAC port used for the test into Mbps
 * @todo Is the link down shortly after VFIO has opened the device, if that resets part of the MRMAC?
 *       If so, add a delay waiting for the link to be up.
 *       Also need to determine when the link is considered "up". With a 10G port which uses only one lane the only bits high
 *       when the link was connected to a switch were:
 *       - MRMAC_STAT_RX_STATUS_MASK
 *       - MRMAC_STAT_RX_BLOCK_LOCK_MASK
 *
 *       TBC if when a link use more than one lane (i.e. 40G, 50G or 100G) if the following bits should also be high
 *       for the link to be considered up:
 *       - MRMAC_STAT_RX_ALIGNED_MASK
 *       - MRMAC_STAT_RX_SYNCED_MASK
 * @param[in] context The initialised context to obtains the speed from
 * @return The rate for the MRMAC port being used for the test
 */
static int64_t get_mrmac_rate_mbps (const frame_tx_rx_thread_context_t *const context)
{
    int64_t rate_mbps = 0;

    const uint32_t mode_reg = read_reg32 (context->mrmac_port_regs, MRMAC_MODE_REG_OFFSET);
    const uint32_t port_data_rate = vfio_extract_field_u32 (mode_reg, MRMAC_CTL_DATA_RATE_MASK);

    switch (port_data_rate)
    {
    case MRMAC_CTL_DATA_RATE_10GE:
        rate_mbps = 10000;
        break;

    case MRMAC_CTL_DATA_RATE_25GE:
        rate_mbps = 25000;
        break;

    case MRMAC_CTL_DATA_RATE_40GE:
        rate_mbps = 40000;
        break;

    case MRMAC_CTL_DATA_RATE_50GE:
        rate_mbps = 50000;
        break;

    case MRMAC_CTL_DATA_RATE_100GE:
        rate_mbps = 100000;
        break;

    default:
        console_printf ("Unknown port_data_rate encoding 0x%x\n", port_data_rate);
        exit (EXIT_FAILURE);
    }

    return rate_mbps;
}


/**
 * @brief Create a EtherCAT test frame which can be sent.
 * @details The EtherCAT commands and datagram contents are not significant; have just used values which populate
 *          a maximum length frame and for which Wireshark reports a valid frame (for debugging).
 * @param frame[out] The populated frame which can be transmitted.
 * @param source_port_index[in] The index into test_ports[] which selects the source MAC address and outgoing VLAN.
 * @param destination_port_index[in] The index into test_ports[] which selects the destination MAC address.
 * @param sequence_number[in] The sequence number to place in the transmitted frame
 */
static void create_test_frame (ethercat_frame_t *const frame,
                               const uint32_t source_port_index, const uint32_t destination_port_index,
                               const uint32_t sequence_number)
{
    memset (frame, 0, sizeof (*frame));

    /* MAC addresses */
    memcpy (frame->destination_mac_addr, test_ports[destination_port_index].mac_addr,
            sizeof (frame->destination_mac_addr));
    memcpy (frame->source_mac_addr, test_ports[source_port_index].mac_addr,
            sizeof (frame->source_mac_addr));

    /* VLAN */
    frame->ether_type = htons (ETH_P_8021Q);
    frame->vlan_tci = htons (test_ports[source_port_index].vlan);

    frame->vlan_ether_type = htons (ETH_P_ETHERCAT);

    frame->Length = sizeof (ethercat_frame_t) - offsetof (ethercat_frame_t, Cmd);
    frame->Type = 1; /* EtherCAT commands */
    frame->Cmd = 11; /* Logical Memory Write */
    frame->Len = ETHERCAT_DATAGRAM_LEN;

    static uint8_t fill_value;
    for (uint32_t data_index = 0; data_index < ETHERCAT_DATAGRAM_LEN; data_index++)
    {
        frame->data[data_index] = fill_value++;
    }

    frame->Address = sequence_number;
}


/**
 * @brief Open the MRMAC device used to send/receive test frames
 * @param[in/out] context The context being initialised.
 */
static void open_mrmac_device (frame_tx_rx_thread_context_t *const context)
{
    /* Apply any device filter specified in the command line arguments */
    if (strlen (arg_mrmac_device_location) > 0)
    {
        vfio_add_pci_device_location_filter (arg_mrmac_device_location);
    }

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&context->designs);

    /* Obtain the MRMAC design to be used for the test, and validate has the port number requested by the command line arguments */
    uint32_t num_designs_with_mrmacs = 0;
    context->mrmac_design = NULL;
    for (uint32_t design_index = 0; design_index < context->designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const candidate_design = &context->designs.designs[design_index];

        if (candidate_design->mrmac.regs != NULL)
        {
            num_designs_with_mrmacs++;
            if (context->mrmac_design == NULL)
            {
                context->mrmac_design = candidate_design;
            }
        }
    }

    if (num_designs_with_mrmacs == 0)
    {
        printf ("Error: Found no design with a supported MRMAC\n");
        exit (EXIT_FAILURE);
    }
    else if (num_designs_with_mrmacs > 1)
    {
        printf ("Error: Found %u designs with MRMAC. Use -i option to select a single design\n", num_designs_with_mrmacs);
        exit (EXIT_FAILURE);
    }
    else if (!context->mrmac_design->mrmac.used_ports[arg_mrmac_port_num])
    {
        printf ("Error: Design %s doesn't have requested MRMAC port %u\n",
                fpga_design_names[context->mrmac_design->design_id], arg_mrmac_port_num);
        exit (EXIT_FAILURE);
    }

    context->mrmac_port_regs = &context->mrmac_design->mrmac.regs[arg_mrmac_port_num * MRMAC_PORT_REGS_FRAME_SIZE];
    context->xdma_overall_success = true;

    /* Limit the burst which can be queued for transmission to the number of ports on the switch under test,
     * to avoid potentially overloading switch ports if the software gets behind.
     *
     * @todo This is only needed when need to limit the transmit frame rate.
     *       If not limiting the transmit frame rate may prevent generating the maximum frame rate on the interface to the injection
     *       switch. */
    context->tx_num_buffers = num_tested_port_indices;

    /* @todo Is this an over estimate? While smaller may make better use of cache not sure how that works
     *       with XDMA (which has to target memory?).
     *       Also, the comment about the rationale for the value of NOMINAL_TOTAL_PENDING_RX_FRAMES has
     *       been copied from the code which used pcap.
     *
     *       With pcap the amount of transmit and receive buffering was not visible to the program.
     *
     *       Whereas with XDMA this program can limit the maximum number of Tx frames which can be queued
     *       and therefore should be determine the maximum number of pending Rx frames after some allowance
     *       for flooded packets / other packets.
     *
     *       However, can the switches delay sending frames? */
    context->rx_num_buffers = NOMINAL_TOTAL_PENDING_RX_FRAMES;

    /* Configure XDMA transmit to use a queue of fixed size buffers, based upon the cache line aligned size of the test frame */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = context->mrmac_design->dma_bridge_memory_size_bytes,
        .dma_bridge_memory_base_address = context->mrmac_design->dma_bridge_memory_base_address,
        .min_size_alignment = 1, /* The host memory is byte addressable */
        .num_descriptors = context->tx_num_buffers,
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = arg_mrmac_port_num,
        .bytes_per_buffer = vfio_align_cache_line_size (sizeof (ethercat_frame_t)),
        .host_buffer_start_offset = 0, /* Separate host buffer used for transmit and receive transfers */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = false,
        .timeout_seconds = XMDA_TRANSFER_TIMEOUT_SECS,
        .vfio_device = context->mrmac_design->vfio_device,
        .bar_index = context->mrmac_design->dma_bridge_bar,
        .descriptors_mapping = &context->descriptors_mapping,
        .data_mapping = &context->h2c_data_mapping,
        .overall_success = &context->xdma_overall_success
    };

    /* Configure XMDA receive to use a queue of fixed size buffers, based upon the cache line aligned size of the test frame.
     *
     * c2h_stream_continuous is used since:
     * a. Allows the number of receive buffers to be larger than X2X_SGDMA_MAX_DESCRIPTOR_CREDITS.
     * b. There is no AXI stream flow-control to stop received frames being dropped / truncated if the receive DMA is unable
     *    to keep up with the receive Ethernet frames.
     * c. Avoid the overhead of the software having to queue receive descriptor credits. */
    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_size_bytes = context->mrmac_design->dma_bridge_memory_size_bytes,
        .dma_bridge_memory_base_address = context->mrmac_design->dma_bridge_memory_base_address,
        .min_size_alignment = 1, /* The host memory is byte addressable */
        .num_descriptors = context->rx_num_buffers,
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = arg_mrmac_port_num,
        .bytes_per_buffer = vfio_align_cache_line_size (sizeof (ethercat_frame_t)),
        .host_buffer_start_offset = 0, /* Separate host buffer used for transmit and receive transfers */
        .card_buffer_start_offset = 0, /* Not used for AXI stream */
        .c2h_stream_continuous = true,
        .timeout_seconds = -1, /* Disable transfer timeout */
        .vfio_device = context->mrmac_design->vfio_device,
        .bar_index = context->mrmac_design->dma_bridge_bar,
        .descriptors_mapping = &context->descriptors_mapping,
        .data_mapping = &context->c2h_data_mapping,
        .overall_success = &context->xdma_overall_success
    };

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (context->mrmac_design->vfio_device, &context->descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Read mapping used by device, for all the transmit buffers */
    allocate_vfio_dma_mapping (context->mrmac_design->vfio_device,
            &context->h2c_data_mapping, h2c_transfer_configuration.num_descriptors * h2c_transfer_configuration.bytes_per_buffer,
            VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Write mapping used by device, for all the receive buffers */
    allocate_vfio_dma_mapping (context->mrmac_design->vfio_device,
            &context->c2h_data_mapping, c2h_transfer_configuration.num_descriptors * c2h_transfer_configuration.bytes_per_buffer,
            VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    context->xdma_overall_success = (context->descriptors_mapping.buffer.vaddr != NULL) &&
                                    (context->h2c_data_mapping.buffer.vaddr    != NULL) &&
                                    (context->c2h_data_mapping.buffer.vaddr    != NULL);
    if (context->xdma_overall_success)
    {
        /* Initialise the transfers.
         * Since c2h_stream_continuous is enabled for receive, this will start the XDMA storing any received frames in memory.
         * This program doesn't attempt to flush any frames which might be in the internal AXI steam FIFOs between the MRMAC
         * receive and the C2H XDMA stream. It is assumed there won't be a large amount of frames before the actual test starts. */
        x2x_initialise_transfer_context (&context->h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&context->c2h_transfer, &c2h_transfer_configuration);
    }

    context->rx_end_of_packet_pending = false;
}


/**
 * @brief Close the MRMAC device used to send/receive test frames, freeing the resources.
 * @param[in/out] context The context being closed.
 */
static void close_mrmac_device (frame_tx_rx_thread_context_t *const context)
{
    x2x_finalise_transfer_context (&context->h2c_transfer);
    x2x_finalise_transfer_context (&context->c2h_transfer);

    report_if_transfer_failed (&context->h2c_transfer);
    report_if_transfer_failed (&context->c2h_transfer);

    free_vfio_dma_mapping (&context->c2h_data_mapping);
    free_vfio_dma_mapping (&context->h2c_data_mapping);
    free_vfio_dma_mapping (&context->descriptors_mapping);
    close_pcie_fpga_designs (&context->designs);
}


/**
 * @brief Write a hexadecimal MAC address to a CSV file
 * @param[in/out] csv_file File to write to
 * @param[in] mac_addr The MAC address to write
 */
static void write_mac_addr (FILE *const csv_file, const uint8_t *const mac_addr)
{
    for (uint32_t byte_index = 0; byte_index < ETHER_MAC_ADDRESS_LEN; byte_index++)
    {
        fprintf (csv_file, "%s%02X", (byte_index == 0) ? ", " : "-", mac_addr[byte_index]);
    }
}


/*
 * @brief Reset the statistics which are accumulated over one test interval
 * @param[in/out] statistics The statistics to reset
 */
static void reset_frame_test_statistics (frame_test_statistics_t *const statistics)
{
    for (frame_record_type_t frame_type = 0; frame_type < FRAME_RECORD_ARRAY_SIZE; frame_type++)
    {
        statistics->frame_counts[frame_type] = 0;
    }
    statistics->total_missing_frames = 0;
    for (uint32_t source_port_index = 0; source_port_index < NUM_DEFINED_PORTS; source_port_index++)
    {
        for (uint32_t destination_port_index = 0; destination_port_index < NUM_DEFINED_PORTS; destination_port_index++)
        {
            port_frame_statistics_t *const port_stats =
                    &statistics->port_frame_statistics[source_port_index][destination_port_index];

            port_stats->num_valid_rx_frames = 0;
            port_stats->num_missing_rx_frames = 0;
            port_stats->num_tx_frames = 0;
        }
    }
}


/**
 * @brief Initialise the context for the transmit/receive thread, for the start of the test
 * @param[out] context The initialised context
 */
static void transmit_receive_initialise (frame_tx_rx_thread_context_t *const context)
{
    context->next_tx_sequence_number = 1;
    context->destination_tested_port_index = 0;
    context->source_port_offset = 1;
    for (uint32_t source_port_index = 0; source_port_index < NUM_DEFINED_PORTS; source_port_index++)
    {
        for (uint32_t destination_port_index = 0; destination_port_index < NUM_DEFINED_PORTS; destination_port_index++)
        {
            pending_rx_frames_t *const pending = &context->pending_rx_frames[source_port_index][destination_port_index];

            pending->pending_rx_sequence_numbers = NULL;
            pending->tx_frame_records = NULL;
            pending->num_pending_rx_frames = 0;
            pending->tx_index = 0;
            pending->rx_index = 0;
        }
    }
    reset_frame_test_statistics (&context->statistics);
    context->statistics.max_pending_rx_frames = 0;
    context->statistics.final_statistics = false;

    if (arg_frame_debug_enabled)
    {
        /* Allocate space to record all expected frames within one test duration */
        const size_t nominal_records_per_test = 3; /* tx frame, copy of tx frame, rx frame */
        const size_t max_frame_rate = 82000; /* Slightly more than max non-jumbo frames can be sent on a 1 Gb link */
        context->frame_recording.allocated_length =
                (uint32_t) (max_frame_rate * nominal_records_per_test * (uint32_t) arg_test_interval_secs);
        context->frame_recording.frame_records = calloc
                (context->frame_recording.allocated_length, sizeof (context->frame_recording.frame_records[0]));
    }
    else
    {
        context->frame_recording.allocated_length = 0;
        context->frame_recording.frame_records = NULL;
    }
    context->frame_recording.num_frame_records = 0;

    /* Calculate the number of pending rx frames which can be stored per tested source / destination port combination.
     * This aims for a nominal total number of pending rx frames divided among the the number of combinations tested. */
    const uint32_t num_tested_port_combinations = num_tested_port_indices * (num_tested_port_indices - 1);
    context->pending_rx_sequence_numbers_length = NOMINAL_TOTAL_PENDING_RX_FRAMES / num_tested_port_combinations;
    if (context->pending_rx_sequence_numbers_length < MIN_PENDING_RX_FRAMES_PER_PORT_COMBO)
    {
        context->pending_rx_sequence_numbers_length = MIN_PENDING_RX_FRAMES_PER_PORT_COMBO;
    }

    /* Allocate space for pending rx frames for each tested source / destination port combination tested */
    const uint32_t pending_rx_sequence_numbers_pool_size =
            num_tested_port_combinations * context->pending_rx_sequence_numbers_length;
    uint32_t *const pending_rx_sequence_numbers_pool = calloc (pending_rx_sequence_numbers_pool_size, sizeof (uint32_t));
    frame_record_t **const tx_frame_records_pool = calloc (pending_rx_sequence_numbers_pool_size, sizeof (frame_record_t *));

    uint32_t pool_index = 0;
    for (uint32_t source_tested_port_index = 0; source_tested_port_index < num_tested_port_indices; source_tested_port_index++)
    {
        const uint32_t source_port_index = tested_port_indices[source_tested_port_index];

        for (uint32_t destination_tested_port_index = 0;
             destination_tested_port_index < num_tested_port_indices;
             destination_tested_port_index++)
        {
            const uint32_t destination_port_index = tested_port_indices[destination_tested_port_index];

            if (source_port_index != destination_port_index)
            {
                pending_rx_frames_t *const pending = &context->pending_rx_frames[source_port_index][destination_port_index];

                pending->pending_rx_sequence_numbers = &pending_rx_sequence_numbers_pool[pool_index];
                pending->tx_frame_records = &tx_frame_records_pool[pool_index];
                pool_index += context->pending_rx_sequence_numbers_length;
            }
        }
    }

    /* Start the timers for statistics collection and frame transmission */
    const int64_t now = get_monotonic_time ();
    context->statistics.interval_start_time = now;
    context->test_interval_end_time = now + (arg_test_interval_secs * NSECS_PER_SEC);

    if (context->tx_rate_limited)
    {
        context->tx_time_of_next_frame = now;
    }
}


/*
 * @brief Get the port index from a MAC address
 * @details This assumes that the last octet of the test frame MAC addresses is the index into the test_ports[] array.
 * @param[in] mac_addr The MAC address in a frame to get the port index for
 * @param[out] port_index The index obtained from the MAC address
 * @return Returns true if the MAC address is one used for the test and the port_index has been obtained, or false otherwise.
 */
static bool get_port_index_from_mac_addr (const uint8_t *const mac_addr, uint32_t *const port_index)
{
    bool port_index_valid;

    *port_index = mac_addr[5];
    port_index_valid = (*port_index < NUM_DEFINED_PORTS);

    if (port_index_valid)
    {
        port_index_valid = memcmp (mac_addr, test_ports[*port_index].mac_addr, ETHER_MAC_ADDRESS_LEN) == 0;
    }

    return port_index_valid;
}


/*
 * @brief When enabled by a command line option, record a transmit/receive frame for debug
 * @param[in/out] context Context to record the frame in
 * @param[in] frame_record The frame to record.
 * @return Returns a pointer to the recorded frame entry, or NULL if not recorded.
 *         Allows the caller to refer to the recorded frame for later updating the frame_missed field.
 */
static frame_record_t *record_frame_for_debug (frame_tx_rx_thread_context_t *const context,
                                               const frame_record_t *const frame_record)
{
    frame_record_t *recorded_frame = NULL;

    if (context->frame_recording.num_frame_records < context->frame_recording.allocated_length)
    {
        recorded_frame = &context->frame_recording.frame_records[context->frame_recording.num_frame_records];
        *recorded_frame = *frame_record;
        recorded_frame->frame_missed = false;
        context->frame_recording.num_frame_records++;
    }

    return recorded_frame;
}


/*
 * @brief Identify if an Ethernet frame is one used by the test.
 * @details If the Ethernet frame is one used by the test also extracts the source/destination port indices
 *          and the sequence number.
 * @param[in] context Used to obtain the start time of the test interval, to populate a relative time.
 * @param[in] rx_transfer_len When non-null the length of the received buffer.
 *                            When NULL indicates are being called to record a transmitted test frame.
 * @param[in] frame The frame to identify
 * @param[out] frame_record Contains information for the identified frame.
 *                          For a receive frame haven't yet performed the checks against the pending receive frames.
 */
static void identify_frame (const frame_tx_rx_thread_context_t *const context,
                            const size_t *const rx_transfer_len, const ethercat_frame_t *const frame,
                            frame_record_t *const frame_record)
{
    if (rx_transfer_len != NULL)
    {
        /* Use the len from the received frame */
        frame_record->len = *rx_transfer_len;
    }
    else
    {
        /* Set the len for the transmitted frame */
        frame_record->len = sizeof (ethercat_frame_t);
    }

    /* Extract MAC addresses, Ethernet type and VLAN ID from the frame, independent of the test frame format */
    const uint16_t ether_type = ntohs (frame->ether_type);
    const uint16_t vlan_ether_type = ntohs (frame->vlan_ether_type);

    frame_record->relative_test_time = get_monotonic_time () - context->statistics.interval_start_time;
    memcpy (frame_record->destination_mac_addr, frame->destination_mac_addr, sizeof (frame_record->destination_mac_addr));
    memcpy (frame_record->source_mac_addr, frame->source_mac_addr, sizeof (frame_record->source_mac_addr));
    frame_record->vlan_present = ether_type == ETH_P_8021Q;
    if (frame_record->vlan_present)
    {
        frame_record->ether_type = vlan_ether_type;
        frame_record->vlan_id = ntohs (frame->vlan_tci);
    }
    else
    {
        frame_record->ether_type = ether_type;
    }

    /* Determine if the frame is one generated by the test program */
    bool is_test_frame = frame_record->len >= sizeof (ethercat_frame_t);

    if (is_test_frame)
    {
        is_test_frame = (ether_type == ETH_P_8021Q) && (vlan_ether_type == ETH_P_ETHERCAT) &&
                get_port_index_from_mac_addr (frame_record->source_mac_addr, &frame_record->source_port_index) &&
                get_port_index_from_mac_addr (frame_record->destination_mac_addr, &frame_record->destination_port_index);
    }

    /* Set the initial identified frame type. FRAME_RECORD_RX_TEST_FRAME may be modified following
     * subsequent checks against the pending receive frames */
    if (is_test_frame)
    {
        frame_record->test_sequence_number = frame->Address;
        frame_record->frame_type = (rx_transfer_len != NULL) ? FRAME_RECORD_RX_TEST_FRAME : FRAME_RECORD_TX_TEST_FRAME;
    }
    else
    {
        frame_record->frame_type = FRAME_RECORD_RX_OTHER;
    }
}


/*
 * @brief Called when a received frame has been identified as a test frame, to update the list of pending frames
 * @param[in/out] context Context to update the pending frames for
 * @param[in/out] frame_record The received frame to compare against the list of pending frames.
 *                             On output the frame_type has been updated to identify which sub-type of receive frame it is.
 */
static void handle_pending_rx_frame (frame_tx_rx_thread_context_t *const context, frame_record_t *const frame_record)
{
    pending_rx_frames_t *const pending =
            &context->pending_rx_frames[frame_record->source_port_index][frame_record->destination_port_index];
    port_frame_statistics_t *const port_stats =
            &context->statistics.port_frame_statistics[frame_record->source_port_index][frame_record->destination_port_index];

    if (frame_record->vlan_id == test_ports[frame_record->destination_port_index].vlan)
    {
        /* The frame was received with the VLAN ID for the expected destination port, compare against the pending frames */
        bool pending_match_found = false;
        while ((!pending_match_found) && (pending->num_pending_rx_frames > 0))
        {
            if (frame_record->test_sequence_number == pending->pending_rx_sequence_numbers[pending->rx_index])
            {
                /* This is an expected pending receive frame */
                port_stats->num_valid_rx_frames++;
                frame_record->frame_type = FRAME_RECORD_RX_TEST_FRAME;
                pending_match_found = true;
            }
            else
            {
                /* The sequence number is not the next expected pending, which means a preceding frame has been missed */
                port_stats->num_missing_rx_frames++;
                context->statistics.total_missing_frames++;
                if (pending->tx_frame_records[pending->rx_index] != NULL)
                {
                    pending->tx_frame_records[pending->rx_index]->frame_missed = true;
                }
            }

            pending->num_pending_rx_frames--;
            pending->tx_frame_records[pending->rx_index] = NULL;
            pending->rx_index = (pending->rx_index + 1) % context->pending_rx_sequence_numbers_length;
        }

        if (!pending_match_found)
        {
            frame_record->frame_type = FRAME_RECORD_RX_UNEXPECTED_FRAME;
        }
    }
    else
    {
        /* Must be a frame flooded to a port other than the intended destination */
        frame_record->frame_type = FRAME_RECORD_RX_FLOODED_FRAME;
    }
}


/*
 * @brief Sequence transmitting the next test frame, cycling around combinations of the source and destination ports
 * @details This also records the frame as pending receipt, identified with the combination of source/destination port
 *          and sequence number.
 * @param[in/out] context Context used transmitting frames.
 */
static void transmit_next_test_frame (frame_tx_rx_thread_context_t *const context)
{
    const uint32_t source_tested_port_index =
            (context->destination_tested_port_index + context->source_port_offset) % num_tested_port_indices;
    const uint32_t destination_port_index = tested_port_indices[context->destination_tested_port_index];
    const uint32_t source_port_index = tested_port_indices[source_tested_port_index];
    pending_rx_frames_t *const pending = &context->pending_rx_frames[source_port_index][destination_port_index];
    port_frame_statistics_t *const port_stats =
            &context->statistics.port_frame_statistics[source_port_index][destination_port_index];

    /* Create the test frame and queue for transmission */
    ethercat_frame_t *const tx_frame = x2x_get_next_h2c_buffer (&context->h2c_transfer);
    create_test_frame (tx_frame, source_port_index, destination_port_index, context->next_tx_sequence_number);
    x2x_start_populated_descriptors (&context->h2c_transfer);

    /* When debug is enabled identify the transmit frame and record it.
     * While the point at which the transmit completion is polled could be the point that which the frame is recorded for debug,
     * that might allow the frame receipt to be seen as completed before the transmit which would confuse the debug. */
    frame_record_t *recorded_frame = NULL;
    if (arg_frame_debug_enabled)
    {
        frame_record_t frame_record;

        identify_frame (context, NULL, tx_frame, &frame_record);
        recorded_frame = record_frame_for_debug (context, &frame_record);
    }

    /* Update transmit frame counts */
    context->statistics.frame_counts[FRAME_RECORD_TX_TEST_FRAME]++;
    port_stats->num_tx_frames++;

    /* If the maximum number of receive frames are pending, then mark the oldest as missing */
    if (pending->num_pending_rx_frames == context->pending_rx_sequence_numbers_length)
    {
        port_stats->num_missing_rx_frames++;
        context->statistics.total_missing_frames++;
        pending->num_pending_rx_frames--;
        if (pending->tx_frame_records[pending->rx_index] != NULL)
        {
            pending->tx_frame_records[pending->rx_index]->frame_missed = true;
        }
        pending->rx_index = (pending->rx_index + 1) % context->pending_rx_sequence_numbers_length;
    }

    /* Record the transmitted frame as pending receipt */
    pending->pending_rx_sequence_numbers[pending->tx_index] = context->next_tx_sequence_number;
    pending->tx_frame_records[pending->tx_index] = recorded_frame;
    pending->tx_index = (pending->tx_index + 1) % context->pending_rx_sequence_numbers_length;
    pending->num_pending_rx_frames++;
    if (pending->num_pending_rx_frames > context->statistics.max_pending_rx_frames)
    {
        context->statistics.max_pending_rx_frames = pending->num_pending_rx_frames;
    }

    /* Advance to the next frame which will be transmitted */
    context->next_tx_sequence_number++;
    context->destination_tested_port_index = (context->destination_tested_port_index + 1) % num_tested_port_indices;
    if (context->destination_tested_port_index == 0)
    {
        context->source_port_offset++;
        if (context->source_port_offset == num_tested_port_indices)
        {
            context->source_port_offset = 1;
        }
    }
}


/*
 * @brief Thread which transmits test frames and checks for receipt of the frames from the switch under test
 * @param[out] arg The context for the thread.
 */
static void *transmit_receive_thread (void *arg)
{
    frame_tx_rx_thread_context_t *const context = arg;
    bool exit_requested = false;
    int64_t now;
    int rc;
    const ethercat_frame_t *rx_frame;
    size_t rx_transfer_len;
    bool rx_end_of_packet;

    transmit_receive_initialise (context);

    /* Run test until requested to exit, or a XMDA error occurs.
     * This gives preference to polling for receipt of test frames, and when no available frame transmits the next test frame.
     * This tries to send frames at the maximum possible rate, and relies upon the poll for frame receipt not causing any
     * frames to be missed. */
    while (!exit_requested && context->xdma_overall_success)
    {
        now = get_monotonic_time ();

        /* Process all outstanding XDMA receive completions */
        rx_frame = x2x_poll_completed_transfer (&context->c2h_transfer, &rx_transfer_len, &rx_end_of_packet);
        while (rx_frame != NULL)
        {
            if (context->rx_end_of_packet_pending)
            {
                /* The receive buffer is a continuation of a previous frame, due to the Ethernet frame being larger than the
                 * test frame used by this program. Just increase the total length of the pending receive frame. */
                context->rx_frame_record.len += rx_transfer_len;
            }
            else
            {
                /* Identify the start of a new received Ethernet frame */
                identify_frame (context, &rx_transfer_len, rx_frame, &context->rx_frame_record);

                /* If there was no end-of-packet in the receive buffer, indicate the end of the received Ethernet frame
                 * is pending in a following buffer. */
                context->rx_end_of_packet_pending = !rx_end_of_packet;
            }

            if (rx_end_of_packet)
            {
                /* When have received the end of one packet, handle the frame */
                if (context->rx_frame_record.frame_type != FRAME_RECORD_RX_OTHER)
                {
                    handle_pending_rx_frame (context, &context->rx_frame_record);
                }
                context->statistics.frame_counts[context->rx_frame_record.frame_type]++;
                record_frame_for_debug (context, &context->rx_frame_record);
                context->rx_end_of_packet_pending = false;
            }


            rx_frame = x2x_poll_completed_transfer (&context->c2h_transfer, &rx_transfer_len, &rx_end_of_packet);
        }

        /* Check for completion of the transmit XDMA transfers.
         * No need to do anything with the completed transfers, since the transmit frames are recorded when they have been queued
         * for transmission rather when the XMDA completes. That means the receive frames can't appear to happen before the transmit,
         * in case of race conditions for checking transmit/receive XMDA completion. */
        while (x2x_poll_completed_transfer (&context->h2c_transfer, NULL, NULL) != NULL)
        {
        }

        /* Determine if can transmit the next frame */
        if (x2x_get_num_free_descriptors (&context->h2c_transfer) == 0)
        {
            /* No available transmit buffer */
        }
        else if (!context->tx_rate_limited)
        {
            /* Transmit frames as quickly as possible */
            transmit_next_test_frame (context);
        }
        else if (now >= context->tx_time_of_next_frame)
        {
            /* Transmit frames, with the rate limited to a maximum */
            transmit_next_test_frame (context);
            context->tx_time_of_next_frame += context->tx_interval;
        }

        if (now > context->test_interval_end_time)
        {
            /* The end of test interval has been reached */
            context->statistics.interval_end_time = now;
            if (arg_frame_debug_enabled)
            {
                exit_requested = true;
            }
            else if (test_stop_requested)
            {
                exit_requested = true;
            }

            /* Publish and then reset statistics for the next test interval */
            rc = sem_wait (&test_statistics_free);
            CHECK_ASSERT (rc == 0);
            context->statistics.final_statistics = exit_requested;
            test_statistics = context->statistics;
            rc = sem_post (&test_statistics_populated);
            CHECK_ASSERT (rc == 0);
            reset_frame_test_statistics (&context->statistics);
            context->statistics.interval_start_time = context->statistics.interval_end_time;
            context->test_interval_end_time += (arg_test_interval_secs * NSECS_PER_SEC);
        }
    }

    close_mrmac_device (context);

    return NULL;
}


/**
 * @brief Write a CSV file which contains a record of the frames sent/received during a test.
 * @details This is used to debug a single test interval.
 * @param[in] frame_debug_csv_filename Name of CSV file to create
 * @param[in] frame_recording The frames which were sent/received during the test
 */
static void write_frame_debug_csv_file (const char *const frame_debug_csv_filename, const frame_records_t *const frame_recording)
{
    /* Create CSV file and write headers */
    FILE *const csv_file = fopen (frame_debug_csv_filename, "w");
    if (csv_file == NULL)
    {
        console_printf ("Failed to create %s\n", frame_debug_csv_filename);
        exit (EXIT_FAILURE);
    }
    fprintf (csv_file, "frame type,relative test time (secs),missed,source switch port,destination switch port,destination MAC addr,source MAC addr,len,ether type,VLAN,test sequence number\n");

    /* Write one row per recorded frame */
    for (uint32_t frame_index = 0; frame_index < frame_recording->num_frame_records; frame_index++)
    {
        const frame_record_t *const frame_record = &frame_recording->frame_records[frame_index];

        fprintf (csv_file, "%s,%.6f,%s",
                frame_record_types[frame_record->frame_type],
                (double) frame_record->relative_test_time / 1E9,
                frame_record->frame_missed ? "Frame missed" : "");
        if (frame_record->frame_type != FRAME_RECORD_RX_OTHER)
        {
            fprintf (csv_file, ",%" PRIu32 ",%" PRIu32,
                    test_ports[frame_record->source_port_index].switch_port_number,
                    test_ports[frame_record->destination_port_index].switch_port_number);
        }
        else
        {
            fprintf (csv_file, ",,");
        }
        write_mac_addr (csv_file, frame_record->destination_mac_addr);
        write_mac_addr (csv_file, frame_record->source_mac_addr);
        fprintf (csv_file, ",%zu,'%04x", frame_record->len, frame_record->ether_type);
        if (frame_record->vlan_present)
        {
            fprintf (csv_file, ",%" PRIu32, frame_record->vlan_id);
        }
        else
        {
            fprintf (csv_file, ",");
        }
        if (frame_record->frame_type != FRAME_RECORD_RX_OTHER)
        {
            fprintf (csv_file, ",%" PRIu32, frame_record->test_sequence_number);
        }
        else
        {
            fprintf (csv_file, ",");
        }
        fprintf (csv_file, "\n");
    }

    fclose (csv_file);
}


/*
 * @brief Write the frame test statistics from the most recent test interval.
 * @details This is written as:
 *          - The console with a overall summary of which combinations of source/destination ports have missed frames.
 *          - A CSV file which has the per-port count of frames.
 * @param[in/out] results_summary Used to maintain a summary of which test intervals have had test failures.
 * @param[in] statistics The statistics from the most recent interval
 */
static void write_frame_test_statistics (results_summary_t *const results_summary,
                                         const frame_test_statistics_t *const statistics)
{
    uint32_t source_tested_port_index;
    uint32_t destination_tested_port_index;
    char time_str[80];
    struct tm broken_down_time;
    struct timeval tod;
    frame_record_type_t frame_type;

    /* Display time when these statistics are reported */
    gettimeofday (&tod, NULL);
    const time_t tod_sec = tod.tv_sec;
    const int64_t tod_msec = tod.tv_usec / 1000;
    localtime_r (&tod_sec, &broken_down_time);
    strftime (time_str, sizeof (time_str), "%H:%M:%S", &broken_down_time);
    size_t str_len = strlen (time_str);
    snprintf (&time_str[str_len], sizeof (time_str) - str_len, ".%03" PRIi64, tod_msec);

    console_printf ("\n%s\n", time_str);

    /* Print header for counts */
    const int count_field_width = 13;
    for (frame_type = 0; frame_type < FRAME_RECORD_ARRAY_SIZE; frame_type++)
    {
        console_printf ("%*s  ", count_field_width, frame_record_types[frame_type]);
    }
    console_printf ("%*s  %*s  %*s\n", count_field_width, "missed frames", count_field_width, "tx rate (Hz)", count_field_width, "per port Mbps");

    /* Display the count of the different frame types during the test interval.
     * Even when no missing frames the count of the transmit and receive frames may be different due to frames
     * still in flight at the end of the test interval. */
    for (frame_type = 0; frame_type < FRAME_RECORD_ARRAY_SIZE; frame_type++)
    {
        console_printf ("%*" PRIu32 "  ", count_field_width, statistics->frame_counts[frame_type]);
    }

    /* Report the total number of missing frames during the test interval */
    console_printf ("%*" PRIu32 "  ", count_field_width, statistics->total_missing_frames);

    /* Report the average frame rate achieved over the statistics interval */
    const double statistics_interval_secs = (double) (statistics->interval_end_time - statistics->interval_start_time) / 1E9;
    const double frame_rate = (double) statistics->frame_counts[FRAME_RECORD_TX_TEST_FRAME] / statistics_interval_secs;
    console_printf ("%*.1f  ", count_field_width, frame_rate);

    /* Report the average bit rate generated for each switch port under test */
    const double per_port_mbps = ((frame_rate * (double) TEST_PACKET_BITS) / (double) num_tested_port_indices) / 1E6;
    console_printf ("%*.2f\n", count_field_width, per_port_mbps);

    /* Display summary of missed frames over combination of source / destination ports */
    console_printf ("\nSummary of missed frames : '.' none missed 'S' some missed 'A' all missed\n");
    console_printf ("Source  Destination ports --->\n");
    for (uint32_t header_row = 0; header_row < 2; header_row++)
    {
        console_printf ("%s", (header_row == 0) ? "  port  " : "        ");
        for (destination_tested_port_index = 0;
             destination_tested_port_index < num_tested_port_indices;
             destination_tested_port_index++)
        {
            char port_num_text[3];

            snprintf (port_num_text, sizeof (port_num_text), "%2" PRIu32,
                    test_ports[tested_port_indices[destination_tested_port_index]].switch_port_number);
            console_printf ("%c", port_num_text[header_row]);
        }
        console_printf ("\n");
    }

    for (source_tested_port_index = 0; source_tested_port_index < num_tested_port_indices; source_tested_port_index++)
    {
        const uint32_t source_port_index = tested_port_indices[source_tested_port_index];

        console_printf ("    %2" PRIu32 "  ", test_ports[source_port_index].switch_port_number);
        for (destination_tested_port_index = 0;
             destination_tested_port_index < num_tested_port_indices;
             destination_tested_port_index++)
        {
            const uint32_t destination_port_index = tested_port_indices[destination_tested_port_index];
            const port_frame_statistics_t *const port_statistics =
                    &statistics->port_frame_statistics[source_port_index][destination_port_index];
            char port_status;

            if (source_port_index == destination_port_index)
            {
                port_status = ' ';
            }
            else if (port_statistics->num_missing_rx_frames == 0)
            {
                port_status = '.';
            }
            else if (port_statistics->num_valid_rx_frames > 0)
            {
                port_status = 'S';
            }
            else
            {
                port_status = 'A';
            }
            console_printf ("%c", port_status);
        }
        console_printf ("\n");
    }

    /* Any missed frames counts as a test failure */
    if (statistics->total_missing_frames > 0)
    {
        results_summary->num_test_intervals_with_failures++;
        snprintf (results_summary->time_of_last_failure, sizeof (results_summary->time_of_last_failure), "%s", time_str);
    }

    /* Create per-port counts CSV file on first call, and write column headers */
    if (results_summary->per_port_counts_csv_file == NULL)
    {
        results_summary->per_port_counts_csv_file = fopen (results_summary->per_port_counts_csv_filename, "w");
        if (results_summary->per_port_counts_csv_file == NULL)
        {
            console_printf ("Failed to create %s\n", results_summary->per_port_counts_csv_filename);
            exit (EXIT_FAILURE);
        }
        fprintf (results_summary->per_port_counts_csv_file,
                "Time,Source switch port,Destination switch port,Num tx frames,Num valid rx frames,Num missing rx frames\n");
    }

    /* Write one row containing the number of frames per combination of source and destination switch ports tested */
    for (source_tested_port_index = 0; source_tested_port_index < num_tested_port_indices; source_tested_port_index++)
    {
        for (destination_tested_port_index = 0;
             destination_tested_port_index < num_tested_port_indices;
             destination_tested_port_index++)
        {
            if (source_tested_port_index != destination_tested_port_index)
            {
                const uint32_t source_port_index = tested_port_indices[source_tested_port_index];
                const uint32_t destination_port_index = tested_port_indices[destination_tested_port_index];
                const port_frame_statistics_t *const port_statistics =
                        &statistics->port_frame_statistics[source_port_index][destination_port_index];

                fprintf (results_summary->per_port_counts_csv_file,
                        " %s,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n",
                        time_str,
                        test_ports[source_port_index].switch_port_number,
                        test_ports[destination_port_index].switch_port_number,
                        port_statistics->num_tx_frames,
                        port_statistics->num_valid_rx_frames,
                        port_statistics->num_missing_rx_frames);
            }
        }
    }

    /* Display overall summary of test failures */
    console_printf ("Total test intervals with failures = %" PRIu32, results_summary->num_test_intervals_with_failures);
    if (results_summary->num_test_intervals_with_failures)
    {
        console_printf (" : last failure %s\n", (statistics->total_missing_frames > 0) ? "NOW" : results_summary->time_of_last_failure);
    }
    else
    {
        console_printf ("\n");
    }
}


int main (int argc, char *argv[])
{
    int rc;

    /* Check that ethercat_frame_t has the expected size */
    check_assert (sizeof (ethercat_frame_t) == 1518,
            "sizeof (ethercat_frame_t) unexpected value of %" PRIuPTR, sizeof (ethercat_frame_t));

    /* Read the commandline arguments */
    read_command_line_arguments (argc, argv);

    /* Open the MRMAC device specified on the command line, which validates the device is present and supports
     * the functionality required by this program. */
    frame_tx_rx_thread_context_t *const tx_rx_thread_context = calloc (1, sizeof (*tx_rx_thread_context));
    open_mrmac_device (tx_rx_thread_context);
    if (!tx_rx_thread_context->xdma_overall_success)
    {
        printf ("XDMA initialisation failed\n");
        report_if_transfer_failed (&tx_rx_thread_context->h2c_transfer);
        report_if_transfer_failed (&tx_rx_thread_context->c2h_transfer);
        exit (EXIT_FAILURE);
    }

    /* Try and avoid page faults while running */
    rc = mlockall (MCL_CURRENT | MCL_FUTURE);
    if (rc != 0)
    {
        printf ("mlockall() failed\n");
    }

    /* Initialise the semaphores used to control access to the test interval statistics */
    rc = sem_init (&test_statistics_free, 0, 1);
    CHECK_ASSERT (rc == 0);
    rc = sem_init (&test_statistics_populated, 0, 0);
    CHECK_ASSERT (rc == 0);

    /* Set filenames which contain the output files containing the date/time and OS used  */
    results_summary_t results_summary = {{0}};
    const time_t tod_now = time (NULL);
    struct tm broken_down_time;
    char date_time_str[80];
    char frame_debug_csv_filename[160];
    char console_filename[160];

    localtime_r (&tod_now, &broken_down_time);
    strftime (date_time_str, sizeof (date_time_str), "%Y%m%dT%H%M%S", &broken_down_time);
    snprintf (frame_debug_csv_filename, sizeof (frame_debug_csv_filename), "%s_frames_debug_%s.csv", date_time_str, OS_NAME);
    snprintf (console_filename, sizeof (console_filename), "%s_console_%s.txt", date_time_str, OS_NAME);
    snprintf (results_summary.per_port_counts_csv_filename, sizeof (results_summary.per_port_counts_csv_filename),
            "%s_per_port_counts_%s.csv", date_time_str, OS_NAME);

    console_file = fopen (console_filename, "wt");
    if (console_file == NULL)
    {
        fprintf (stderr, "Failed to create %s\n", console_filename);
        exit (EXIT_FAILURE);
    }

    /* Get the bit rate of the MRMAC port used by this program */
    const int64_t injection_port_bit_rate_mbps = get_mrmac_rate_mbps (tx_rx_thread_context);
    const int64_t injection_port_bit_rate = 1000000L * injection_port_bit_rate_mbps;
    console_printf ("Bit rate on interface to injection switch = %ld (Mbps)\n", injection_port_bit_rate_mbps);

    /* The requested bit rate to be generated across all the switch ports under test */
    const int64_t requested_switch_under_test_bit_rate = lround (arg_tested_port_mbps * 1E6) * num_tested_port_indices;
    console_printf ("Requested bit rate to be generated on each switch port under test = %.2f (Mbps)\n", arg_tested_port_mbps);

    /* Decide if need to limit the transmitted frame rate or not */
    if (injection_port_bit_rate > requested_switch_under_test_bit_rate)
    {
        const double limited_frame_rate = (double) requested_switch_under_test_bit_rate / (double) TEST_PACKET_BITS;
        tx_rx_thread_context->tx_interval = lround (1E9 / limited_frame_rate);
        tx_rx_thread_context->tx_rate_limited = true;
        console_printf ("Limiting max frame rate to %.1f Hz, as bit-rate on interface to injection switch exceeds that across all switch ports under test\n",
                limited_frame_rate);
    }
    else
    {
        tx_rx_thread_context->tx_rate_limited = false;
        console_printf ("Not limiting frame rate, as bit-rate on interface to injection switch doesn't exceed the total across all switch ports under test\n");
    }

    /* Report the command line arguments used */
    console_printf ("Writing per-port counts to %s\n", results_summary.per_port_counts_csv_filename);
    console_printf ("Using design %s device %s port %u\n",
            fpga_design_names[tx_rx_thread_context->mrmac_design->design_id],
            tx_rx_thread_context->mrmac_design->vfio_device->device_name, arg_mrmac_port_num);
    console_printf ("Test interval = %" PRIi64 " (secs)\n", arg_test_interval_secs);
    console_printf ("Frame debug enabled = %s\n", arg_frame_debug_enabled ? "Yes" : "No");

    /* Create the transmit_receive_thread */
    pthread_t tx_rx_thread_handle;

    rc = pthread_create (&tx_rx_thread_handle, NULL, transmit_receive_thread, tx_rx_thread_context);
    CHECK_ASSERT (rc == 0);

    /* Report that the test has started */
    if (arg_frame_debug_enabled)
    {
        console_printf ("Running for a single test interval to collect debug information\n");
    }
    else
    {
#ifdef _WIN32
        signal (SIGINT, stop_test_handler);
#else
        struct sigaction action;

        memset (&action, 0, sizeof (action));
        action.sa_handler = stop_test_handler;
        action.sa_flags = SA_RESTART;
        rc = sigaction (SIGINT, &action, NULL);
        CHECK_ASSERT (rc == 0);
#endif
        console_printf ("Press Ctrl-C to stop test at end of next test interval\n");
    }

    /* Report the statistics for each test interval, stopping when get the final statistics */
    bool exit_requested = false;
    while (!exit_requested)
    {
        /* Wait for the statistics upon completion of a test interval */
        rc = sem_wait (&test_statistics_populated);
        CHECK_ASSERT (rc == 0);

        /* Report the statistics */
        write_frame_test_statistics (&results_summary, &test_statistics);
        exit_requested = test_statistics.final_statistics;

        /* Indicate the main thread has completed using the test_statistics */
        rc = sem_post (&test_statistics_free);
        CHECK_ASSERT (rc == 0);
    }

    /* Wait for the transmit_receive_thread to exit */
    rc = pthread_join (tx_rx_thread_handle, NULL);
    CHECK_ASSERT (rc == 0);

    console_printf ("Max pending rx frames = %" PRIu32 " out of %" PRIu32 "\n",
            tx_rx_thread_context->statistics.max_pending_rx_frames, tx_rx_thread_context->pending_rx_sequence_numbers_length);

    /* Write the debug frame recording information if enabled */
    if (arg_frame_debug_enabled)
    {
        write_frame_debug_csv_file (frame_debug_csv_filename, &tx_rx_thread_context->frame_recording);
    }

    fclose (results_summary.per_port_counts_csv_file);
    fclose (console_file);

    return EXIT_SUCCESS;
}
