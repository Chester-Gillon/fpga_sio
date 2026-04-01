/*
 * @file mrmac_configuration.c
 * @date 1 Apr 2026
 * @author Chester Gillon
 * @brief Command line utility to allow MRMAC configuration registers to be modified.
 * @details
 *   This was written to allow a sub-set of configuration register fields to be modified, while investigating MRMAC behaviour.
 *   As per mrmac-registers-v3-0.xlsx the configuration registers modified by this program:
 *   a. Are configured at device programming.
 *   b. Are not affected by a AXI reset (which can be asserted via a PCIe Hot Reset when VFIO opens or closes the device).
 *
 *   To simplify this program:
 *   1. If one register has multiple configuration fields, there is no logic to perform a single register read-modify-write
 *      to update all the fields at once. This is on the assumption that a port reset can be asserted once all configuration
 *      fields have been modified, if required.
 *   2. Numeric field values are used, rather than adding enumerations.
 */

#include "mrmac_register_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <getopt.h>


/* Command line argument, which when sets causes the configuration values to be displayed */
static int arg_dump_config;

/* Command line argument, when when set causes the MRMAC port to be reset */
static int arg_reset_port;

/* Optional command line argument which allow to operate on only a specific MRMAC port */
static uint32_t arg_port_num;
static bool arg_port_num_specified;


/** The command line options for this program, in the format passed to getopt_long().
 *  Only long arguments are supported */
static const struct option command_line_options[] =
{
    {"device", required_argument, NULL, 0},
    {"port", required_argument, NULL, 0},
    {"dump", no_argument, &arg_dump_config, 1},
    {"reset", no_argument, &arg_reset_port, 1},
    {NULL, 0, NULL, 0}
};

/* Defines one configuration register field which this program can modify */
typedef struct
{
    /* The offset to the configuration register for the port */
    uint32_t offset;
    /* The mask for the configuration field within the register */
    uint32_t mask;
    /* The name for the configuration field used for the command line arguments and display */
    const char *name;
} mrmac_configuration_field_t;

/* Defines all the configuration fields which can be modified by this program */
static const mrmac_configuration_field_t mrmac_configuration_field_definitions[] =
{
    /* PG314 indicates ctl_rx_fec_transcode_clause49 and ctl_tx_fec_four_lane_pmd need to be set according to the ctl_fec_mode
     * for consistency. For testing allows the fields to be modified independently, rather than enforcing consistency. */
    {
        .offset = MRMAC_FEC_CONFIGURATION_REG1_OFFSET,
        .mask = MRMAC_CTL_FEC_MODE_MASK,
        .name = "ctl_fec_mode"
    },
    {
        .offset = MRMAC_FEC_CONFIGURATION_REG1_OFFSET,
        .mask = MRMAC_CTL_RX_FEC_TRANSCODE_CLAUSE49,
        .name = "ctl_rx_fec_transcode_clause49"
    },
    {
        .offset = MRMAC_FEC_CONFIGURATION_REG1_OFFSET,
        .mask = MRMAC_CTL_TX_FEC_FOUR_LANE_PMD,
        .name = "ctl_tx_fec_four_lane_pmd"
    }
};


/* Defines if a particular field is to be modified */
typedef struct
{
    bool modify;
    uint32_t value;
} field_modification_t;

/* Set from parsing command line arguments to give which fields, if any, to modify */
static field_modification_t field_modifications[VFIO_NELEMENTS (mrmac_configuration_field_definitions)];


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("  mrmac_configuration <options>\n");
    printf ("--device <domain>:<bus>:<dev>.<func>,<h2c_channel_id>\n");
    printf ("    Only operate on the specified device. By default operates on all\n");
    printf ("    designs with a MRMAC\n");
    printf ("--port <port_num>\n");
    printf ("    Only operate on the specified MRMAC port. By default operates on all\n");
    printf ("    ports\n");
    printf ("--dump Display all configuration fields\n");
    printf ("--reset Reset the MRMAC port at the end of the program\n");
    printf ("    A PCIe Hot Reset on VFIO open/close may perform a reset without this\n");
    printf ("field_name=<value>\n");
    printf ("  Modify a configuration field. The available fields are:\n");
    for (uint32_t field_index = 0; field_index < VFIO_NELEMENTS (mrmac_configuration_field_definitions); field_index++)
    {
        const mrmac_configuration_field_t *const config_field = &mrmac_configuration_field_definitions[field_index];
        uint32_t shifted_mask = vfio_extract_field_u32 (UINT32_MAX, config_field->mask);
        uint32_t num_bits = 0;

        while (shifted_mask != 0)
        {
            num_bits++;
            shifted_mask >>= 1;
        }
        printf ("   %s (%u bit%s)\n", config_field->name, num_bits, (num_bits > 1) ? "s" : "");
    }

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

    /* Process options */
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
            else if (strcmp (optdef->name, "port") == 0)
            {
                if ((sscanf (optarg, "%u%c", &arg_port_num, &junk) != 1) || (arg_port_num >= NUM_MRMAC_PORTS))
                {
                    printf ("Invalid %s %s\n", optdef->name, optarg);
                    exit (EXIT_FAILURE);
                }
                arg_port_num_specified = true;
            }
            else
            {
                /* This is a program error, and shouldn't be triggered by the command line options */
                fprintf (stderr, "Unexpected argument definition %s\n", optdef->name);
                exit (EXIT_FAILURE);
            }
        }
    } while (opt_status != -1);

    /* Process all non-options as fields to modify */
    for (int arg_index = optind; arg_index < argc; arg_index++)
    {
        char field_name[128] = {0};
        uint32_t requested_field_value;
        const int num_items = sscanf (argv[arg_index], "%127[^=]=%i%c", field_name, &requested_field_value, &junk);
        if (num_items == 2)
        {
            bool field_found = false;
            uint32_t field_index;

            /* Find the definition for the field, by searching for the name */
            field_index = 0;
            while (!field_found && (field_index < VFIO_NELEMENTS (mrmac_configuration_field_definitions)))
            {
                if (strcmp (field_name, mrmac_configuration_field_definitions[field_index].name) == 0)
                {
                    field_found = true;
                }
                else
                {
                    field_index++;
                }
            }

            if (field_found)
            {
                /* Store the requested value for the field. Validates the value can be stored in the field by setting it in a
                 * dummy register, and checking the extracted value matches. */
                uint32_t register_value = 0;

                vfio_update_field_u32 (&register_value, mrmac_configuration_field_definitions[field_index].mask, requested_field_value);
                const uint32_t extracted_field_value =
                        vfio_extract_field_u32 (register_value, mrmac_configuration_field_definitions[field_index].mask);
                if (field_modifications[field_index].modify)
                {
                    printf ("Multiple values specified for field %s\n", field_name);
                    exit (EXIT_FAILURE);
                }
                else if (extracted_field_value == requested_field_value)
                {
                    field_modifications[field_index].value = requested_field_value;
                    field_modifications[field_index].modify = true;
                }
                else
                {
                    printf ("Value %u is too large for field %s\n", requested_field_value, field_name);
                    exit (EXIT_FAILURE);
                }
            }
            else
            {
                printf ("Unknown field name %s\n", field_name);
                exit (EXIT_FAILURE);
            }
        }
        else
        {
            printf ("Invalid field_name=<value> : %s\n", argv[arg_index]);
            exit (EXIT_FAILURE);
        }
    }
}


/**
 * @brief Dump the MRMAC configuration register fields known by this program
 * @param[in,out] designs The FPGA designs to process.
 */
static void dump_mrmac_config (fpga_designs_t *const designs)
{
    mrmac_port_iterator_t iterator;
    fpga_design_t *design;
    uint32_t port_num;

    mrmac_port_iterator_initialise (&iterator, designs, arg_port_num, arg_port_num_specified);
    design = mrmac_port_iterator_next (&iterator, &port_num);
    while (design != NULL)
    {
        printf ("Design %s device %s port %u:\n", fpga_design_names[design->design_id], design->vfio_device->device_name, port_num);

        for (uint32_t field_index = 0; field_index < VFIO_NELEMENTS (mrmac_configuration_field_definitions); field_index++)
        {
            const mrmac_configuration_field_t *const config_field = &mrmac_configuration_field_definitions[field_index];
            const uint8_t *const port_regs = &design->mrmac.regs[port_num * MRMAC_PORT_REGS_FRAME_SIZE];
            const uint32_t register_value = read_reg32 (port_regs, config_field->offset);
            const uint32_t field_value = vfio_extract_field_u32 (register_value, config_field->mask);

            printf ("  %s=%u\n", config_field->name, field_value);
        }
        design = mrmac_port_iterator_next (&iterator, &port_num);
    }
}


/**
 * @brief Issue a reset on the MRMAC ports
 * @param[in,out] designs The FPGA designs to process.
 */
static void reset_mrmac_ports (fpga_designs_t *const designs)
{
    mrmac_port_iterator_t iterator;
    fpga_design_t *design;
    uint32_t port_num;

    mrmac_port_iterator_initialise (&iterator, designs, arg_port_num, arg_port_num_specified);
    design = mrmac_port_iterator_next (&iterator, &port_num);
    while (design != NULL)
    {
        mrmac_reset_port (design, port_num);
        printf ("Reset design %s device %s port %u\n", fpga_design_names[design->design_id], design->vfio_device->device_name, port_num);
        design = mrmac_port_iterator_next (&iterator, &port_num);
    }
}


/**
 * @brief Modify the MRMAC port configuration values specified on the command line.
 * @param[in,out] designs The FPGA designs to process.
 */
static void modify_mrmac_configuration (fpga_designs_t *const designs)
{
    mrmac_port_iterator_t iterator;
    fpga_design_t *design;
    uint32_t port_num;
    uint32_t register_value;
    uint32_t original_field_value;

    mrmac_port_iterator_initialise (&iterator, designs, arg_port_num, arg_port_num_specified);
    design = mrmac_port_iterator_next (&iterator, &port_num);
    while (design != NULL)
    {
        uint8_t *const port_regs = &design->mrmac.regs[port_num * MRMAC_PORT_REGS_FRAME_SIZE];

        for (uint32_t field_index = 0; field_index < VFIO_NELEMENTS (mrmac_configuration_field_definitions); field_index++)
        {
            const mrmac_configuration_field_t *const config_field = &mrmac_configuration_field_definitions[field_index];
            const field_modification_t *const field_modification = &field_modifications[field_index];

            if (field_modification->modify)
            {
                register_value = read_reg32 (port_regs, config_field->offset);
                original_field_value = vfio_extract_field_u32 (register_value, config_field->mask);
                if (original_field_value != field_modification->value)
                {
                    vfio_update_field_u32 (&register_value, config_field->mask, field_modification->value);
                    write_reg32 (port_regs, config_field->offset, register_value);
                    printf ("Design %s device %s port %u field %s changed from %u -> %u\n",
                            fpga_design_names[design->design_id], design->vfio_device->device_name, port_num,
                            config_field->name, original_field_value, field_modification->value);
                }
                else
                {
                    printf ("Design %s device %s port %u field %s value unchanged at value %u\n",
                            fpga_design_names[design->design_id], design->vfio_device->device_name, port_num,
                            config_field->name, field_modification->value);
                }
            }
        }

        design = mrmac_port_iterator_next (&iterator, &port_num);
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    modify_mrmac_configuration (&designs);
    if (arg_reset_port)
    {
        reset_mrmac_ports (&designs);
    }
    if (arg_dump_config)
    {
        dump_mrmac_config (&designs);
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
