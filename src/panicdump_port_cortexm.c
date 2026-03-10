/**
 * panicdump_port_cortexm.c — Cortex-M3/M4 port implementation.
 *
 * Architecture guard, SCB addresses and RAM bounds are all defined in
 * panicdump_port.h — this file has no bare #ifdefs or magic numbers.
 *
 * Assembly entry stubs: ports/cortexm/panicdump_fault_entry.S
 */

#include "panicdump_port.h"
#include "panicdump.h"

#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Cortex-M hardware exception frame (auto-stacked by CPU on exception entry)
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Internal: panicdump_commit (defined in panicdump.c)
 * ---------------------------------------------------------------------- */

extern void panicdump_commit(panicdump_fault_t        fault_reason,
                             const panicdump_regs_t  *regs,
                             const panicdump_stack_t *stack);

/* -------------------------------------------------------------------------
 * Stack slice helper — with bounds checking
 *
 * Reads up to PANICDUMP_STACK_SLICE_BYTES from `sp`, but ONLY within
 * [PANICDUMP_RAM_BASE, PANICDUMP_RAM_END).
 *
 * If sp is outside the known RAM window, stack_bytes is set to 0 and
 * captured_sp is still recorded (useful for diagnosing stack corruption).
 * This prevents a nested fault from a corrupt SP value.
 * ---------------------------------------------------------------------- */

static void capture_stack_slice(panicdump_stack_t *out, uint32_t sp)
{
    memset(out, 0, sizeof(*out));
    out->captured_sp = sp;

    if (sp < PANICDUMP_RAM_BASE ||
        sp > (PANICDUMP_RAM_END - PANICDUMP_STACK_SLICE_BYTES))
    {
        /* SP out of known RAM range — record 0 bytes, do not dereference */
        out->stack_bytes = 0;
        return;
    }

    out->stack_bytes = PANICDUMP_STACK_SLICE_BYTES;
    memcpy(out->data, (const void *)(uintptr_t)sp, PANICDUMP_STACK_SLICE_BYTES);
}

/* -------------------------------------------------------------------------
 * Read special and SCB registers — Cortex-M3/M4 inline asm
 * Addresses come from PANICDUMP_SCB_* macros in panicdump_port.h.
 * ---------------------------------------------------------------------- */

static void read_special_regs(panicdump_regs_t *r)
{
    __asm volatile ("mrs %0, msp"       : "=r"(r->msp));
    __asm volatile ("mrs %0, psp"       : "=r"(r->psp));
    __asm volatile ("mrs %0, control"   : "=r"(r->control));
    __asm volatile ("mrs %0, primask"   : "=r"(r->primask));
    __asm volatile ("mrs %0, basepri"   : "=r"(r->basepri));
    __asm volatile ("mrs %0, faultmask" : "=r"(r->faultmask));

    r->cfsr  = *PANICDUMP_SCB_CFSR;
    r->hfsr  = *PANICDUMP_SCB_HFSR;
    r->dfsr  = *PANICDUMP_SCB_DFSR;
    r->mmfar = *PANICDUMP_SCB_MMFAR;
    r->bfar  = *PANICDUMP_SCB_BFAR;
    r->afsr  = *PANICDUMP_SCB_AFSR;
    r->shcsr = *PANICDUMP_SCB_SHCSR;
}

/* -------------------------------------------------------------------------
 * Fault handler — C entry point
 *
 * Called from panicdump_fault_entry.S with:
 *   exc_frame    — pointer to hardware-stacked frame (R0-xPSR)
 *   fault_reason — PANICDUMP_FAULT_* code
 *   use_psp      — 1 if PSP was active at fault
 * ---------------------------------------------------------------------- */

void panicdump_fault_handler_c(const uint32_t *exc_frame,
                                uint32_t        fault_reason,
                                int             use_psp)
{
    panicdump_regs_t  regs;
    panicdump_stack_t stack;

    memset(&regs,  0, sizeof(regs));
    memset(&stack, 0, sizeof(stack));

    const cortexm_exc_frame_t *frame = (const cortexm_exc_frame_t *)exc_frame;
    regs.r0   = frame->r0;
    regs.r1   = frame->r1;
    regs.r2   = frame->r2;
    regs.r3   = frame->r3;
    regs.r12  = frame->r12;
    regs.lr   = frame->lr;
    regs.pc   = frame->pc;
    regs.xpsr = frame->xpsr;

    read_special_regs(&regs);

    uint32_t sp = use_psp ? regs.psp : regs.msp;
    capture_stack_slice(&stack, sp);

    panicdump_commit((panicdump_fault_t)fault_reason, &regs, &stack);

#if defined(PANICDUMP_HALT_ON_FAULT)
    __asm volatile ("bkpt #0");
    while (1) { __asm volatile ("nop"); }
#else
    panicdump_port_reset();
#endif
}

/* -------------------------------------------------------------------------
 * Software-context register capture
 *
 * Captures the actual caller's r0-r12 via a single STM instruction so the
 * compiler cannot clobber them before we read them. Previously this zeroed
 * r0-r12 which defeated the purpose of a software panic dump.
 * ---------------------------------------------------------------------- */

void panicdump_port_capture_sw_regs(panicdump_regs_t  *out_regs,
                                    panicdump_stack_t *out_stack)
{
    uint32_t gp[13];   /* r0-r12 */
    uint32_t sp_val, lr_val, pc_val;

    /*
     * Single STM captures r0-r12 before the compiler touches them.
     * "memory" clobber prevents reordering across this boundary.
     */
    __asm volatile (
        "stmia  %[buf], {r0-r12}    \n"
        "mov    %[sp],  sp          \n"
        "mov    %[lr],  lr          \n"
        "mov    %[pc],  pc          \n"
        : [sp] "=r"(sp_val),
          [lr] "=r"(lr_val),
          [pc] "=r"(pc_val)
        : [buf] "r"(gp)
        : "memory"
    );

    out_regs->r0   = gp[0];
    out_regs->r1   = gp[1];
    out_regs->r2   = gp[2];
    out_regs->r3   = gp[3];
    out_regs->r12  = gp[12];
    out_regs->lr   = lr_val;
    out_regs->pc   = pc_val;
    out_regs->xpsr = 0;  /* not meaningful outside exception context */

    read_special_regs(out_regs);
    capture_stack_slice(out_stack, sp_val);
}

/* -------------------------------------------------------------------------
 * System reset — AIRCR SYSRESETREQ
 * ---------------------------------------------------------------------- */

void panicdump_port_reset(void)
{
    __asm volatile ("dsb" ::: "memory");
    *PANICDUMP_SCB_AIRCR = UINT32_C(0x05FA0004);  /* VECTKEY | SYSRESETREQ */
    __asm volatile ("dsb" ::: "memory");
    while (1) {}
}
