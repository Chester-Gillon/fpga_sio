/*
 * @file test_dma_bridge_memory_addressing.c
 * @date 27 Jun 2026
 * @author Chester Gillon
 * @brief Test DMA accessible memory in a way which looks for addressing issues.
 * @details
 *  Unlike test_dma_bridge this doesn't attempt to test all of the memory. Instead writes to increasing power-of-two addresses
 *  to find if writes to one address alias to another.
 *
 *  It was written when test_dma_bridge reported failures for the U200_dma_ddr4 design where:
 *  a. The memory_fixed_buffers and memory_variable_transfers options which readback the memory as the test progresses didn't report
 *     a failure.
 *  b. The memory_host_chunks option which writes all of the memory before reading back did report a failure.
 *
 *  From running test_dma_bridge with different options appeared that the four 16GB DDR4 channels in U200_dma_ddr4 were actually
 *  only 8GB each. This program was created to show that more clearly.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_dma_bridge_transfers.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#include <immintrin.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


/* Use a single fixed transfer timeout, to stop the test from hanging */
#define TRANSFER_TIMEOUT_SECS 5


/* Command line arguments for overriding the definition of DMA accessible memory for the design */
static size_t arg_dma_bridge_memory_base_address;
static bool arg_dma_bridge_memory_base_address_specified;
static size_t arg_dma_bridge_memory_size_bytes;
static bool arg_dma_bridge_memory_size_bytes_specified;

/* Command line argument for the maximum block size */
static size_t arg_max_block_size_bytes = UINT64_MAX;


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"device", required_argument, NULL, 0},
    {"dma_bridge_memory_base_address", required_argument, NULL, 0},
    {"dma_bridge_memory_size_bytes", required_argument, NULL, 0},
    {"max_block_size_bytes", required_argument, NULL, 0},
    {NULL, 0, NULL, 0}
};


/* One element used in the memory addressing test */
typedef struct
{
    /* The address in memory this element is written to, so that if the memory addressing isn't working as expected can determine
     * the source of an incorrect access. */
    uint64_t address;
    /* A random value, as a way of excluding any "stale" data from a previous test from causing a pass. */
    uint64_t random;
} memory_test_element_t;


/* Allows for 64-bit addressing and upper and lower elements, and multiple channels */
#define MAX_MEMORY_TEST_ELEMENTS 512


/* The readback status for one memory element tested */
typedef enum
{
    /* The element has the expected content after every test iteration */
    ELEMENT_READBACK_CORRECT,
    /* The element had incorrect contents, which doesn't match that of the element just written.
     * This points at a fault other than incorrect addressing. */
    ELEMENT_READBACK_INCORRECT,
    /* The element had it contents modified to that of a different element just written.
     * This points at a fault with incorrect addressing. */
    ELEMENT_READBACK_OVERWRITTEN
} element_readback_status_t;


/* The context for the memory addressing test */
typedef struct
{
    /* Overall success for DMA transfers. The test aborts when a DMA transfer fails, since can't then check the memory contents */
    bool transfer_success;
    /* Overall success for checking the memory has the expected contents. Test continues if the memory readback has incorrect
     * contents, to attempt to characterise the issue. */
    bool content_success;
    /* Allocates space for the write and read descriptors */
    vfio_dma_mapping_t descriptors_mapping;
    /* Used to write a single memory test element at a time */
    vfio_dma_mapping_t write_data_mapping;
    /* Used to read a variable number of memory tests elements at a time, up to MAX_MEMORY_TEST_ELEMENTS.
     * After the next memory test element is written, reads back the memory test elements written to far in order to detect
     * if not all the address bits are working. */
    vfio_dma_mapping_t read_data_mapping;
    /* Points at the host memory of read_data_mapping for memory element readback */
    const memory_test_element_t *read_elements;
    x2x_transfer_context_t h2c_transfer;
    x2x_transfer_context_t c2h_transfer;
    /* The total number of memory elements to be written during the test */
    uint32_t num_write_elements;
    /* The memory elements to be written during the test */
    memory_test_element_t write_elements[MAX_MEMORY_TEST_ELEMENTS];
    /* The number of times append_memory_test_element() was called.
     * If this is more than num_write_elements then something went wrong with trying to test all of the address range of the
     * memory in terms of power-of-two block sizes. */
    uint32_t num_attempted_write_elements;
    /* The number of entries in write_elements[] which have been written so far during the test */
    uint32_t num_elements_written;
    /* The status for each memory element during the test */
    element_readback_status_t readback_status[MAX_MEMORY_TEST_ELEMENTS];
    /* Contains the previous value readback for each memory element, to report when changes */
    memory_test_element_t previous_readback[MAX_MEMORY_TEST_ELEMENTS];
} memory_addressing_test_context_t;


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("  test_dma_bridge_memory_addressing <options>   Test Xilinx DMA accessible memory addressing\n");
    printf ("\n");
    printf ("--device <domain>:<bus>:<dev>.<func>\n");
    printf ("  only open using VFIO specific PCI devices in the event that there is one than\n");
    printf ("  one PCI device which matches the identity filters.\n");
    printf ("  May be used more than once.\n");
    printf ("--dma_bridge_memory_base_address <address>\n");
    printf ("  Overrides the dma_bridge_memory_base_address specified for the design.\n");
    printf ("  To either reduce the memory tested, or investigate accessing non-existent memory\n");
    printf ("--dma_bridge_memory_size_bytes <size>\n");
    printf ("  Overrides the dma_bridge_memory_size_bytes specified for the design.\n");
    printf ("--max_block_size_bytes <size>\n");
    printf ("  Set the maximum block size for the memory test, for when the design has multiple\n");
    printf ("  memory blocks / channels used for the memory\n");

    exit (EXIT_FAILURE);
}


/**
 * @brief Parse the command line arguments, storing the results in global variables
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    int opt_status;
    char junk;

    do
    {
        int option_index = 0;

        opt_status = getopt_long (argc, argv, "", command_line_options, &option_index);
        if (opt_status == '?')
        {
            display_usage ();
        }
        else if (opt_status >= 0)
        {
            const struct option *const optdef = &command_line_options[option_index];

            if (optdef->flag != NULL)
            {
                /* Argument just sets a flag */
            }
            else if (strcmp (optdef->name, "device") == 0)
            {
                vfio_add_pci_device_location_filter (optarg);
            }
            else if (strcmp (optdef->name, "dma_bridge_memory_base_address") == 0)
            {
                if (sscanf (optarg, "%zi%c", &arg_dma_bridge_memory_base_address, &junk) != 1)
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
                arg_dma_bridge_memory_base_address_specified = true;
            }
            else if (strcmp (optdef->name, "dma_bridge_memory_size_bytes") == 0)
            {
                if ((sscanf (optarg, "%zi%c", &arg_dma_bridge_memory_size_bytes, &junk) != 1) ||
                    (arg_dma_bridge_memory_size_bytes == 0))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
                arg_dma_bridge_memory_size_bytes_specified = true;
            }
            else if (strcmp (optdef->name, "max_block_size_bytes") == 0)
            {
                if ((sscanf (optarg, "%zi%c", &arg_max_block_size_bytes, &junk) != 1) ||
                    (arg_max_block_size_bytes == 0))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
            }
            else
            {
                /* This is a program error, and shouldn't be triggered by the command line options */
                fprintf (stderr, "Unexpected argument definition %s\n", optdef->name);
                exit (EXIT_FAILURE);
            }
        }
    } while (opt_status != -1);
}


/**
 * @brief Get a 64-bit random value for use in a memory test
 * @return The random value
 */
static uint64_t get_random (void)
{
#ifdef __RDRND__
    /* Obtain hardware generated random value */
    unsigned long long random_value;

    do
    {
    } while (!_rdrand64_step (&random_value));

    return random_value;
#else
    /* Not compiled with support for hardware random generation instructions, fall back to Linux /dev/random.
     * To simplify the interface to this function, opens /dev/random on every call. */
    uint64_t random_value;
    const int random_fd = open ("/dev/random", O_RDONLY);

    if (random_fd == -1)
    {
        fprintf (stderr, "Failed to open /dev/random\n");
        exit (EXIT_FAILURE);
    }

    const ssize_t bytes_read = read (random_fd, &random_value, sizeof (random_value));
    if (bytes_read != sizeof (random_value))
    {
        fprintf (stderr, "Only read %zd out of%zu bytes from /dev/random\n", bytes_read, sizeof (random_value));
        exit (EXIT_FAILURE);
    }

    close (random_fd);

    return random_value;
#endif
}


/**
 * @brief Append one memory element to the list to be tested, adding random data for the element
 * @param[in,out] test_context Where to append the memory element
 * @param[in] address The address of the memory element which is to be tested
 */
static void append_memory_test_element (memory_addressing_test_context_t *const test_context, const uint64_t address)
{
    if (test_context->num_write_elements < MAX_MEMORY_TEST_ELEMENTS)
    {
        test_context->write_elements[test_context->num_write_elements].address = address;
        test_context->write_elements[test_context->num_write_elements].random = get_random ();
        test_context->num_write_elements++;

    }
    test_context->num_attempted_write_elements++;
}


/**
 * @brief Initialise the memory elements for be tested, to cover the addressing range to be tested.
 * @param[in] design Defines the amount of DMA accessible memory in the design
 * @param[in/out] test_context On exit the elements[] array has been initialised with the memory elements to be tested.
 */
static void initialise_memory_test_elements (const fpga_design_t *const design,
                                             memory_addressing_test_context_t *const test_context)
{
    /* Exclusive end address to simplify the code */
    const uint64_t memory_end_address = design->dma_bridge_memory_base_address + design->dma_bridge_memory_size_bytes;
    uint64_t region_base_address;
    uint64_t region_lower_offset_bytes;
    uint64_t region_size_bytes;
    uint64_t region_alignment_mask;
    uint64_t previous_region_size_bytes;
    uint32_t num_unaligned_elements;

    /* Try and test the address range of the memory, allowing for potentially the overall memory range being a number
     * of different adjacent power-of-two regions. */
    num_unaligned_elements = 0;
    test_context->num_write_elements = 0;
    test_context->num_attempted_write_elements = 0;
    region_base_address = design->dma_bridge_memory_base_address;
    do
    {
        /* (re-)set the region size to the minimum for a lower and upper element */
        region_lower_offset_bytes = 0;
        previous_region_size_bytes = 0;
        region_size_bytes = sizeof (memory_test_element_t) * 2;
        region_alignment_mask = region_size_bytes - 1u;

        /* Test blocks of increasing powers-of-two which fit in the memory */
        while ((region_size_bytes <= arg_max_block_size_bytes) && ((region_base_address + region_size_bytes) <= memory_end_address))
        {
            const uint64_t region_lower_address = region_base_address + region_lower_offset_bytes;
            const uint64_t region_upper_address = region_base_address + ((region_size_bytes) - sizeof (memory_test_element_t));
            if ((region_lower_address & region_alignment_mask) != 0)
            {
                /* While allow testing on unaligned elements, e.g. if the command line options to override the amount of memory
                 * tested are used, count how many are unaligned to report a warning. */
                num_unaligned_elements++;
            }

            /* Test the lower address of the element */
            append_memory_test_element (test_context, region_lower_address);

            /* Test the upper address of the block */
            append_memory_test_element (test_context, region_upper_address);

            region_lower_offset_bytes = region_size_bytes;
            previous_region_size_bytes = region_size_bytes;
            region_alignment_mask = region_size_bytes - 1u;
            region_size_bytes <<= 1u;
        }

        region_base_address += previous_region_size_bytes;
    } while (region_base_address < memory_end_address);

    if (num_unaligned_elements > 0)
    {
        printf ("Warning: %u unaligned memory elements\n", num_unaligned_elements);
    }
    if (test_context->num_attempted_write_elements > test_context->num_write_elements)
    {
        printf ("Warning: num_attempted_write_elements=%u more than num_write_elements=%u\n",
                test_context->num_attempted_write_elements, test_context->num_write_elements);
    }
}


/**
 * @brief Write the next memory element into card memory
 * @param[in/out] test_context The test context to use.
 */
static void write_next_memory_element (memory_addressing_test_context_t *const test_context)
{
    const memory_test_element_t *const write_element = &test_context->write_elements[test_context->num_elements_written];
    const uint64_t host_buffer_offset = 0u; /* Only a single write buffer */
    const uint64_t card_buffer_offset = write_element->address;
    memory_test_element_t *h2c_buffer = NULL;

    /* Get host buffer */
    h2c_buffer = x2x_populate_memory_transfer (&test_context->h2c_transfer, sizeof (memory_test_element_t),
            host_buffer_offset, card_buffer_offset);
    X2X_ASSERT (&test_context->h2c_transfer, h2c_buffer != NULL);

    if (h2c_buffer != NULL)
    {
        /* Write to card memory */
        *h2c_buffer = *write_element;
        x2x_start_populated_descriptors (&test_context->h2c_transfer);

        /* Wait for write to complete */
        do
        {
            h2c_buffer = x2x_poll_completed_transfer (&test_context->h2c_transfer, NULL, NULL);
        } while (test_context->transfer_success && (h2c_buffer == NULL));
    }

    if (test_context->transfer_success)
    {
        test_context->num_elements_written++;
    }
}


/**
 * @brief Readback the contents of all memory elements which have been written so far during the test
 * @param[in/out] test_context The test context to use.
 */
static void readback_memory_elements (memory_addressing_test_context_t *const test_context)
{
    uint32_t element_index;
    memory_test_element_t *c2h_buffer;

    /* Start reads of the elements */
    for (element_index = 0; test_context->transfer_success && (element_index < test_context->num_elements_written); element_index++)
    {
        const uint64_t host_buffer_offset = element_index * sizeof (memory_test_element_t);
        const uint64_t card_buffer_offset = test_context->write_elements[element_index].address;
        c2h_buffer = x2x_populate_memory_transfer (&test_context->c2h_transfer, sizeof (memory_test_element_t),
                host_buffer_offset, card_buffer_offset);
        X2X_ASSERT (&test_context->c2h_transfer, c2h_buffer != NULL);
        x2x_start_populated_descriptors (&test_context->c2h_transfer);
    }

    /* Wait for reads to complete */
    for (element_index = 0; test_context->transfer_success && (element_index < test_context->num_elements_written); element_index++)
    {
        do
        {
            c2h_buffer = x2x_poll_completed_transfer (&test_context->c2h_transfer, NULL, NULL);
        } while (test_context->transfer_success && (c2h_buffer == NULL));
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
        printf ("  %s failure : %s%s\n",
                (context->configuration.channels_submodule == DMA_SUBMODULE_H2C_CHANNELS) ? "H2C" : "C2H",
                context->error_message,
                context->timeout_awaiting_idle_at_finalisation ? " (+timeout waiting for idle at finalisation)" : "");
    }
}


/**
 * @brief Test the memory addressing for one design
 * @param[in] design The design containing the DMA bridge with DMA accessible memory to test
 * @param[in/out] vfio_device The device containing the DMA bridge to test
 * @param[in/out] test_context The context for the memory test.
 *                             On entry the elements[] array has been initialised with the memory elements to be tested.
 * @return Returns true if the test passed, or false otherwise
 */
static bool test_memory_addressing (const fpga_design_t *const design, vfio_device_t *const vfio_device,
                                    memory_addressing_test_context_t *const test_context)
{
    /* Since as focused of testing the memory, just use fixed channels */
    const uint32_t h2c_channel_id = 0;
    const uint32_t c2h_channel_id = 0;

    /* Populate the transfer configurations to be used.
     * The dma_bridge_memory_base_address is set to zero since this program works on physical addresses of the memory,
     * rather than offsets from the start of the accessible memory. */
    const x2x_transfer_configuration_t h2c_transfer_configuration =
    {
        .dma_bridge_memory_base_address = 0,
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_base_address + design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = 1, /* Only writes one memory element at a time */
        .channels_submodule = DMA_SUBMODULE_H2C_CHANNELS,
        .channel_id = h2c_channel_id,
        .bytes_per_buffer = 0, /* Length and offsets set before each each transfer */
        .host_buffer_start_offset = 0,
        .card_buffer_start_offset = 0,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &test_context->descriptors_mapping,
        .data_mapping = &test_context->write_data_mapping,
        .overall_success = &test_context->transfer_success
    };

    const x2x_transfer_configuration_t c2h_transfer_configuration =
    {
        .dma_bridge_memory_base_address = 0,
        .dma_bridge_memory_size_bytes = design->dma_bridge_memory_base_address + design->dma_bridge_memory_size_bytes,
        .min_size_alignment = 1, /* The card memory is byte addressable */
        .num_descriptors = test_context->num_write_elements, /* Can readback in one go up to all memory elements */
        .channels_submodule = DMA_SUBMODULE_C2H_CHANNELS,
        .channel_id = c2h_channel_id,
        .bytes_per_buffer = 0, /* Length and offsets set before each each transfer */
        .host_buffer_start_offset = 0,
        .card_buffer_start_offset = 0,
        .timeout_seconds = TRANSFER_TIMEOUT_SECS,
        .vfio_device = vfio_device,
        .bar_index = design->dma_bridge_bar,
        .descriptors_mapping = &test_context->descriptors_mapping,
        .data_mapping = &test_context->read_data_mapping,
        .overall_success = &test_context->transfer_success
    };

    /* Create read/write mapping for DMA descriptors */
    const size_t descriptors_allocation_size = x2x_get_descriptor_allocation_size (&h2c_transfer_configuration) +
            x2x_get_descriptor_allocation_size (&c2h_transfer_configuration);
    allocate_vfio_dma_mapping (vfio_device, &test_context->descriptors_mapping, descriptors_allocation_size,
            VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Write mapping used by device, for a single memory element */
    allocate_vfio_dma_mapping (vfio_device, &test_context->write_data_mapping, sizeof (memory_test_element_t),
            VFIO_DMA_MAP_FLAG_READ, VFIO_BUFFER_ALLOCATION_HEAP);

    /* Read mapping used by device, for all the memory elements */
    allocate_vfio_dma_mapping (vfio_device, &test_context->read_data_mapping,
            test_context->num_write_elements * sizeof (memory_test_element_t),
            VFIO_DMA_MAP_FLAG_WRITE, VFIO_BUFFER_ALLOCATION_HEAP);

    test_context->transfer_success = (test_context->descriptors_mapping.buffer.vaddr != NULL) &&
              (test_context->write_data_mapping.buffer.vaddr != NULL) &&
              (test_context->read_data_mapping.buffer.vaddr != NULL);
    test_context->content_success = true;

    if (test_context->transfer_success)
    {
        /* Initialise the transfers */
        test_context->read_elements = test_context->read_data_mapping.buffer.vaddr;
        x2x_initialise_transfer_context (&test_context->h2c_transfer, &h2c_transfer_configuration);
        x2x_initialise_transfer_context (&test_context->c2h_transfer, &c2h_transfer_configuration);

        /* Sequence the test */
        test_context->num_elements_written = 0;
        while (test_context->transfer_success && (test_context->num_elements_written < test_context->num_write_elements))
        {
            const uint32_t num_previous_elements_to_check = test_context->num_elements_written;
            const uint32_t written_element_index = test_context->num_elements_written;

            /* Write the next memory element for the test */
            write_next_memory_element (test_context);

            /* Readback all the memory elements written so far during the test */
            readback_memory_elements (test_context);

            /* Check if the memory element just written reads back correctly */
            if (memcmp (&test_context->write_elements[written_element_index], &test_context->read_elements[written_element_index],
                    sizeof (memory_test_element_t)) == 0)
            {
                test_context->readback_status[written_element_index] = ELEMENT_READBACK_CORRECT;
            }
            else
            {
                test_context->readback_status[written_element_index] = ELEMENT_READBACK_INCORRECT;
                test_context->content_success = false;
                printf ("Element readback incorrect: Expected 0x%zx 0x%zx Actual 0x%zx 0x%zx\n",
                        test_context->write_elements[written_element_index].address,
                        test_context->write_elements[written_element_index].random,
                        test_context->read_elements[written_element_index].address,
                        test_context->read_elements[written_element_index].random);
            }
            test_context->previous_readback[written_element_index] = test_context->read_elements[written_element_index];

            /* Check if the previous elements written still have the expected contents */
            for (uint32_t readback_index = 0; readback_index < num_previous_elements_to_check; readback_index++)
            {
                if (memcmp (&test_context->write_elements[readback_index], &test_context->read_elements[readback_index],
                        sizeof (memory_test_element_t)) != 0)
                {
                    /* Memory element doesn't have the expected contents. Determine which failure to report. */
                    if ((test_context->readback_status[readback_index] == ELEMENT_READBACK_CORRECT) &&
                            (memcmp (&test_context->read_elements[readback_index],
                                    &test_context->write_elements[written_element_index], sizeof (memory_test_element_t)) == 0))
                    {
                        /* Memory element previously had the correct contents, but appears to have been overwritten
                         * with the contents of the address just written. This points at an addressing issue with the memory. */
                        test_context->readback_status[readback_index] = ELEMENT_READBACK_OVERWRITTEN;
                        test_context->content_success = false;
                        printf ("Write to address 0x%zx overwrote address 0x%zx\n",
                                test_context->write_elements[written_element_index].address,
                                test_context->write_elements[readback_index].address);
                        test_context->previous_readback[readback_index] = test_context->read_elements[readback_index];
                    }
                    else if ((test_context->readback_status[readback_index] != ELEMENT_READBACK_CORRECT) &&
                            (memcmp (&test_context->read_elements[readback_index], &test_context->previous_readback[readback_index],
                            sizeof (memory_test_element_t)) == 0))
                    {
                        /* Memory element has the same incorrect content as previously reported, so no need for another
                         * diagnostic error message. */
                    }
                    else
                    {
                        /* Memory element contents has changed for unidentified reason */
                        test_context->readback_status[readback_index] = ELEMENT_READBACK_INCORRECT;
                        test_context->content_success = false;
                        printf ("Following write to address 0x%zx, element at address 0x%zx changed incorrect contents from 0x%zx 0x%zx -> 0x%zx 0x%zx\n",
                                test_context->write_elements[written_element_index].address,
                                test_context->write_elements[readback_index].address,
                                test_context->previous_readback[readback_index].address,
                                test_context->previous_readback[readback_index].random,
                                test_context->read_elements[readback_index].address,
                                test_context->read_elements[readback_index].random);
                        test_context->previous_readback[readback_index] = test_context->read_elements[readback_index];
                    }
                }
            }
        }

        report_if_transfer_failed (&test_context->h2c_transfer);
        report_if_transfer_failed (&test_context->c2h_transfer);
    }

    free_vfio_dma_mapping (&test_context->write_data_mapping);
    free_vfio_dma_mapping (&test_context->read_data_mapping);
    free_vfio_dma_mapping (&test_context->descriptors_mapping);

    return test_context->transfer_success && test_context->content_success;
}


/**
 * @brief Process optional command line options which allow overriding the rage of DMA accessible memory tested.
 * @details
 *   Called before any tests on the design. The only modification prevented, via the command line option validation,
 *   is zeroing the dma_bridge_memory_size_bytes. Since zeroing that would indicate the design uses streams.
 * @param[in/out] design The design, which might have its DMA accessible memory definition tested
 */
static void allow_override_of_dma_accessible_memory_tested (fpga_design_t *const design)
{
    const size_t original_dma_bridge_memory_base_address = design->dma_bridge_memory_base_address;
    const size_t original_dma_bridge_memory_size_bytes = design->dma_bridge_memory_size_bytes;
    const size_t original_end_address = original_dma_bridge_memory_base_address + original_dma_bridge_memory_size_bytes - 1;

    if (arg_dma_bridge_memory_base_address_specified &&
            (arg_dma_bridge_memory_base_address != original_dma_bridge_memory_base_address))
    {
        design->dma_bridge_memory_base_address = arg_dma_bridge_memory_base_address;
        printf ("Overriding dma_bridge_memory_base_address 0x%zx -> 0x%zx\n",
                original_dma_bridge_memory_base_address, design->dma_bridge_memory_base_address);
    }

    if (arg_dma_bridge_memory_size_bytes_specified &&
            (arg_dma_bridge_memory_size_bytes != original_dma_bridge_memory_size_bytes))
    {
        design->dma_bridge_memory_size_bytes = arg_dma_bridge_memory_size_bytes;
        printf ("Overriding dma_bridge_memory_size_bytes 0x%zx -> 0x%zx\n",
                original_dma_bridge_memory_size_bytes, design->dma_bridge_memory_size_bytes);
    }

    const size_t new_end_address = design->dma_bridge_memory_base_address + design->dma_bridge_memory_size_bytes - 1;
    if ((design->dma_bridge_memory_base_address < original_dma_bridge_memory_base_address) ||
        (new_end_address > original_end_address))
    {
        printf ("Warning: Overridden DMA accessible memory is outside of that specified for the design.\n");
        printf ("         Tests are expected to fail.\n");
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    memory_addressing_test_context_t test_context;
    bool test_success;
    bool overall_success = true;

    parse_command_line_arguments (argc, argv);

    #ifdef __RDRND__
    printf ("Compiled with hardware random instruction support\n");
#endif

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Process any FPGA designs which have a DMA bridge with DMA accessible memory */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];

        if (design->dma_bridge_present && (design->dma_bridge_memory_size_bytes > 0))
        {
            allow_override_of_dma_accessible_memory_tested (design);
            memset (&test_context, 0, sizeof (test_context));
            initialise_memory_test_elements (design, &test_context);
            printf ("%s %s design", (test_context.num_write_elements > 0) ?  "Testing" : "Skipping",
                    fpga_design_names[design->design_id]);
            if ((design->design_id == FPGA_DESIGN_LITEFURY_PROJECT0) || (design->design_id == FPGA_DESIGN_NITEFURY_PROJECT0))
            {
                printf (" version 0x%x", design->board_version);
            }
            printf (" with memory base address 0x%zx size 0x%zx %u memory elements\n",
                    design->dma_bridge_memory_base_address, design->dma_bridge_memory_size_bytes,
                    test_context.num_write_elements);
            printf ("PCI device %s IOMMU group %s\n", design->vfio_device->device_name,
                    design->vfio_device->group->iommu_group_name);
            if (test_context.num_write_elements > 0)
            {
                printf ("Lowest memory element at address 0x%" PRIx64 ", highest at 0x%" PRIx64 "\n",
                        test_context.write_elements[0].address,
                        test_context.write_elements[test_context.num_write_elements - 1].address);
                test_success = test_memory_addressing (design, design->vfio_device, &test_context);
                overall_success = overall_success && test_success;
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    printf ("\nOverall %s\n", overall_success ? "PASS" : "FAIL");

    return overall_success ? EXIT_SUCCESS : EXIT_FAILURE;
}
