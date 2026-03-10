#ifndef PANICDUMP_CRC32_H
#define PANICDUMP_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Standard CRC-32 (ISO 3309 / ITU-T V.42).
 * Polynomial: 0xEDB88320 (reflected).
 * No dynamic table — computed on the fly to save RAM.
 */
static inline uint32_t panicdump_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ UINT32_C(0xEDB88320);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

#ifdef __cplusplus
}
#endif

#endif /* PANICDUMP_CRC32_H */
