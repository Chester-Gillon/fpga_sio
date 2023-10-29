/*
 * @file display_possible_fpga_designs.c
 * @date 29 Oct 2023
 * @author Chester Gillon
 * @brief An executable which simply calls display_possible_fpga_designs()
 */

#include "identify_pcie_fpga_design.h"

#include <stdlib.h>


int main (int argc, char *argv[])
{
    display_possible_fpga_designs ();

    return EXIT_SUCCESS;
}
