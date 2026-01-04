/*
 * @file vfio_bitops.h
 * @date 3 Jan 2026
 * @author Chester Gillon
 * @brief Provides functionality for bit-level operations
 */

#ifndef VFIO_BITOPS_H_
#define VFIO_BITOPS_H_

#include <stdint.h>
#include <strings.h>


/* Create a mask for one bit */
#define VFIO_BIT(bit_num) (1u << (bit_num))


/* Define a mask for a number of consecutive bits */
#define VFIO_BITS_PER_LONG 32u
#define VFIO_GENMASK_U32(high_bit_num,low_bit_num) \
    ((0xFFFFFFFF << (low_bit_num)) & (0xFFFFFFFF >> (VFIO_BITS_PER_LONG - 1u - (high_bit_num))))


/**
 * @brief Extract a field which spans multiple consecutive bits
 * @param[in] register_value The register containing the field
 * @param[in] field_mask The mask for the field to extract
 * @return The extracted field value, shifted to the least significant bits
 */
static inline uint32_t vfio_extract_field_u32 (const uint32_t register_value, const uint32_t field_mask)
{
    const int field_shift = ffs ((int) field_mask) - 1; /* ffs returns the least significant bit as zero */

    return (register_value & field_mask) >> field_shift;
}


/**
 * @brief Update a field which spans multiple consecutive bits
 * @param[in,out] register_value The register containing the field
 * @param[in] field_mask The mask for the field to update
 * @param[in] field_value The new field value
 */
static inline void vfio_update_field_u32 (uint32_t *const register_value, const uint32_t field_mask,
                                                    const uint32_t field_value)
{
    const int field_shift = ffs ((int) field_mask) - 1; /* ffs returns the least significant bit as zero */

    *register_value &= ~field_mask;
    *register_value |= (field_value << field_shift) & field_mask;
}


#endif /* VFIO_BITOPS_H_ */
