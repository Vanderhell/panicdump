/**
 * panicdump_fault_entry.S — Cortex-M3/M4 exception entry stubs.
 *
 * For each fault type we need to:
 *   1. Determine whether MSP or PSP was active (from EXC_RETURN in LR)
 *   2. Get the correct exception frame pointer
 *   3. Call panicdump_fault_handler_c(exc_frame, fault_reason, use_psp)
 *
 * EXC_RETURN bit[2]:
 *   1 = return to Thread mode using PSP
 *   0 = return to Handler mode using MSP (or Thread+MSP)
 *
 * Calling convention (AAPCS):
 *   R0 = exc_frame pointer
 *   R1 = fault_reason
 *   R2 = use_psp flag
 */

    .syntax unified
    .thumb

    .extern panicdump_fault_handler_c

/* -------------------------------------------------------------------------
 * Macro: build a fault entry stub for a given fault reason code
 *
 * PANICDUMP_FAULT_HARD   = 1
 * PANICDUMP_FAULT_MEM    = 2
 * PANICDUMP_FAULT_BUS    = 3
 * PANICDUMP_FAULT_USAGE  = 4
 * ---------------------------------------------------------------------- */

.macro FAULT_ENTRY name, reason
    .global \name
    .thumb_func
    .type   \name, %function
\name:
    /* Test EXC_RETURN bit[2]: 1 = PSP was active */
    tst     lr, #4
    ite     eq
    mrseq   r0, msp      /* MSP was used — get frame from MSP */
    mrsne   r0, psp      /* PSP was used — get frame from PSP */

    mov     r1, #\reason /* fault_reason */
    ite     eq
    moveq   r2, #0       /* use_psp = 0 */
    movne   r2, #1       /* use_psp = 1 */

    b       panicdump_fault_handler_c
    .size   \name, . - \name
.endm

/* -------------------------------------------------------------------------
 * Fault entry stubs
 * ---------------------------------------------------------------------- */

    .section .text

FAULT_ENTRY panicdump_HardFault_Handler,  1
FAULT_ENTRY panicdump_MemManage_Handler,  2
FAULT_ENTRY panicdump_BusFault_Handler,   3
FAULT_ENTRY panicdump_UsageFault_Handler, 4

/* -------------------------------------------------------------------------
 * END
 * ---------------------------------------------------------------------- */
