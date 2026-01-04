/*
 * @file qdma_pf_registers.h
 * @date 2 Jan 2026
 * @author Chester Gillon
 * @brief Defines the QDMA Subsystem physical function registers
 * @details
 *   This was created to test a QDMA Subsystem using memory mapped transfers, with a soft QDMA.
 *
 *   The register definitions are based upon those in https://github.com/Xilinx/dma_ip_drivers.
 *
 *   The register space in PG302 and the linked qdma-v5-1-register-map.zip seem to be incomplete.
 *   E.g. for the QDNA_GLBL2_MISC_CAP (sic) register at address 0x134:
 *   a. In PG302 is only mentioned in the Revision Summary, and in the body of the document the register isn't defined.
 *   b. In qdma-v5-1-register-map.zip the bits aren't specified.
 */

#ifndef QDMA_PF_REGISTERS_H_
#define QDMA_PF_REGISTERS_H_

#include "vfio_bitops.h"


/* Identifies the IP */
#define QDMA_IDENTIFIER 0x1FD3


/* ------------------------- QDMA_TRQ_SEL_GLBL1 (0x0) -----------------*/
#define QDMA_OFFSET_CONFIG_BLOCK_ID                         0x0
#define     QDMA_CONFIG_BLOCK_ID_MASK                       VFIO_GENMASK_U32(31, 16)

#define QDMA_OFFSET_GLBL2_PF_BARLITE_INT        0x104
#define QDMA_GLBL2_PF3_BAR_MAP_MASK             VFIO_GENMASK_U32(23, 18)
#define QDMA_GLBL2_PF2_BAR_MAP_MASK             VFIO_GENMASK_U32(17, 12)
#define QDMA_GLBL2_PF1_BAR_MAP_MASK             VFIO_GENMASK_U32(11, 6)
#define QDMA_GLBL2_PF0_BAR_MAP_MASK             VFIO_GENMASK_U32(5, 0)

#define EQDMA_GLBL2_CHANNEL_MDMA_ADDR                      0x118
#define GLBL2_CHANNEL_MDMA_C2H_ST_MASK                     VFIO_BIT(17)
#define GLBL2_CHANNEL_MDMA_H2C_ST_MASK                     VFIO_BIT(16)
#define GLBL2_CHANNEL_MDMA_C2H_ENG_MASK                    VFIO_GENMASK_U32(11, 8)
#define GLBL2_CHANNEL_MDMA_H2C_ENG_MASK                    VFIO_GENMASK_U32(3, 0)

#define EQDMA_GLBL2_CHANNEL_CAP_ADDR                       0x120
#define GLBL2_CHANNEL_CAP_MULTIQ_MAX_MASK                  VFIO_GENMASK_U32(11, 0)

#define QDMA_OFFSET_GLBL2_MISC_CAP                          0x134
#define     QDMA_GLBL2_DEVICE_ID_MASK                       VFIO_GENMASK_U32(31, 28)
#define     QDMA_GLBL2_VIVADO_RELEASE_MASK                  VFIO_GENMASK_U32(27, 24)
#define     QDMA_GLBL2_VERSAL_IP_MASK                       VFIO_GENMASK_U32(23, 20)
#define     QDMA_GLBL2_RTL_VERSION_MASK                     VFIO_GENMASK_U32(19, 16)
#define     EQDMA_GLBL2_DBG_MODE_EN_MASK                    VFIO_BIT(4)
#define     EQDMA_GLBL2_DESC_ENG_MODE_MASK                  VFIO_GENMASK_U32(3, 2)
#define     EQDMA_GLBL2_FLR_PRESENT_MASK                    VFIO_BIT(1)
#define     EQDMA_GLBL2_MAILBOX_EN_MASK                     VFIO_BIT(0)

/* In QDMA_GLBL2_MISC_CAP(0x134) register,
 * Bits [23:20] gives QDMA IP version.
 * 0: QDMA3.1, 1: QDMA4.0, 2: QDMA5.0
 */
#define EQDMA_IP_VERSION_4                1
#define EQDMA_IP_VERSION_5                2

#endif /* QDMA_PF_REGISTERS_H_ */
