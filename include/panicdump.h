#ifndef PANICDUMP_H
#define PANICDUMP_H

/*
 * panicdump - tiny crash dump library for bare-metal Cortex-M MCUs.
 *
 * Scope:
 *   - Cortex-M3/M4 non-FPU target support
 *   - retained RAM dump slot
 *   - offline host decoding
 *   - no RTOS, stack unwinding, flash backend, or symbol resolution
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PANICDUMP_NORETURN __attribute__((noreturn))
#define PANICDUMP_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define PANICDUMP_NORETURN
#define PANICDUMP_NONNULL(...)
#endif

#define PANICDUMP_MAGIC                 UINT32_C(0x50444331)
#define PANICDUMP_VERSION               UINT16_C(1)
#define PANICDUMP_ARCH_CORTEXM3         UINT32_C(0x0003)
#define PANICDUMP_ARCH_CORTEXM4         UINT32_C(0x0004)
#define PANICDUMP_ARCH_HOST             UINT32_C(0xFFFF)

#define PANICDUMP_REASON_TAG_MAX_LEN    32u

#define PANICDUMP_FLAG_INVALID_FRAME    UINT32_C(1u << 0)
#define PANICDUMP_FLAG_USE_PSP          UINT32_C(1u << 1)
#define PANICDUMP_FLAG_STACK_VALID      UINT32_C(1u << 2)
#define PANICDUMP_FLAG_FRAME_VALID      UINT32_C(1u << 3)

enum {
    PANICDUMP_WIRE_MAGIC_OFFSET = 0u,
    PANICDUMP_WIRE_VERSION_OFFSET = 4u,
    PANICDUMP_WIRE_HEADER_SIZE_OFFSET = 6u,
    PANICDUMP_WIRE_TOTAL_SIZE_OFFSET = 8u,
    PANICDUMP_WIRE_FLAGS_OFFSET = 12u,
    PANICDUMP_WIRE_ARCH_ID_OFFSET = 16u,
    PANICDUMP_WIRE_FAULT_REASON_OFFSET = 20u,
    PANICDUMP_WIRE_SEQUENCE_OFFSET = 24u,
    PANICDUMP_WIRE_USER_TAG_OFFSET = 28u,
    PANICDUMP_WIRE_CRC32_OFFSET = 32u,
    PANICDUMP_WIRE_HEADER_SIZE = 36u,
    PANICDUMP_WIRE_REGS_OFFSET = 36u,
    PANICDUMP_WIRE_REGS_SIZE = 84u,
    PANICDUMP_WIRE_STACK_OFFSET = 120u,
    PANICDUMP_WIRE_STACK_SIZE = 72u,
    PANICDUMP_WIRE_STACK_BYTES = 64u,
    PANICDUMP_WIRE_TOTAL_SIZE = 192u,
};

typedef enum {
    PANICDUMP_FAULT_UNKNOWN = 0,
    PANICDUMP_FAULT_HARD = 1,
    PANICDUMP_FAULT_MEM = 2,
    PANICDUMP_FAULT_BUS = 3,
    PANICDUMP_FAULT_USAGE = 4,
    PANICDUMP_FAULT_SW_TRIGGER = 5,
} panicdump_fault_t;

typedef enum {
    PANICDUMP_EXPORT_OK = 0,
    PANICDUMP_EXPORT_NO_DUMP = 1,
    PANICDUMP_EXPORT_SHORT_WRITE = 2,
    PANICDUMP_EXPORT_NULL_CALLBACK = 3,
} panicdump_export_status_t;

typedef bool (*panicdump_write_char_fn)(char c);

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t msp;
    uint32_t psp;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t afsr;
    uint32_t shcsr;
} panicdump_regs_t;

typedef struct {
    uint32_t captured_sp;
    uint32_t stack_bytes;
    uint8_t data[PANICDUMP_WIRE_STACK_BYTES];
} panicdump_stack_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t flags;
    uint32_t arch_id;
    uint32_t fault_reason;
    uint32_t sequence;
    uint32_t user_tag;
    uint32_t crc32;
    panicdump_regs_t regs;
    panicdump_stack_t stack;
} panicdump_dump_t;

bool panicdump_has_valid(void);
void panicdump_clear(void);
const panicdump_dump_t *panicdump_get(void);

panicdump_export_status_t panicdump_export_hex(panicdump_write_char_fn write_char);
panicdump_export_status_t panicdump_boot_check_and_export(panicdump_write_char_fn write_char);

void panicdump_set_user_tag(uint32_t tag);
uint32_t panicdump_get_user_tag(void);

uint32_t panicdump_reason_tag_hash_n(const char *reason_tag, size_t max_len);
void panicdump_trigger_reason(uint32_t reason_tag) PANICDUMP_NORETURN;
void panicdump_trigger_tag(const char *reason_tag, size_t max_len) PANICDUMP_NORETURN;

static inline void panicdump_trigger(const char *reason_tag)
{
    panicdump_trigger_tag(reason_tag, PANICDUMP_REASON_TAG_MAX_LEN);
}

bool panicdump_encode_dump(uint8_t *out_wire,
                           size_t out_wire_len,
                           const panicdump_dump_t *dump);
bool panicdump_decode_dump(panicdump_dump_t *out_dump,
                           const uint8_t *wire,
                           size_t wire_len);
bool panicdump_validate_dump(const panicdump_dump_t *dump);
bool panicdump_validate_wire(const uint8_t *wire, size_t wire_len);

#ifdef __cplusplus
}
#endif

#endif /* PANICDUMP_H */
