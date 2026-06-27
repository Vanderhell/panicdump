#ifndef PANICDUMP_CRC32_H
#define PANICDUMP_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standard CRC-32 (ISO 3309 / ITU-T V.42), reflected polynomial 0xEDB88320.
 * The caller owns the seed/final xor so the same helper works for streaming.
 */
static inline uint32_t panicdump_crc32_update(uint32_t crc,
                                              const uint8_t *data,
                                              size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ UINT32_C(0xEDB88320);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static inline uint32_t panicdump_crc32(const uint8_t *data, size_t len)
{
    return panicdump_crc32_update(UINT32_C(0xFFFFFFFF), data, len) ^
           UINT32_C(0xFFFFFFFF);
}

#ifdef __cplusplus
}
#endif

#endif /* PANICDUMP_CRC32_H */
