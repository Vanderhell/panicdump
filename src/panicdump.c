#include "panicdump.h"

#include "panicdump_crc32.h"
#include "panicdump_port.h"
#include "panicdump_wire.h"

#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((section(".noinit"), aligned(4)))
#endif
static uint8_t g_dump_wire[PANICDUMP_WIRE_TOTAL_SIZE];

#if defined(__GNUC__) || defined(__clang__)
__attribute__((section(".noinit"), aligned(4)))
#endif
static uint32_t g_user_tag;

static panicdump_dump_t g_cached_dump;

static uint32_t panicdump_wire_crc32_with_zero_crc(const uint8_t *wire)
{
    uint32_t crc = panicdump_wire_crc32_magic_prefix();
    crc = panicdump_crc32_update(crc, wire + 4u, PANICDUMP_WIRE_CRC32_OFFSET - 4u);
    {
        const uint8_t zero_crc[4] = { 0u, 0u, 0u, 0u };
        crc = panicdump_crc32_update(crc, zero_crc, sizeof(zero_crc));
    }
    crc = panicdump_crc32_update(crc,
                                 wire + PANICDUMP_WIRE_CRC32_OFFSET + 4u,
                                 PANICDUMP_WIRE_TOTAL_SIZE -
                                 (PANICDUMP_WIRE_CRC32_OFFSET + 4u));
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static void panicdump_write_u32_field(uint8_t *wire, size_t offset, uint32_t value)
{
    panicdump_wire_store_u32(wire + offset, value);
}

static void panicdump_write_u16_field(uint8_t *wire, size_t offset, uint16_t value)
{
    panicdump_wire_store_u16(wire + offset, value);
}

static void panicdump_write_regs(uint8_t *wire, const panicdump_regs_t *regs)
{
    const uint32_t values[] = {
        regs->r0, regs->r1, regs->r2, regs->r3, regs->r12, regs->lr, regs->pc,
        regs->xpsr, regs->msp, regs->psp, regs->control, regs->primask,
        regs->basepri, regs->faultmask, regs->cfsr, regs->hfsr, regs->dfsr,
        regs->mmfar, regs->bfar, regs->afsr, regs->shcsr,
    };

    size_t offset = PANICDUMP_WIRE_REGS_OFFSET;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++, offset += 4u) {
        panicdump_write_u32_field(wire, offset, values[i]);
    }
}

static void panicdump_write_stack(uint8_t *wire, const panicdump_stack_t *stack)
{
    size_t offset = PANICDUMP_WIRE_STACK_OFFSET;
    panicdump_write_u32_field(wire, offset, stack->captured_sp);
    offset += 4u;
    panicdump_write_u32_field(wire, offset, stack->stack_bytes);
    offset += 4u;
    if (stack->stack_bytes != 0u) {
        memcpy(wire + offset, stack->data, PANICDUMP_WIRE_STACK_BYTES);
    } else {
        memset(wire + offset, 0, PANICDUMP_WIRE_STACK_BYTES);
    }
}

static void panicdump_read_regs(panicdump_regs_t *regs, const uint8_t *wire)
{
    const uint8_t *p = wire + PANICDUMP_WIRE_REGS_OFFSET;
    regs->r0       = panicdump_wire_load_u32(p + 0u);
    regs->r1       = panicdump_wire_load_u32(p + 4u);
    regs->r2       = panicdump_wire_load_u32(p + 8u);
    regs->r3       = panicdump_wire_load_u32(p + 12u);
    regs->r12      = panicdump_wire_load_u32(p + 16u);
    regs->lr       = panicdump_wire_load_u32(p + 20u);
    regs->pc       = panicdump_wire_load_u32(p + 24u);
    regs->xpsr     = panicdump_wire_load_u32(p + 28u);
    regs->msp      = panicdump_wire_load_u32(p + 32u);
    regs->psp      = panicdump_wire_load_u32(p + 36u);
    regs->control  = panicdump_wire_load_u32(p + 40u);
    regs->primask  = panicdump_wire_load_u32(p + 44u);
    regs->basepri  = panicdump_wire_load_u32(p + 48u);
    regs->faultmask = panicdump_wire_load_u32(p + 52u);
    regs->cfsr     = panicdump_wire_load_u32(p + 56u);
    regs->hfsr     = panicdump_wire_load_u32(p + 60u);
    regs->dfsr     = panicdump_wire_load_u32(p + 64u);
    regs->mmfar    = panicdump_wire_load_u32(p + 68u);
    regs->bfar     = panicdump_wire_load_u32(p + 72u);
    regs->afsr     = panicdump_wire_load_u32(p + 76u);
    regs->shcsr    = panicdump_wire_load_u32(p + 80u);
}

static void panicdump_read_stack(panicdump_stack_t *stack, const uint8_t *wire)
{
    const uint8_t *p = wire + PANICDUMP_WIRE_STACK_OFFSET;
    stack->captured_sp = panicdump_wire_load_u32(p + 0u);
    stack->stack_bytes = panicdump_wire_load_u32(p + 4u);
    if (stack->stack_bytes > PANICDUMP_WIRE_STACK_BYTES) {
        stack->stack_bytes = 0u;
    }
    memcpy(stack->data, p + 8u, PANICDUMP_WIRE_STACK_BYTES);
}

static bool panicdump_dump_fields_valid(const panicdump_dump_t *dump)
{
    if (!dump) {
        return false;
    }

    if (dump->magic != PANICDUMP_MAGIC) {
        return false;
    }
    if (dump->version != PANICDUMP_VERSION) {
        return false;
    }
    if (dump->header_size != PANICDUMP_WIRE_HEADER_SIZE) {
        return false;
    }
    if (dump->total_size != PANICDUMP_WIRE_TOTAL_SIZE) {
        return false;
    }
    if (dump->flags & ~(PANICDUMP_FLAG_INVALID_FRAME |
                        PANICDUMP_FLAG_USE_PSP |
                        PANICDUMP_FLAG_STACK_VALID |
                        PANICDUMP_FLAG_FRAME_VALID)) {
        return false;
    }
    if (dump->arch_id != PANICDUMP_ARCH_CORTEXM3 &&
        dump->arch_id != PANICDUMP_ARCH_CORTEXM4) {
        return false;
    }
    if (dump->fault_reason > PANICDUMP_FAULT_SW_TRIGGER) {
        return false;
    }
    if (dump->stack.stack_bytes > PANICDUMP_WIRE_STACK_BYTES) {
        return false;
    }
    if (dump->stack.stack_bytes != 0u &&
        dump->stack.stack_bytes != PANICDUMP_WIRE_STACK_BYTES) {
        return false;
    }
    return true;
}

static void panicdump_encode_header(uint8_t *wire, const panicdump_dump_t *dump)
{
    panicdump_write_u16_field(wire, PANICDUMP_WIRE_VERSION_OFFSET, dump->version);
    panicdump_write_u16_field(wire, PANICDUMP_WIRE_HEADER_SIZE_OFFSET, dump->header_size);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_TOTAL_SIZE_OFFSET, dump->total_size);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_FLAGS_OFFSET, dump->flags);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_ARCH_ID_OFFSET, dump->arch_id);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_FAULT_REASON_OFFSET, dump->fault_reason);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_SEQUENCE_OFFSET, dump->sequence);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_USER_TAG_OFFSET, dump->user_tag);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_CRC32_OFFSET, dump->crc32);
}

static void panicdump_encode_dump_fields(uint8_t *wire, const panicdump_dump_t *dump)
{
    memset(wire, 0, PANICDUMP_WIRE_TOTAL_SIZE);
    panicdump_write_u32_field(wire, PANICDUMP_WIRE_MAGIC_OFFSET, dump->magic);
    panicdump_encode_header(wire, dump);
    panicdump_write_regs(wire, &dump->regs);
    panicdump_write_stack(wire, &dump->stack);
}

static void panicdump_decode_dump_fields(panicdump_dump_t *dump, const uint8_t *wire)
{
    memset(dump, 0, sizeof(*dump));
    dump->magic = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_MAGIC_OFFSET);
    dump->version = panicdump_wire_load_u16(wire + PANICDUMP_WIRE_VERSION_OFFSET);
    dump->header_size = panicdump_wire_load_u16(wire + PANICDUMP_WIRE_HEADER_SIZE_OFFSET);
    dump->total_size = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_TOTAL_SIZE_OFFSET);
    dump->flags = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_FLAGS_OFFSET);
    dump->arch_id = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_ARCH_ID_OFFSET);
    dump->fault_reason = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_FAULT_REASON_OFFSET);
    dump->sequence = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_SEQUENCE_OFFSET);
    dump->user_tag = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_USER_TAG_OFFSET);
    dump->crc32 = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_CRC32_OFFSET);
    panicdump_read_regs(&dump->regs, wire);
    panicdump_read_stack(&dump->stack, wire);
}

static void panicdump_write_magic(uint8_t *wire, uint32_t magic)
{
    *(volatile uint32_t *)(void *)(wire + PANICDUMP_WIRE_MAGIC_OFFSET) = magic;
}

static void panicdump_write_commit_marker(uint8_t *wire)
{
    panicdump_port_publish_barrier();
    panicdump_write_magic(wire, PANICDUMP_MAGIC);
}

bool panicdump_validate_wire(const uint8_t *wire, size_t wire_len)
{
    if (!wire || wire_len != PANICDUMP_WIRE_TOTAL_SIZE) {
        return false;
    }

    if (panicdump_wire_load_u32(wire + PANICDUMP_WIRE_MAGIC_OFFSET) != PANICDUMP_MAGIC) {
        return false;
    }
    if (panicdump_wire_load_u16(wire + PANICDUMP_WIRE_VERSION_OFFSET) != PANICDUMP_VERSION) {
        return false;
    }
    if (panicdump_wire_load_u16(wire + PANICDUMP_WIRE_HEADER_SIZE_OFFSET) !=
        PANICDUMP_WIRE_HEADER_SIZE) {
        return false;
    }
    if (panicdump_wire_load_u32(wire + PANICDUMP_WIRE_TOTAL_SIZE_OFFSET) !=
        PANICDUMP_WIRE_TOTAL_SIZE) {
        return false;
    }

    uint32_t flags = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_FLAGS_OFFSET);
    if (flags & ~(PANICDUMP_FLAG_INVALID_FRAME |
                  PANICDUMP_FLAG_USE_PSP |
                  PANICDUMP_FLAG_STACK_VALID |
                  PANICDUMP_FLAG_FRAME_VALID)) {
        return false;
    }

    uint32_t arch_id = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_ARCH_ID_OFFSET);
    if (arch_id != PANICDUMP_ARCH_CORTEXM3 && arch_id != PANICDUMP_ARCH_CORTEXM4) {
        return false;
    }

    uint32_t fault_reason = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_FAULT_REASON_OFFSET);
    if (fault_reason > PANICDUMP_FAULT_SW_TRIGGER) {
        return false;
    }

    uint32_t stack_bytes = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_STACK_OFFSET + 4u);
    if (stack_bytes > PANICDUMP_WIRE_STACK_BYTES) {
        return false;
    }
    if (stack_bytes != 0u && stack_bytes != PANICDUMP_WIRE_STACK_BYTES) {
        return false;
    }

    uint32_t stored_crc = panicdump_wire_load_u32(wire + PANICDUMP_WIRE_CRC32_OFFSET);
    uint32_t expected_crc = panicdump_wire_crc32_with_zero_crc(wire);
    return stored_crc == expected_crc;
}

bool panicdump_validate_dump(const panicdump_dump_t *dump)
{
    if (!panicdump_dump_fields_valid(dump)) {
        return false;
    }

    uint8_t wire[PANICDUMP_WIRE_TOTAL_SIZE];
    return panicdump_encode_dump(wire, sizeof(wire), dump);
}

bool panicdump_encode_dump(uint8_t *out_wire,
                           size_t out_wire_len,
                           const panicdump_dump_t *dump)
{
    if (!out_wire || !panicdump_dump_fields_valid(dump) ||
        out_wire_len != PANICDUMP_WIRE_TOTAL_SIZE) {
        return false;
    }

    panicdump_encode_dump_fields(out_wire, dump);
    panicdump_write_u32_field(out_wire, PANICDUMP_WIRE_CRC32_OFFSET,
                              panicdump_wire_crc32_with_zero_crc(out_wire));
    return panicdump_validate_wire(out_wire, out_wire_len);
}

bool panicdump_decode_dump(panicdump_dump_t *out_dump,
                           const uint8_t *wire,
                           size_t wire_len)
{
    if (!out_dump || !panicdump_validate_wire(wire, wire_len)) {
        return false;
    }

    panicdump_decode_dump_fields(out_dump, wire);
    return true;
}

bool panicdump_has_valid(void)
{
    return panicdump_validate_wire(g_dump_wire, sizeof(g_dump_wire));
}

void panicdump_clear(void)
{
    memset(g_dump_wire, 0, sizeof(g_dump_wire));
}

const panicdump_dump_t *panicdump_get(void)
{
    if (!panicdump_has_valid()) {
        return NULL;
    }

    panicdump_decode_dump_fields(&g_cached_dump, g_dump_wire);
    return &g_cached_dump;
}

void panicdump_set_user_tag(uint32_t tag)
{
    g_user_tag = tag;
}

uint32_t panicdump_get_user_tag(void)
{
    return g_user_tag;
}

static bool panicdump_write_char_checked(panicdump_write_char_fn write_char, char c)
{
    return write_char && write_char(c);
}

static bool panicdump_write_str(panicdump_write_char_fn write_char, const char *s)
{
    while (*s) {
        if (!panicdump_write_char_checked(write_char, *s++)) {
            return false;
        }
    }
    return true;
}

static bool panicdump_write_hex_byte(panicdump_write_char_fn write_char, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    return panicdump_write_char_checked(write_char, hex[(value >> 4) & 0x0Fu]) &&
           panicdump_write_char_checked(write_char, hex[value & 0x0Fu]);
}

panicdump_export_status_t panicdump_export_hex(panicdump_write_char_fn write_char)
{
    if (!write_char) {
        return PANICDUMP_EXPORT_NULL_CALLBACK;
    }
    if (!panicdump_has_valid()) {
        return PANICDUMP_EXPORT_NO_DUMP;
    }

    if (!panicdump_write_str(write_char, "=== PANICDUMP BEGIN ===\r\n")) {
        return PANICDUMP_EXPORT_SHORT_WRITE;
    }

    for (size_t i = 0; i < sizeof(g_dump_wire); i++) {
        if (!panicdump_write_hex_byte(write_char, g_dump_wire[i])) {
            return PANICDUMP_EXPORT_SHORT_WRITE;
        }
        if ((i % 32u) == 31u || i + 1u == sizeof(g_dump_wire)) {
            if (!panicdump_write_char_checked(write_char, '\r') ||
                !panicdump_write_char_checked(write_char, '\n')) {
                return PANICDUMP_EXPORT_SHORT_WRITE;
            }
        }
    }

    if (!panicdump_write_str(write_char, "=== PANICDUMP END ===\r\n")) {
        return PANICDUMP_EXPORT_SHORT_WRITE;
    }

    return PANICDUMP_EXPORT_OK;
}

panicdump_export_status_t panicdump_boot_check_and_export(panicdump_write_char_fn write_char)
{
    panicdump_export_status_t status = panicdump_export_hex(write_char);
    if (status == PANICDUMP_EXPORT_OK) {
        panicdump_clear();
    }
    return status;
}

uint32_t panicdump_reason_tag_hash_n(const char *reason_tag, size_t max_len)
{
    uint32_t hash = UINT32_C(0x811C9DC5);
    if (!reason_tag) {
        return hash;
    }

    for (size_t i = 0; i < max_len && reason_tag[i] != '\0'; i++) {
        hash ^= (uint32_t)(unsigned char)reason_tag[i];
        hash *= UINT32_C(0x01000193);
    }
    return hash;
}

void panicdump_trigger_reason(uint32_t reason_tag)
{
    panicdump_regs_t regs;
    panicdump_stack_t stack;

    g_user_tag = reason_tag;
    panicdump_port_capture_sw_regs(&regs, &stack);
    panicdump_commit_snapshot(&regs, &stack, PANICDUMP_FAULT_SW_TRIGGER, 0u,
                              UINT32_C(0));
    panicdump_port_reset();
}

void panicdump_trigger_tag(const char *reason_tag, size_t max_len)
{
    panicdump_trigger_reason(reason_tag ?
                             panicdump_reason_tag_hash_n(reason_tag, max_len) :
                             g_user_tag);
}

void panicdump_commit_snapshot(const panicdump_regs_t *regs,
                               const panicdump_stack_t *stack,
                               uint32_t fault_reason,
                               uint32_t flags,
                               uint32_t exc_return)
{
    panicdump_write_magic(g_dump_wire, 0u);

    panicdump_write_u16_field(g_dump_wire, PANICDUMP_WIRE_VERSION_OFFSET,
                              PANICDUMP_VERSION);
    panicdump_write_u16_field(g_dump_wire, PANICDUMP_WIRE_HEADER_SIZE_OFFSET,
                              PANICDUMP_WIRE_HEADER_SIZE);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_TOTAL_SIZE_OFFSET,
                              PANICDUMP_WIRE_TOTAL_SIZE);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_FLAGS_OFFSET, flags);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_ARCH_ID_OFFSET,
                              PANICDUMP_PORT_ARCH_ID);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_FAULT_REASON_OFFSET,
                              fault_reason);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_SEQUENCE_OFFSET,
                              exc_return);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_USER_TAG_OFFSET,
                              g_user_tag);
    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_CRC32_OFFSET, 0u);

    if (regs) {
        panicdump_write_regs(g_dump_wire, regs);
    } else {
        memset(g_dump_wire + PANICDUMP_WIRE_REGS_OFFSET, 0, PANICDUMP_WIRE_REGS_SIZE);
    }

    if (stack) {
        panicdump_write_stack(g_dump_wire, stack);
    } else {
        memset(g_dump_wire + PANICDUMP_WIRE_STACK_OFFSET, 0, PANICDUMP_WIRE_STACK_SIZE);
    }

    panicdump_write_u32_field(g_dump_wire, PANICDUMP_WIRE_CRC32_OFFSET,
                              panicdump_wire_crc32_with_zero_crc(g_dump_wire));
    panicdump_write_commit_marker(g_dump_wire);
}
