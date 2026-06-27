#ifndef PANICDUMP_WIRE_H
#define PANICDUMP_WIRE_H

#include "panicdump.h"
#include "panicdump_crc32.h"

static inline void panicdump_wire_store_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static inline void panicdump_wire_store_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static inline uint16_t panicdump_wire_load_u16(const uint8_t *src)
{
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

static inline uint32_t panicdump_wire_load_u32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static inline uint32_t panicdump_wire_crc32_magic_prefix(void)
{
    const uint8_t magic_bytes[4] = {
        (uint8_t)(PANICDUMP_MAGIC & 0xFFu),
        (uint8_t)((PANICDUMP_MAGIC >> 8) & 0xFFu),
        (uint8_t)((PANICDUMP_MAGIC >> 16) & 0xFFu),
        (uint8_t)((PANICDUMP_MAGIC >> 24) & 0xFFu),
    };
    return panicdump_crc32_update(UINT32_C(0xFFFFFFFF), magic_bytes,
                                  sizeof(magic_bytes));
}

#endif /* PANICDUMP_WIRE_H */
