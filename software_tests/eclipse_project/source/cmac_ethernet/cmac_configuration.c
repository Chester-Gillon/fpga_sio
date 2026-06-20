/*
 * @file cmac_configuration.c
 * @date 19 Jun 2026
 * @author Chester Gillon
 * @brief Command line utility to allow CMAC configuration registers to be modified.
 * @details
 *   This was written to allow a sub-set of configuration register fields to be modified, while investigating CMAC behaviour.
 *   The configuration registers modified by this program are found to be reset when the CMAC s_axi_sreset is asserted,
 *   which which can be asserted via a PCIe Hot Reset when VFIO opens or closes the device.
 *
 *   To simplify this program numeric field values are used, rather than adding enumerations.
 */

#include "cmac_register_access.h"
#include "cmac_axi4_lite_registers.h"
#include "vfio_bitops.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <getopt.h>


/* Command line argument, which when sets causes the configuration values to be displayed */
static int arg_dump_config;

/* Command line argument, when when set causes the CMAC port to be reset */
static int arg_reset_port;

/* Optional command line argument which allow to operate on only a specific CMAC port */
static uint32_t arg_port_num;
static bool arg_port_num_specified;


/* Defines one configuration register field which this program can modify */
typedef struct
{
    /* Which CMAC the configuration register is for. CMAC_FEATURE_ARRAY_SIZE means applicable to any feature. */
    cmac_port_configured_features_t feature;
    /* The offset to the configuration register for the port */
    uint32_t offset;
    /* The mask for the configuration field within the register */
    uint32_t mask;
    /* The name for the configuration field used for the command line arguments and display */
    const char *name;
} cmac_configuration_field_t;

/* Defines all the configuration fields which can be modified by this program */
static const cmac_configuration_field_t cmac_configuration_field_definitions[] =
{
    /* Transmit controls */
    {
        .feature = CMAC_FEATURE_PACKET_TX,
        .offset = CONFIGURATION_TX_REG1_OFFSET,
        .mask = CONFIGURATION_TX_REG1_CTL_TX_ENABLE_MASK,
        .name = "tx_enable"
    },
    {
        .feature = CMAC_FEATURE_PACKET_TX,
        .offset = CONFIGURATION_TX_REG1_OFFSET,
        .mask = CONFIGURATION_TX_REG1_CTL_TX_SEND_LFI_MASK,
        .name = "tx_send_lfi"
    },
    {
        .feature = CMAC_FEATURE_PACKET_TX,
        .offset = CONFIGURATION_TX_REG1_OFFSET,
        .mask = CONFIGURATION_TX_REG1_CTL_TX_SEND_RFI_MASK,
        .name = "tx_send_rfi"
    },
    {
        .feature = CMAC_FEATURE_PACKET_TX,
        .offset = CONFIGURATION_TX_REG1_OFFSET,
        .mask = CONFIGURATION_TX_REG1_CTL_TX_SEND_IDLE_MASK,
        .name = "tx_send_idle"
    },
    {
        .feature = CMAC_FEATURE_PACKET_TX,
        .offset = CONFIGURATION_TX_REG1_OFFSET,
        .mask = CONFIGURATION_TX_REG1_CTL_TX_TEST_PATTERN_MASK,
        .name = "tx_test_pattern"
    },

    /* Receive controls */
    {
        .feature = CMAC_FEATURE_PACKET_RX,
        .offset = CONFIGURATION_RX_REG1_OFFSET,
        .mask = CONFIGURATION_RX_REG1_CTL_RX_ENABLE_MASK,
        .name = "rx_enable"
    },
    {
        .feature = CMAC_FEATURE_PACKET_RX,
        .offset = CONFIGURATION_RX_REG1_OFFSET,
        .mask = CONFIGURATION_RX_REG1_CTL_RX_FORCE_RESYNC_MASK,
        .name = "rx_force_resync"
    },
    {
        .feature = CMAC_FEATURE_PACKET_RX,
        .offset = CONFIGURATION_RX_REG1_OFFSET,
        .mask = CONFIGURATION_RX_REG1_CTL_RX_TEST_PATTERN_MASK,
        .name = "rx_test_pattern"
    },

    /* RSFEC controls */
    {
        .feature = CMAC_FEATURE_RS_FEC,
        .offset = RSFEC_CONFIG_ENABLE_OFFSET,
        .mask = RSFEC_CONFIG_ENABLE_CTL_RX_RSFEC_ENABLE_MASK,
        .name = "rx_rsfec_enable"
    },
    {
        .feature = CMAC_FEATURE_RS_FEC,
        .offset = RSFEC_CONFIG_ENABLE_OFFSET,
        .mask = RSFEC_CONFIG_ENABLE_CTL_TX_RSFEC_ENABLE_MASK,
        .name = "tx_rsfec_enable"
    },

    /* GT controls, applicable to any feature */
    {
        .feature = CMAC_FEATURE_ARRAY_SIZE,
        .offset = GT_LOOPBACK_REG_OFFSET,
        .mask = GT_LOOPBACK_REG_CTL_GT_LOOPBACK_MASK,
        .name = "gt_loopback"
    }
};


/* Defines if a particular field is to be modified */
typedef struct
{
    bool modify;
    uint32_t value;
} field_modification_t;

/* Set from parsing command line arguments to give which fields, if any, to modify */
static field_modification_t field_modifications[VFIO_NELEMENTS (cmac_configuration_field_definitions)];


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


/**
 * @brief Display the usage for this program, and the exit
 */
static void display_usage (void)
{
    printf ("Usage:\n");
    printf ("  cmac_configuration <options>\n");
    printf ("--device <domain>:<bus>:<dev>.<func>,<h2c_channel_id>\n");
    printf ("    Only operate on the specified device. By default operates on all\n");
    printf ("    designs with a CMAC\n");
    printf ("--port <port_num>\n");
    printf ("    Only operate on the specified CMAC port. By default operates on all\n");
    printf ("    ports\n");
    printf ("--dump Display all configuration fields\n");
    printf ("--reset Reset the CMAC port at the end of the program\n");
    printf ("    A PCIe Hot Reset on VFIO open/close may perform a reset without this\n");
    printf ("field_name=<value>\n");
    printf ("  Modify a configuration field. The available fields are:\n");
    for (uint32_t field_index = 0; field_index < VFIO_NELEMENTS (cmac_configuration_field_definitions); field_index++)
    {
        const cmac_configuration_field_t *const config_field = &cmac_configuration_field_definitions[field_index];
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
                if ((sscanf (optarg, "%u%c", &arg_port_num, &junk) != 1) || (arg_port_num >= MAX_CMAC_PORTS_PER_DESIGN))
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
            while (!field_found && (field_index < VFIO_NELEMENTS (cmac_configuration_field_definitions)))
            {
                if (strcmp (field_name, cmac_configuration_field_definitions[field_index].name) == 0)
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

                vfio_update_field_u32 (&register_value, cmac_configuration_field_definitions[field_index].mask, requested_field_value);
                const uint32_t extracted_field_value =
                        vfio_extract_field_u32 (register_value, cmac_configuration_field_definitions[field_index].mask);
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
 * @brief Dump the CMAC configuration register fields known by this program
 * @param[in,out] designs The FPGA designs to process.
 */
static void dump_cmac_config (fpga_designs_t *const designs)
{
    cmac_port_iterator_t iterator;
    fpga_design_t *design;
    uint32_t port_num;

    cmac_port_iterator_initialise (&iterator, designs, arg_port_num, arg_port_num_specified);
    design = cmac_port_iterator_next (&iterator, &port_num);
    while (design != NULL)
    {
        printf ("Design %s device %s port %u:\n", fpga_design_names[design->design_id], design->vfio_device->device_name, port_num);

        for (uint32_t field_index = 0; field_index < VFIO_NELEMENTS (cmac_configuration_field_definitions); field_index++)
        {
            const cmac_configuration_field_t *const config_field = &cmac_configuration_field_definitions[field_index];
            const bool field_present = (config_field->feature == CMAC_FEATURE_ARRAY_SIZE) ||
                    (design->cmac_ports[port_num].configured_features[config_field->feature]);

            if (field_present)
            {
                const uint8_t *const port_regs = design->cmac_ports[port_num].cmac_regs;
                const uint32_t register_value = read_reg32 (port_regs, config_field->offset);
                const uint32_t field_value = vfio_extract_field_u32 (register_value, config_field->mask);

                printf ("  %s=%u\n", config_field->name, field_value);
            }
        }
        design = cmac_port_iterator_next (&iterator, &port_num);
    }
}


/**
 * @brief Issue a reset on the CMAC ports
 * @param[in,out] designs The FPGA designs to process.
 */
static void reset_cmac_ports (fpga_designs_t *const designs)
{
    cmac_port_iterator_t iterator;
    fpga_design_t *design;
    uint32_t port_num;

    cmac_port_iterator_initialise (&iterator, designs, arg_port_num, arg_port_num_specified);
    design = cmac_port_iterator_next (&iterator, &port_num);
    while (design != NULL)
    {
        cmac_reset_port (design, port_num);
        printf ("Reset design %s device %s port %u\n", fpga_design_names[design->design_id], design->vfio_device->device_name, port_num);
        design = cmac_port_iterator_next (&iterator, &port_num);
    }
}


/**
 * @brief Modify the CMAC port configuration values specified on the command line.
 * @param[in,out] designs The FPGA designs to process.
 */
static void modify_cmac_configuration (fpga_designs_t *const designs)
{
    cmac_port_iterator_t iterator;
    fpga_design_t *design;
    uint32_t port_num;
    uint32_t register_value;
    uint32_t original_field_value;

    cmac_port_iterator_initialise (&iterator, designs, arg_port_num, arg_port_num_specified);
    design = cmac_port_iterator_next (&iterator, &port_num);
    while (design != NULL)
    {
        uint8_t *const port_regs = design->cmac_ports[port_num].cmac_regs;

        for (uint32_t field_index = 0; field_index < VFIO_NELEMENTS (cmac_configuration_field_definitions); field_index++)
        {
            const cmac_configuration_field_t *const config_field = &cmac_configuration_field_definitions[field_index];
            const field_modification_t *const field_modification = &field_modifications[field_index];

            if (field_modification->modify)
            {
                const bool field_present = (config_field->feature == CMAC_FEATURE_ARRAY_SIZE) ||
                        (design->cmac_ports[port_num].configured_features[config_field->feature]);

                if (field_present)
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
                else
                {
                    printf ("Unable to modify %s as required CMAC features not present\n", config_field->name);
                }
            }
        }

        design = cmac_port_iterator_next (&iterator, &port_num);
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);
    uint32_t num_cmac_designs = 0;
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        if (designs.designs[design_index].num_cmac_ports > 0)
        {
            num_cmac_designs++;
        }
    }
    if (num_cmac_designs == 0)
    {
        printf ("No CMAC designs to operate on\n");
        exit (EXIT_FAILURE);
    }

    modify_cmac_configuration (&designs);
    if (arg_reset_port)
    {
        reset_cmac_ports (&designs);
    }
    if (arg_dump_config)
    {
        dump_cmac_config (&designs);
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
