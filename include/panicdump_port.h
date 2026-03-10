#ifndef PANICDUMP_PORT_H
#define PANICDUMP_PORT_H

/**
 * panicdump_port.h — Platform abstraction interface.
 *
 * All architecture-specific constants, register addresses and compile-time
 * guards live here so that panicdump_port_cortexm.c has no bare #ifdefs
 * scattered in it.
 *
 * v1: Cortex-M3 (__ARM_ARCH_7M__) and Cortex-M4 (__ARM_ARCH_7EM__) only.
 * Host/test builds are supported via PANICDUMP_HOST_BUILD.
 */

#include "panicdump.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Compile-time architecture guard
 *
 * Catch misuse early: if someone tries to build panicdump_port_cortexm.c
 * on an unsupported target (ESP32, RISC-V, Cortex-M0...) they get a clear
 * error instead of silently reading garbage SCB addresses.
 * ---------------------------------------------------------------------- */

#if !defined(PANICDUMP_HOST_BUILD)
  #if !defined(__ARM_ARCH_7M__) && !defined(__ARM_ARCH_7EM__)
    #error "panicdump v1 requires Cortex-M3 (__ARM_ARCH_7M__) or \
Cortex-M4 (__ARM_ARCH_7EM__). For other targets define PANICDUMP_HOST_BUILD \
(mock/test) or add a new port. See docs/PORTING.md."
  #endif
#endif

/* -------------------------------------------------------------------------
 * Architecture ID (stored in dump header)
 * ---------------------------------------------------------------------- */

#if defined(PANICDUMP_ARCH_OVERRIDE)
    #define PANICDUMP_PORT_ARCH_ID  PANICDUMP_ARCH_OVERRIDE
#elif defined(__ARM_ARCH_7EM__)
    #define PANICDUMP_PORT_ARCH_ID  PANICDUMP_ARCH_CORTEXM4
#elif defined(__ARM_ARCH_7M__)
    #define PANICDUMP_PORT_ARCH_ID  PANICDUMP_ARCH_CORTEXM3
#else
    #define PANICDUMP_PORT_ARCH_ID  UINT32_C(0xFFFF)  /* host/mock */
#endif

/* -------------------------------------------------------------------------
 * Cortex-M3/M4 SCB register addresses (ARMv7-M Architecture Reference)
 *
 * Centralised here so panicdump_port_cortexm.c has no bare magic numbers.
 * These are fixed by the ARM architecture — not vendor-specific.
 * ---------------------------------------------------------------------- */

#define PANICDUMP_SCB_SHCSR  ((volatile uint32_t *)0xE000ED24u)
#define PANICDUMP_SCB_CFSR   ((volatile uint32_t *)0xE000ED28u)
#define PANICDUMP_SCB_HFSR   ((volatile uint32_t *)0xE000ED2Cu)
#define PANICDUMP_SCB_DFSR   ((volatile uint32_t *)0xE000ED30u)
#define PANICDUMP_SCB_MMFAR  ((volatile uint32_t *)0xE000ED34u)
#define PANICDUMP_SCB_BFAR   ((volatile uint32_t *)0xE000ED38u)
#define PANICDUMP_SCB_AFSR   ((volatile uint32_t *)0xE000ED3Cu)
#define PANICDUMP_SCB_AIRCR  ((volatile uint32_t *)0xE000ED0Cu)

/* -------------------------------------------------------------------------
 * Retained RAM region bounds for stack-slice bounds checking.
 *
 * Override these for your specific MCU linker layout.
 * Default covers STM32F4 SRAM1 (128KB @ 0x20000000).
 *
 * The stack-slice capture will clamp to this window rather than
 * reading from an arbitrary SP value that could fault.
 *
 * Example for STM32F407:
 *   SRAM1: 0x20000000 – 0x2001FFFF  (128KB)
 *
 * Example for STM32F103:
 *   SRAM:  0x20000000 – 0x20004FFF  (20KB)
 *
 * Set both to 0 to disable bounds checking (not recommended).
 * ---------------------------------------------------------------------- */

#if !defined(PANICDUMP_RAM_BASE)
    #define PANICDUMP_RAM_BASE  UINT32_C(0x20000000)
#endif

#if !defined(PANICDUMP_RAM_END)
    #define PANICDUMP_RAM_END   UINT32_C(0x20020000)   /* 128KB */
#endif

/* -------------------------------------------------------------------------
 * Port function declarations
 * ---------------------------------------------------------------------- */

/**
 * Capture register state from a software-trigger context.
 *
 * Uses inline asm (MRS) to read the actual general-purpose and special
 * registers at the call site. The caller's r0–r12 are captured via
 * a small asm block — not zeroed.
 */
void panicdump_port_capture_sw_regs(panicdump_regs_t  *out_regs,
                                    panicdump_stack_t *out_stack);

/**
 * Issue a system reset. Does not return.
 * Cortex-M: writes AIRCR SYSRESETREQ.
 * Host: calls abort().
 */
void panicdump_port_reset(void) __attribute__((noreturn));

/**
 * C entry point called from assembly fault stubs.
 *
 * exc_frame   — pointer to hardware-stacked exception frame (R0–xPSR)
 * fault_reason — PANICDUMP_FAULT_* code
 * use_psp     — 1 if PSP was active at fault, 0 if MSP
 */
void panicdump_fault_handler_c(const uint32_t *exc_frame,
                                uint32_t        fault_reason,
                                int             use_psp);

#ifdef __cplusplus
}
#endif

#endif /* PANICDUMP_PORT_H */
