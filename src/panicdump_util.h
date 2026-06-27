#ifndef PANICDUMP_UTIL_H
#define PANICDUMP_UTIL_H

#include <stddef.h>
#include <stdint.h>

static inline void panicdump_zero_bytes(void *dst, size_t len)
{
    uint8_t *bytes = (uint8_t *)dst;
    for (size_t i = 0; i < len; i++) {
        bytes[i] = 0;
    }
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static inline void panicdump_copy_bytes(void *dst, const void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
}

#endif /* PANICDUMP_UTIL_H */
