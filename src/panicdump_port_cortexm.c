#include "panicdump_port.h"
#include "panicdump_util.h"

#include <stdint.h>

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} cortexm_exc_frame_t;

static bool panicdump_frame_within_ram(const uint32_t *frame)
{
    uintptr_t addr = (uintptr_t)frame;
    return addr >= (uintptr_t)PANICDUMP_RAM_BASE &&
           addr <= (uintptr_t)(PANICDUMP_RAM_END - PANICDUMP_WIRE_STACK_BYTES) &&
           ((addr & 0x3u) == 0u);
}

static void panicdump_capture_stack_slice(panicdump_stack_t *out, const uint32_t *frame)
{
    panicdump_zero_bytes(out, sizeof(*out));
    out->captured_sp = (uint32_t)(uintptr_t)frame;

    if (!panicdump_frame_within_ram(frame)) {
        return;
    }

    out->stack_bytes = PANICDUMP_WIRE_STACK_BYTES;
    panicdump_copy_bytes(out->data, frame, PANICDUMP_WIRE_STACK_BYTES);
}

static void panicdump_read_special_regs(panicdump_regs_t *regs)
{
    __asm volatile ("mrs %0, msp" : "=r"(regs->msp));
    __asm volatile ("mrs %0, psp" : "=r"(regs->psp));
    __asm volatile ("mrs %0, control" : "=r"(regs->control));
    __asm volatile ("mrs %0, primask" : "=r"(regs->primask));
    __asm volatile ("mrs %0, basepri" : "=r"(regs->basepri));
    __asm volatile ("mrs %0, faultmask" : "=r"(regs->faultmask));

    regs->cfsr = *PANICDUMP_SCB_CFSR;
    regs->hfsr = *PANICDUMP_SCB_HFSR;
    regs->dfsr = *PANICDUMP_SCB_DFSR;
    regs->mmfar = *PANICDUMP_SCB_MMFAR;
    regs->bfar = *PANICDUMP_SCB_BFAR;
    regs->afsr = *PANICDUMP_SCB_AFSR;
    regs->shcsr = *PANICDUMP_SCB_SHCSR;
}

static bool panicdump_exc_frame_safe(const uint32_t *exc_frame)
{
    return panicdump_frame_within_ram(exc_frame);
}

void panicdump_fault_handler_c(const uint32_t *exc_frame,
                               uint32_t fault_reason,
                               int use_psp,
                               uint32_t exc_return)
{
    panicdump_regs_t regs;
    panicdump_stack_t stack;
    uint32_t flags = 0u;

    panicdump_zero_bytes(&regs, sizeof(regs));
    panicdump_zero_bytes(&stack, sizeof(stack));

    if (use_psp) {
        flags |= PANICDUMP_FLAG_USE_PSP;
    }

    if (panicdump_exc_frame_safe(exc_frame)) {
        const cortexm_exc_frame_t *frame = (const cortexm_exc_frame_t *)exc_frame;
        regs.r0 = frame->r0;
        regs.r1 = frame->r1;
        regs.r2 = frame->r2;
        regs.r3 = frame->r3;
        regs.r12 = frame->r12;
        regs.lr = frame->lr;
        regs.pc = frame->pc;
        regs.xpsr = frame->xpsr;
        flags |= PANICDUMP_FLAG_FRAME_VALID;
        panicdump_capture_stack_slice(&stack, exc_frame);
        if (stack.stack_bytes == PANICDUMP_WIRE_STACK_BYTES) {
            flags |= PANICDUMP_FLAG_STACK_VALID;
        }
    } else {
        flags |= PANICDUMP_FLAG_INVALID_FRAME;
        stack.captured_sp = (uint32_t)(uintptr_t)exc_frame;
        stack.stack_bytes = 0u;
    }

    panicdump_read_special_regs(&regs);
    panicdump_commit_snapshot(&regs, &stack, fault_reason, flags, exc_return);

#if defined(PANICDUMP_HALT_ON_FAULT)
    __asm volatile ("bkpt #0");
    for (;;) {
        __asm volatile ("nop");
    }
#else
    panicdump_port_reset();
#endif
}

void panicdump_port_capture_sw_regs(panicdump_regs_t *out_regs,
                                    panicdump_stack_t *out_stack)
{
    uint32_t gp[13];
    uint32_t sp_val;
    uint32_t lr_val;
    uint32_t pc_val;

    __asm volatile (
        "stmia %[buf], {r0-r12} \n"
        "mov   %[sp], sp        \n"
        "mov   %[lr], lr        \n"
        "mov   %[pc], pc        \n"
        : [sp] "=r"(sp_val),
          [lr] "=r"(lr_val),
          [pc] "=r"(pc_val)
        : [buf] "r"(gp)
        : "memory"
    );

    panicdump_zero_bytes(out_regs, sizeof(*out_regs));
    out_regs->r0 = gp[0];
    out_regs->r1 = gp[1];
    out_regs->r2 = gp[2];
    out_regs->r3 = gp[3];
    out_regs->r12 = gp[12];
    out_regs->lr = lr_val;
    out_regs->pc = pc_val;
    out_regs->xpsr = 0u;
    out_regs->msp = 0u;
    out_regs->psp = 0u;

    panicdump_read_special_regs(out_regs);

    panicdump_zero_bytes(out_stack, sizeof(*out_stack));
    out_stack->captured_sp = sp_val;
    out_stack->stack_bytes = 0u;
    if (panicdump_frame_within_ram((const uint32_t *)(uintptr_t)sp_val)) {
        out_stack->stack_bytes = PANICDUMP_WIRE_STACK_BYTES;
        panicdump_copy_bytes(out_stack->data, (const void *)(uintptr_t)sp_val,
                             PANICDUMP_WIRE_STACK_BYTES);
    }
}

void panicdump_port_publish_barrier(void)
{
    __asm volatile ("" ::: "memory");
    __asm volatile ("dmb" ::: "memory");
}

void panicdump_port_reset(void)
{
    __asm volatile ("dsb" ::: "memory");
    *PANICDUMP_SCB_AIRCR = UINT32_C(0x05FA0004);
    __asm volatile ("dsb" ::: "memory");
    for (;;) {
    }
}
