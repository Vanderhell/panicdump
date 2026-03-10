#ifndef PANICDUMP_H
#define PANICDUMP_H

/**
 * panicdump — Tiny crash dump library for bare-metal Cortex-M MCUs.
 *
 * v1 scope:
 *   - Cortex-M3/M4 only
 *   - bare-metal only
 *   - retained RAM (.noinit) backend
 *   - UART export (hex-framed)
 *   - offline Python decoder
 *
 * NOT supported in v1:
 *   - RTOS, flash backend, stack unwinding, symbol resolution
 *
 * See docs/FORMAT.md and docs/LIMITATIONS.md for details.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Version & Magic
 * ---------------------------------------------------------------------- */

#define PANICDUMP_MAGIC         UINT32_C(0x50444331)  /* 'PDC1' */
#define PANICDUMP_VERSION       UINT16_C(1)
#define PANICDUMP_ARCH_CORTEXM3 UINT32_C(0x0003)
#define PANICDUMP_ARCH_CORTEXM4 UINT32_C(0x0004)

/* -------------------------------------------------------------------------
 * Fault reasons
 * ---------------------------------------------------------------------- */

typedef enum {
    PANICDUMP_FAULT_UNKNOWN     = 0,
    PANICDUMP_FAULT_HARD        = 1,
    PANICDUMP_FAULT_MEM         = 2,
    PANICDUMP_FAULT_BUS         = 3,
    PANICDUMP_FAULT_USAGE       = 4,
    PANICDUMP_FAULT_SW_TRIGGER  = 5,   /* explicit software panic */
} panicdump_fault_t;

/* -------------------------------------------------------------------------
 * Dump structs (packed — binary format, do NOT change field order in v1)
 * ---------------------------------------------------------------------- */

#pragma pack(push, 1)

typedef struct {
    /* Stacked by hardware on exception entry (from exception frame) */
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;         /* link register at fault */
    uint32_t pc;         /* program counter at fault */
    uint32_t xpsr;

    /* Read from special registers in fault handler */
    uint32_t msp;
    uint32_t psp;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;

    /* Fault status registers (SCB) */
    uint32_t cfsr;       /* Configurable Fault Status */
    uint32_t hfsr;       /* HardFault Status */
    uint32_t dfsr;       /* Debug Fault Status */
    uint32_t mmfar;      /* MemManage Fault Address */
    uint32_t bfar;       /* BusFault Address */
    uint32_t afsr;       /* Auxiliary Fault Status */
    uint32_t shcsr;      /* System Handler Control and State */
} panicdump_regs_t;

#define PANICDUMP_STACK_SLICE_BYTES  64u

typedef struct {
    uint32_t captured_sp;
    uint32_t stack_bytes;                       /* always PANICDUMP_STACK_SLICE_BYTES */
    uint8_t  data[PANICDUMP_STACK_SLICE_BYTES];
} panicdump_stack_t;

typedef struct {
    /* === header === */
    uint32_t magic;          /* written LAST — marks dump as complete */
    uint16_t version;
    uint16_t header_size;    /* sizeof(panicdump_header_t) */
    uint32_t total_size;     /* sizeof entire panicdump_dump_t) */
    uint32_t flags;          /* reserved, set to 0 */
    uint32_t arch_id;
    uint32_t fault_reason;
    uint32_t sequence;       /* monotonic counter, 0 in v1 */
    uint32_t user_tag;       /* last value set by panicdump_set_user_tag() */
    uint32_t crc32;          /* CRC32 over entire struct with this field = 0 */
    /* === payload === */
    panicdump_regs_t  regs;
    panicdump_stack_t stack;
} panicdump_dump_t;

#pragma pack(pop)

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * Check whether a valid (complete, CRC-ok) dump is present in retained RAM.
 */
bool panicdump_has_valid(void);

/**
 * Invalidate / erase current dump from retained RAM.
 */
void panicdump_clear(void);

/**
 * Get a read-only view of the current dump.
 * Returns NULL if no valid dump is present.
 *
 * NOTE: The returned pointer is const by convention, not by hardware
 * enforcement. Do not cast away const and modify the dump — this will
 * corrupt the stored data and invalidate the CRC.
 */
const panicdump_dump_t *panicdump_get(void);

/**
 * Export current dump as hex-framed text.
 * Caller provides a single-char write callback (e.g. UART putchar).
 * Does nothing if no valid dump is present.
 */
void panicdump_export_hex(void (*write_char)(char c));

/**
 * Set a user-defined tag that will be included in the next dump.
 * Call this at key application milestones (e.g. task IDs, state machine states).
 */
void panicdump_set_user_tag(uint32_t tag);

/**
 * Convenience: check for dump at boot, export it, then clear it.
 * write_char — UART output callback
 */
void panicdump_boot_check_and_export(void (*write_char)(char c));

/**
 * Software-triggered panic dump (useful for assert / watchdog handlers).
 * Does NOT return — resets the MCU after saving the dump.
 *
 * reason_tag: short ASCII string identifying the panic site (e.g. "assert",
 *             "watchdog", "stack_overflow"). It is encoded as a FNV-1a 32-bit
 *             hash and stored in user_tag, overriding any previous tag value.
 *             The same string always produces the same hash — cross-reference
 *             offline without symbol info.
 *             Pass NULL to preserve the last panicdump_set_user_tag() value.
 *
 * Example:
 *   panicdump_trigger("assert");     // user_tag = fnv1a("assert") = 0x5D9E9B9B
 *   panicdump_trigger("watchdog");   // user_tag = fnv1a("watchdog")
 *   panicdump_trigger(NULL);         // user_tag unchanged
 */
void panicdump_trigger(const char *reason_tag);

#ifdef __cplusplus
}
#endif

#endif /* PANICDUMP_H */
