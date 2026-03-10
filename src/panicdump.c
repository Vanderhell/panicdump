/**
 * panicdump.c — Core library implementation.
 *
 * Platform-specific fault capture is in panicdump_port_cortexm.c
 * and ports/cortexm/panicdump_fault_entry.S
 */

#include "panicdump.h"
#include "panicdump_crc32.h"
#include "panicdump_port.h"

#include <string.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Retained RAM dump slot
 *
 * Placed in .noinit section — NOT zeroed by startup code.
 * Survives a warm reset.
 * ---------------------------------------------------------------------- */

#if defined(__GNUC__)
__attribute__((section(".noinit")))
#endif
static panicdump_dump_t g_dump_slot;

/* User tag — also in .noinit so it survives reboot context checks */
#if defined(__GNUC__)
__attribute__((section(".noinit")))
#endif
static uint32_t g_user_tag;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static uint32_t compute_crc(const panicdump_dump_t *d)
{
    /* CRC32 over entire struct with crc32 field treated as 0 */
    panicdump_dump_t tmp;
    memcpy(&tmp, d, sizeof(tmp));
    tmp.crc32 = 0u;
    return panicdump_crc32((const uint8_t *)&tmp, sizeof(tmp));
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool panicdump_has_valid(void)
{
    if (g_dump_slot.magic != PANICDUMP_MAGIC) {
        return false;
    }
    if (g_dump_slot.version != PANICDUMP_VERSION) {
        return false;
    }
    if (g_dump_slot.total_size != sizeof(panicdump_dump_t)) {
        return false;
    }
    uint32_t expected = compute_crc(&g_dump_slot);
    return (g_dump_slot.crc32 == expected);
}

void panicdump_clear(void)
{
    memset(&g_dump_slot, 0, sizeof(g_dump_slot));
    /* magic = 0 is sufficient to invalidate, memset covers it */
}

const panicdump_dump_t *panicdump_get(void)
{
    return panicdump_has_valid() ? &g_dump_slot : NULL;
}

void panicdump_set_user_tag(uint32_t tag)
{
    g_user_tag = tag;
}

/* -------------------------------------------------------------------------
 * Export — hex-framed UART output
 *
 * Format:
 *   === PANICDUMP BEGIN ===\r\n
 *   <hex bytes, 32 per line>\r\n
 *   === PANICDUMP END ===\r\n
 * ---------------------------------------------------------------------- */

static void write_str(void (*wc)(char c), const char *s)
{
    while (*s) {
        wc(*s++);
    }
}

static void write_hex_byte(void (*wc)(char c), uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    wc(hex[(b >> 4) & 0x0F]);
    wc(hex[b & 0x0F]);
}

void panicdump_export_hex(void (*write_char)(char c))
{
    if (!write_char || !panicdump_has_valid()) {
        return;
    }

    write_str(write_char, "=== PANICDUMP BEGIN ===\r\n");

    const uint8_t *raw = (const uint8_t *)&g_dump_slot;
    const size_t   len = sizeof(g_dump_slot);

    for (size_t i = 0; i < len; i++) {
        write_hex_byte(write_char, raw[i]);
        if ((i % 32u) == 31u || i == len - 1u) {
            write_char('\r');
            write_char('\n');
        }
    }

    write_str(write_char, "=== PANICDUMP END ===\r\n");
}

void panicdump_boot_check_and_export(void (*write_char)(char c))
{
    if (!panicdump_has_valid()) {
        return;
    }
    panicdump_export_hex(write_char);
    panicdump_clear();
}

/* -------------------------------------------------------------------------
 * Internal: fill and commit a dump (called from fault capture layer)
 * ---------------------------------------------------------------------- */

void panicdump_commit(panicdump_fault_t fault_reason,
                      const panicdump_regs_t *regs,
                      const panicdump_stack_t *stack)
{
    /* Step 1: invalidate first — magic = 0 */
    g_dump_slot.magic = 0u;

    /* Step 2: fill all fields except magic and crc */
    g_dump_slot.version     = PANICDUMP_VERSION;
    g_dump_slot.header_size = (uint16_t)offsetof(panicdump_dump_t, regs);
    g_dump_slot.total_size  = (uint32_t)sizeof(panicdump_dump_t);
    g_dump_slot.flags       = 0u;
    g_dump_slot.arch_id     = PANICDUMP_PORT_ARCH_ID;
    g_dump_slot.fault_reason = (uint32_t)fault_reason;
    g_dump_slot.sequence    = 0u;
    g_dump_slot.user_tag    = g_user_tag;
    g_dump_slot.crc32       = 0u;

    if (regs) {
        memcpy(&g_dump_slot.regs, regs, sizeof(panicdump_regs_t));
    } else {
        memset(&g_dump_slot.regs, 0, sizeof(panicdump_regs_t));
    }

    if (stack) {
        memcpy(&g_dump_slot.stack, stack, sizeof(panicdump_stack_t));
    } else {
        memset(&g_dump_slot.stack, 0, sizeof(panicdump_stack_t));
    }

    /* Step 3: CRC (with crc32 = 0) */
    g_dump_slot.crc32 = compute_crc(&g_dump_slot);

    /* Step 4: commit — write magic LAST */
    g_dump_slot.magic = PANICDUMP_MAGIC;
}

/* -------------------------------------------------------------------------
 * Software panic trigger
 *
 * reason_tag is a short string (e.g. "assert", "watchdog").
 * It is encoded as a FNV-1a 32-bit hash and stored in user_tag,
 * overriding the last value set by panicdump_set_user_tag().
 *
 * The hash is deterministic — the same string always produces the same
 * 32-bit value, so it can be cross-referenced offline without symbol info.
 *
 * If reason_tag is NULL, the last user_tag value is preserved.
 * ---------------------------------------------------------------------- */

static uint32_t fnv1a_32(const char *s)
{
    uint32_t h = UINT32_C(0x811C9DC5);   /* FNV offset basis */
    while (*s) {
        h ^= (uint32_t)(unsigned char)*s++;
        h *= UINT32_C(0x01000193);        /* FNV prime */
    }
    return h;
}

void panicdump_trigger(const char *reason_tag)
{
    if (reason_tag != NULL) {
        g_user_tag = fnv1a_32(reason_tag);
    }

    panicdump_regs_t  regs;
    panicdump_stack_t stack;

    /* panicdump_port_capture_sw_regs fills actual register state via asm.
     * Do NOT memset regs to zero here — that would erase the context. */
    panicdump_port_capture_sw_regs(&regs, &stack);

    panicdump_commit(PANICDUMP_FAULT_SW_TRIGGER, &regs, &stack);

    panicdump_port_reset();  /* does not return */
}
