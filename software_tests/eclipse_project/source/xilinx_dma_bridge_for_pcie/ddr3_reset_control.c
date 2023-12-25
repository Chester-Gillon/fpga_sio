/*
 * @file ddr3_reset_control.c
 * @date 25 Dec 2023
 * @author Chester Gillon
 * @brief Utility to allow issuing to the MIG controller for a DDR3 interface in a design
 * @details
 *  Written as part of investigating https://gist.github.com/Chester-Gillon/2654caf1f6997aad34d91409c6527f2b
 *  about why performing a verification of the FPGA over JTAG causes Xilinx "DMA/Bridge Subsystem for PCI Express"
 *  DMA to timeout.
 */

#include "identify_pcie_fpga_design.h"
#include "transfer_timing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>


/* GPIO output masks */
#define CLOCKING_WIZARD_RESET_MASK (1u << 0u) /* Active high reset which initiates the DDR3 reset */

/* GPIO input masks */
#define CLOCKING_WIZARD_LOCKED_MASK (1u << 1u) /* Active high locked output from clocking wizard which generates the MIG clocks */
#define MMCM_LOCKED_MASK            (1u << 2u) /* Active high mmcm_locked output from MIG */
#define INIT_CAL_COMPLETE_MASK      (1u << 3u) /* Active high init_cal_complete output from MIG */
#define UI_CLK_SYNC_RST_MASK        (1u << 4u) /* Active high ui_clk_sync_rst output from MIG */


/* Used to record a history of when the reset related signals change state, recording before during and after
 * the assertion of reset. */
typedef struct
{
    int64_t time;
    uint32_t reg_value;
} reset_signals_sample_t;
#define MAX_RESET_SIGNAL_SAMPLES 8192
static reset_signals_sample_t reset_signals_history[MAX_RESET_SIGNAL_SAMPLES];
static uint32_t reset_signals_history_len;


/* Optional command line argument to specify the duration of the reset applied */
static int64_t arg_reset_duration_nsecs;
static bool arg_reset_duration_specified;


/**
 * @brief Parse the command line arguments
 * @param[in] argc, argv Arguments passed to main
 */
static void parse_command_line_arguments (int argc, char *argv[])
{
    const char *const optstring = "d:r:?";
    int option;
    char junk;
    double reset_duration_secs;

    option = getopt (argc, argv, optstring);
    while (option != -1)
    {
        switch (option)
        {
        case 'd':
            vfio_add_pci_device_location_filter (optarg);
            break;

        case 'r':
            if (sscanf (optarg, "%lf%c", &reset_duration_secs, &junk) != 1)
            {
                printf ("Invalid reset_duration_floating_point_secs %s\n", optarg);
                exit (EXIT_FAILURE);
            }
            arg_reset_duration_nsecs = (int64_t) (reset_duration_secs * 1E9);
            arg_reset_duration_specified = true;
            break;

        case '?':
        default:
            printf ("Usage %s [-d <pci_device_location>] [-o <reset_duration_floating_point_secs>]\n", argv[0]);
            exit (EXIT_FAILURE);
            break;
        }
        option = getopt (argc, argv, optstring);
    }
}


/**
 * @brief Display the current state of the reset control input signals
 * @parma[in] gpio_reset_control_regs Base of the GPIO registers
 */
static void display_reset_signals (const uint8_t *const gpio_reset_control_regs)
{
    const uint32_t reg_value = read_reg32 (gpio_reset_control_regs, 0);

    printf ("locked           : %u\n", ((reg_value & CLOCKING_WIZARD_LOCKED_MASK) != 0) ? 1 : 0);
    printf ("mmcm_locked      : %u\n", ((reg_value & MMCM_LOCKED_MASK) != 0) ? 1 : 0);
    printf ("init_cal_complete: %u\n", ((reg_value & INIT_CAL_COMPLETE_MASK) != 0) ? 1 : 0);
    printf ("ui_clk_sync_rst  : %u\n", ((reg_value & UI_CLK_SYNC_RST_MASK) != 0) ? 1 : 0);
}


/**
 * @brief Called to sample the reset signal state, and append to the history upon change
 * @parma[in] gpio_reset_control_regs Base of the GPIO registers
 * @param[in] reset_control_mask The current state of the reset GPIO output controlled by this program
 */
static void record_reset_signal_changes (const uint8_t *const gpio_reset_control_regs, const uint32_t reset_control_mask)
{
    const int64_t now = get_monotonic_time ();

    /* Read the GPIO input bits */
    uint32_t reg_value = read_reg32 (gpio_reset_control_regs, 0);

    /* Store the GPIO output bit (since the AXI GPIO doesn't allow readback of output bits) */
    reg_value &= ~CLOCKING_WIZARD_RESET_MASK;
    reg_value |= reset_control_mask;

    if ((reset_signals_history_len == 0                                                   ) ||
         ((reset_signals_history_len < MAX_RESET_SIGNAL_SAMPLES                       ) &&
          (reg_value != reset_signals_history[reset_signals_history_len - 1].reg_value)   ))
    {
        /* Append the change to the history */
        reset_signals_history[reset_signals_history_len].time = now;
        reset_signals_history[reset_signals_history_len].reg_value = reg_value;
        reset_signals_history_len++;
    }
}


/**
 * @brief Assert a reset of the DDR3 MIG controller
 * @details Also monitors the state of signal which should be affected by the reset, and displays the history
 *          of when the signals change. This is to investigate how quickly the MIG clocks lock following
 *          de-assertion of the reset.
 * @param[in/out] gpio_reset_control_regs Base of the GPIO registers used to control the reset.
 */
static void reset_ddr3 (uint8_t *const gpio_reset_control_regs)
{
    uint32_t reset_control_mask;
    int64_t now;

    const int64_t start_time = get_monotonic_time ();
    const int64_t deassert_reset_time = start_time + arg_reset_duration_nsecs;

    /* The GPIO value which indicates the reset is complete, with the clocks locked and the DDR3 init calibration complete */
    const uint32_t reset_complete_value = CLOCKING_WIZARD_LOCKED_MASK | MMCM_LOCKED_MASK | INIT_CAL_COMPLETE_MASK;

    /* Use a 10 second timeout for the initialisation failing to complete */
    const int64_t initialisation_timeout = deassert_reset_time + (10000000000L);

    /* Save initial state before have asserted reset.
     * Assumed reset is not already asserted when this function called. */
    reset_signals_history_len = 0;
    reset_control_mask = 0;
    record_reset_signal_changes (gpio_reset_control_regs, reset_control_mask);

    /* Assert the reset */
    reset_control_mask = CLOCKING_WIZARD_RESET_MASK;
    write_reg32 (gpio_reset_control_regs, 0, reset_control_mask);
    record_reset_signal_changes (gpio_reset_control_regs, reset_control_mask);

    /* Delay for the reset duration */
    do
    {
        now = get_monotonic_time ();
        record_reset_signal_changes (gpio_reset_control_regs, reset_control_mask);
    } while (now < deassert_reset_time);

    /* De-assert the reset */
    reset_control_mask = 0;
    write_reg32 (gpio_reset_control_regs, 0, reset_control_mask);
    record_reset_signal_changes (gpio_reset_control_regs, reset_control_mask);

    /* Wait until the reset has completed, or timed out */
    bool timedout = false;
    bool reset_complete = false;
    do
    {
        now = get_monotonic_time ();
        record_reset_signal_changes (gpio_reset_control_regs, reset_control_mask);
        timedout = now >= initialisation_timeout;
        reset_complete = reset_signals_history[reset_signals_history_len - 1].reg_value == reset_complete_value;
    } while (!timedout && !reset_complete);

    if (timedout)
    {
        printf ("Reset didn't complete within timeout - DDR3 may not be usable\n");
    }

    /* Display the history of changes to reset signals */
    const uint32_t num_signals = 5;
    uint32_t signal_bitnum;
    const char *signal_names[] =
    {
        "clocking_wizard_reset",
        "clocking_wizard_locked",
        "mmcm_locked",
        "init_cal_complete",
        "ui_clk_sync_rst_mask"
    };

    printf (" Time (secs)");
    for (signal_bitnum = 0; signal_bitnum < num_signals; signal_bitnum++)
    {
        printf (" %s", signal_names[signal_bitnum]);
    }
    printf ("\n");

    for (uint32_t history_index = 0; history_index < reset_signals_history_len; history_index++)
    {
        const reset_signals_sample_t *const sample = &reset_signals_history[history_index];
        const double elapsed_time_secs = (double) (sample->time - reset_signals_history[0].time) / 1E9;

        printf ("%12.9lf", elapsed_time_secs);
        for (signal_bitnum = 0; signal_bitnum < num_signals; signal_bitnum++)
        {
            printf (" %*d", (int) strlen (signal_names [signal_bitnum]), (sample->reg_value & (1u << signal_bitnum)) != 0 ? 1 : 0);
        }
        printf ("\n");
    }
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;
    uint8_t *gpio_reset_control_regs;

    parse_command_line_arguments (argc, argv);

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Process any FPGA designs which have the GPIO used as a DDR3 reset control */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs.designs[design_index];
        vfio_device_t *const vfio_device = &designs.vfio_devices.devices[design_index];

        switch (design->design_id)
        {
        case FPGA_DESIGN_TOSING_160T_DMA_DDR3:
            {
                const uint32_t peripherals_bar_index = 0;
                const size_t gpio_reset_control_base_offset = 0x3000;
                const size_t gpio_reset_control_frame_size  = 0x1000;

                gpio_reset_control_regs =
                        map_vfio_registers_block (vfio_device, peripherals_bar_index,
                                gpio_reset_control_base_offset, gpio_reset_control_frame_size);
            }
            break;

        default:
            gpio_reset_control_regs = NULL;
            break;
        }

        if (gpio_reset_control_regs != NULL)
        {
            /* Attempt a reset of the DDR3 if requested, otherwise just display the current state of the reset signals */
            if (arg_reset_duration_specified)
            {
                printf ("Applying DDR3 reset...\n");
                reset_ddr3 (gpio_reset_control_regs);
            }
            else
            {
                printf ("Current reset signal state:\n");
                display_reset_signals (gpio_reset_control_regs);
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
