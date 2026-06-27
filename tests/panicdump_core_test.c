#include "panicdump.h"
#include "panicdump_port.h"

#include "panicdump_util.h"

static void fill_sample_regs(panicdump_regs_t *regs)
{
    panicdump_zero_bytes(regs, sizeof(*regs));
    regs->r0 = 0x11111111u;
    regs->r1 = 0x22222222u;
    regs->r2 = 0x33333333u;
    regs->r3 = 0x44444444u;
    regs->r12 = 0x12121212u;
    regs->lr = 0xFFFFFFF9u;
    regs->pc = 0x08001234u;
    regs->xpsr = 0x21000000u;
    regs->msp = 0x20007FF0u;
    regs->psp = 0x20006000u;
    regs->control = 0x1u;
    regs->primask = 0u;
    regs->basepri = 0u;
    regs->faultmask = 0u;
    regs->cfsr = 0x00008200u;
    regs->hfsr = 0x40000000u;
    regs->dfsr = 0u;
    regs->mmfar = 0u;
    regs->bfar = 0x20010004u;
    regs->afsr = 0u;
    regs->shcsr = 0u;
}

static void fill_sample_stack(panicdump_stack_t *stack)
{
    panicdump_zero_bytes(stack, sizeof(*stack));
    stack->captured_sp = 0x20007FF0u;
    stack->stack_bytes = PANICDUMP_WIRE_STACK_BYTES;
    for (size_t i = 0; i < PANICDUMP_WIRE_STACK_BYTES; i++) {
        stack->data[i] = (uint8_t)i;
    }
}

static int commit_and_validate(uint32_t tag)
{
    panicdump_regs_t regs;
    panicdump_stack_t stack;
    panicdump_dump_t decoded;
    uint8_t wire[PANICDUMP_WIRE_TOTAL_SIZE];
    const panicdump_dump_t *dump;

    fill_sample_regs(&regs);
    fill_sample_stack(&stack);
    panicdump_set_user_tag(tag);
    panicdump_commit_snapshot(&regs, &stack, PANICDUMP_FAULT_HARD,
                              PANICDUMP_FLAG_FRAME_VALID | PANICDUMP_FLAG_STACK_VALID,
                              0xFFFFFFF9u);

    if (!panicdump_has_valid()) {
        return 1;
    }

    dump = panicdump_get();
    if (!dump) {
        return 1;
    }

    if (dump->user_tag != tag) {
        return 1;
    }
    if (dump->sequence != 0xFFFFFFF9u) {
        return 1;
    }

    if (!panicdump_encode_dump(wire, sizeof(wire), dump)) {
        return 1;
    }
    if (!panicdump_validate_wire(wire, sizeof(wire))) {
        return 1;
    }

    if (!panicdump_decode_dump(&decoded, wire, sizeof(wire))) {
        return 1;
    }

    if (decoded.user_tag != tag || decoded.fault_reason != PANICDUMP_FAULT_HARD) {
        return 1;
    }

    return 0;
}

int main(void)
{
    uint8_t wire[PANICDUMP_WIRE_TOTAL_SIZE];
    panicdump_regs_t regs;
    panicdump_stack_t stack;
    const panicdump_dump_t *dump;

    if (commit_and_validate(0xABCD1234u) != 0) {
        return 1;
    }

    dump = panicdump_get();
    if (!dump) {
        return 1;
    }

    if (!panicdump_encode_dump(wire, sizeof(wire), dump)) {
        return 1;
    }

    wire[0] ^= 0x01u;
    if (panicdump_validate_wire(wire, sizeof(wire))) {
        return 1;
    }
    wire[0] ^= 0x01u;

    wire[PANICDUMP_WIRE_STACK_OFFSET + 8u] ^= 0x01u;
    if (panicdump_validate_wire(wire, sizeof(wire))) {
        return 1;
    }
    wire[PANICDUMP_WIRE_STACK_OFFSET + 8u] ^= 0x01u;

    wire[PANICDUMP_WIRE_CRC32_OFFSET] = 0u;
    wire[PANICDUMP_WIRE_CRC32_OFFSET + 1u] = 0u;
    wire[PANICDUMP_WIRE_CRC32_OFFSET + 2u] = 0u;
    wire[PANICDUMP_WIRE_CRC32_OFFSET + 3u] = 0u;
    if (panicdump_validate_wire(wire, sizeof(wire))) {
        return 1;
    }

    if (!panicdump_encode_dump(wire, sizeof(wire), dump)) {
        return 1;
    }
    wire[PANICDUMP_WIRE_MAGIC_OFFSET] = 0u;
    wire[PANICDUMP_WIRE_MAGIC_OFFSET + 1u] = 0u;
    wire[PANICDUMP_WIRE_MAGIC_OFFSET + 2u] = 0u;
    wire[PANICDUMP_WIRE_MAGIC_OFFSET + 3u] = 0u;
    if (panicdump_validate_wire(wire, sizeof(wire))) {
        return 1;
    }

    panicdump_clear();
    if (panicdump_has_valid()) {
        return 1;
    }

    fill_sample_regs(&regs);
    fill_sample_stack(&stack);
    panicdump_set_user_tag(1u);
    panicdump_commit_snapshot(&regs, &stack, PANICDUMP_FAULT_BUS,
                              PANICDUMP_FLAG_FRAME_VALID | PANICDUMP_FLAG_STACK_VALID,
                              0xFFFFFFF9u);

    if (!panicdump_has_valid()) {
        return 1;
    }

    dump = panicdump_get();
    if (!dump || dump->user_tag != 1u || dump->fault_reason != PANICDUMP_FAULT_BUS) {
        return 1;
    }

    return 0;
}
