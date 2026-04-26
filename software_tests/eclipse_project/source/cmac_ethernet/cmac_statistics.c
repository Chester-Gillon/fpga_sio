/*
 * @file cmac_statistics.c
 * @date 25 Apr 2026
 * @author Chester Gillon
 * @brief Program to report the statistics counters for all ports in a CMAC
 */

#include "cmac_register_access.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <signal.h>
#include <time.h>
#include <sys/time.h>


/** Set from a signal handler to request that the statistics collection is stopped */
static volatile bool exit_requested;


/**
 * @brief Signal handler to request collection of statistics is stopped
 */
static void stop_statistics_collection_handler (const int sig)
{
    exit_requested = true;
}


/**
 * @brief Read and display the statistic counters for all CMAC ports
 * @param[in,out] designs The FPGA designs to process.
 * @param[in] suppress_display When true suppresses the display of the statistic counters.
 *                             This is for when starting regular sampling.
 */
static void read_and_display_cmac_statistics (fpga_designs_t *const designs, const bool suppress_display)
{
    uint32_t design_index;
    uint32_t port_num;
    cmac_port_statistics_t stats[MAX_VFIO_DEVICES][MAX_CMAC_PORTS_PER_DESIGN];

    /* Snapshot the counters for all CMAC ports */
    for (design_index = 0; design_index < designs->num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs->designs[design_index];

        for (port_num = 0; port_num < design->num_cmac_ports; port_num++)
        {
            cmac_snapshot_port_statistics (design, port_num, &stats[design_index][port_num]);
        }
    }

    /* Read and display the counters for all CMAC ports */
    for (design_index = 0; design_index < designs->num_identified_designs; design_index++)
    {
        fpga_design_t *const design = &designs->designs[design_index];

        for (port_num = 0; port_num < design->num_cmac_ports; port_num++)
        {
            cmac_read_port_statistics (&stats[design_index][port_num]);
            if (!suppress_display)
            {
                cmac_display_port_statistics (&stats[design_index][port_num]);
                printf ("\n");
            }
        }
    }
}


/**
 * @brief Display the statistics counters for all CMAC ports at regular intervals until requested to stop
 * @param[in,out] designs The FPGA designs to process.
 * @param[in] display_interval_secs The display interval for the statistics
 */
static void display_regular_cmac_statistics (fpga_designs_t *const designs, const int display_interval_secs)
{
    struct sigaction action;
    int rc;
    struct timespec next_sample_time;
    char time_str[80];
    struct tm broken_down_time;
    struct timeval tod;

    /* Install a signal handler to allow a request to stop of statistics collection */
    printf ("Press Ctrl-C to stop the CMAC port statistics collection\n");
    memset (&action, 0, sizeof (action));
    action.sa_handler = stop_statistics_collection_handler;
    action.sa_flags = SA_RESTART;
    rc = sigaction (SIGINT, &action, NULL);
    if (rc != 0)
    {
        printf ("sigaction() failed\n");
        exit (EXIT_FAILURE);
    }

    bool suppress_display = true;
    uint32_t num_collections = 0;
    clock_gettime (CLOCK_MONOTONIC, &next_sample_time);
    read_and_display_cmac_statistics (designs, suppress_display);
    do
    {
        /* Wait until the next collection interval */
        next_sample_time.tv_sec += display_interval_secs;
        clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &next_sample_time, NULL);
        suppress_display = false;

        /* Display time when these statistics are reported */
        gettimeofday (&tod, NULL);
        const time_t tod_sec = tod.tv_sec;
        const int64_t tod_msec = tod.tv_usec / 1000;
        localtime_r (&tod_sec, &broken_down_time);
        strftime (time_str, sizeof (time_str), "%H:%M:%S", &broken_down_time);
        size_t str_len = strlen (time_str);
        snprintf (&time_str[str_len], sizeof (time_str) - str_len, ".%03" PRIi64, tod_msec);

        num_collections++;
        printf ("\n\n%s collection number %u\n", time_str, num_collections);
        read_and_display_cmac_statistics (designs, suppress_display);
    } while (!exit_requested);
}


int main (int argc, char *argv[])
{
    fpga_designs_t designs;

    /* Process command line arguments */
    char junk;
    bool continuous_display = false;
    int display_interval_secs = 0;
    switch (argc)
    {
    case 1:
        continuous_display = false;
        break;

    case 2:
        if ((sscanf (argv[1], "%d%c", &display_interval_secs, &junk) != 1) || (display_interval_secs < 1))
        {
            printf ("Invalid <display_interval_secs> %s\n", argv[1]);
            return EXIT_FAILURE;
        }
        continuous_display = true;
        break;

    default:
        printf ("Usage: %s [<display_interval_secs>]\n", argv[0]);
        break;
    }

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

    if (continuous_display)
    {
        display_regular_cmac_statistics (&designs, display_interval_secs);
    }
    else
    {
        const bool suppress_display = false;
        read_and_display_cmac_statistics (&designs, suppress_display);
    }

    close_pcie_fpga_designs (&designs);

    return EXIT_SUCCESS;
}
