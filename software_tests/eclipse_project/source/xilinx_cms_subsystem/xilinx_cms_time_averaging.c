/*
 * @file xilinx_cms_time_averaging.c
 * @date 12 Nov 2025
 * @author Chester Gillon
 * @brief Investigate how the CMS firmware averages samples
 * @details
 *   When cms_read_sensors() was called immediately after cms_initialise_access() had released the CMS MicroBlaze firmware from
 *   reset the initial average values were half the instantaneous values.
 *
 *   To investigate the CMAS sampling and averaging works this utility polls the CMS_SENSOR_12V_PEX sensor and records the times
 *   the values change.
 *
 *   The CMS_SENSOR_12V_PEX sensor is used since:
 *   a. It is present on all supported cards.
 *   b. As the input PCIe voltage is not expected to fluctuate.
 *   c. Is the largest voltage measured, and therefore the largest unsigned integer values.
 */

#include "identify_pcie_fpga_design.h"
#include "xilinx_cms.h"

#include <stdlib.h>
#include <stdio.h>

#include <time.h>


/* Used to maintain a history of changes to values for one sensor, along with the time when sampled the change */
#define MAX_HISTORY_ITEMS 1000
typedef struct
{
    struct timespec time_of_change;
    uint32_t max;
    uint32_t average;
    uint32_t instantaneous;
} values_history_t;

static values_history_t history[MAX_HISTORY_ITEMS];
static uint32_t history_len;


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    /* Open the FPGA designs which have an IOMMU group assigned */
    identify_pcie_fpga_designs (&designs);

    /* Process all designs which have the CMS subsystem */
    for (uint32_t design_index = 0; design_index < designs.num_identified_designs; design_index++)
    {
        const fpga_design_t *const design = &designs.designs[design_index];

        if (design->cms_subsystem_present)
        {
            xilinx_cms_context_t context;

            printf ("\nDesign %s:\n", fpga_design_names[design->design_id]);
            printf ("  PCI device %s rev %02x IOMMU group %s\n", design->vfio_device->device_name, design->vfio_device->pci_revision_id,
                    design->vfio_device->group->iommu_group_name);
            if (cms_initialise_access (&context, design->vfio_device, design->cms_subsystem_bar_index, design->cms_subsystem_base_offset))
            {
                struct timespec now;
                struct timespec end_time = {0};
                bool run_time_elapsed = false;
                history_len = 0;

                /* Run until either the maximum history length is populated, or the maximum run length has elapsed */
                while ((history_len < MAX_HISTORY_ITEMS) && (!run_time_elapsed))
                {
                    clock_gettime (CLOCK_MONOTONIC, &now);
                    history[history_len].time_of_change = now;
                    history[history_len].max = read_reg32 (context.host_cms_shared_memory,
                            cms_sensor_definitions[CMS_SENSOR_12V_PEX].max_reg_offset);
                    history[history_len].average = read_reg32 (context.host_cms_shared_memory,
                            cms_sensor_definitions[CMS_SENSOR_12V_PEX].avg_reg_offset);
                    history[history_len].instantaneous = read_reg32 (context.host_cms_shared_memory,
                            cms_sensor_definitions[CMS_SENSOR_12V_PEX].ins_reg_offset);

                    if (history_len == 0)
                    {
                        /* Always store the first sample */
                        end_time = history[history_len].time_of_change;
                        end_time.tv_sec += 30; /* Run for a maximum of 30 seconds */
                        history_len++;
                    }
                    else
                    {
                        /* Store when the values change */
                        const values_history_t *const previous_item = &history[history_len - 1];
                        const values_history_t *const new_item = &history[history_len];

                        if ((new_item->max != previous_item->max) ||
                            (new_item->average != previous_item->average) ||
                            (new_item->instantaneous != previous_item->instantaneous))
                        {
                            history_len++;
                        }
                    }

                    if ((now.tv_sec > end_time.tv_sec) ||
                        ((now.tv_sec == end_time.tv_sec) && (now.tv_nsec > end_time.tv_nsec)))
                    {
                        run_time_elapsed = true;
                    }
                    else
                    {
                        /* Delay with a hold-off of 100 us before sampling for further changes.
                         * This is because sampling sensor values involves polling memory shared with the CMS firmware.
                         * Therefore, polling the shared memory in a tight loop could potentially block the CMS firmware. */
                        const struct timespec holdoff_delay =
                        {
                            .tv_sec = 0,
                            .tv_nsec = 100000
                        };

                        nanosleep (&holdoff_delay, NULL);
                    }
                }

                /* Display the sampled sensor values, along with the time and change in values */
                const int64_t ticks_per_ns = 1000000000;
                const values_history_t *previous_item = NULL;
                double time_change_secs = 0.0;
                int32_t max_change = 0;
                int32_t average_change = 0;
                int32_t instantaneous_change = 0;
                const int64_t first_sample_time_ns =
                        (history[0].time_of_change.tv_sec * ticks_per_ns) + history[0].time_of_change.tv_nsec;

                printf ("  Index            Time (secs)                 Max             Average       Instantaneous\n");
                for (uint32_t history_index = 0; history_index < history_len; history_index++)
                {
                    const values_history_t *const new_item = &history[history_index];
                    const int64_t new_time_ns =
                            (new_item->time_of_change.tv_sec * ticks_per_ns) + new_item->time_of_change.tv_nsec;
                    const double rel_time_secs = ((double) (new_time_ns - first_sample_time_ns)) / 1E9;

                    if (previous_item != NULL)
                    {
                        const int64_t previous_time_ns =
                                (previous_item->time_of_change.tv_sec * ticks_per_ns) + previous_item->time_of_change.tv_nsec;

                        time_change_secs = ((double) (new_time_ns - previous_time_ns)) / 1E9;
                        max_change = (int32_t) new_item->max - (int32_t) previous_item->max;
                        average_change = (int32_t) new_item->average - (int32_t) previous_item->average;
                        instantaneous_change = (int32_t) new_item->instantaneous - (int32_t) previous_item->instantaneous;
                    }

                    printf ("[%5u]  %9.6f(%+10.6f)  %8u(%+8d)  %8u(%+8d)  %8u(%+8d)\n",
                            history_index,
                            rel_time_secs, time_change_secs,
                            new_item->max, max_change,
                            new_item->average, average_change,
                            new_item->instantaneous, instantaneous_change);
                    previous_item = new_item;
                }
            }
        }
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
