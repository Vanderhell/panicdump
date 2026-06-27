#ifndef PANICDUMP_PORT_H
#define PANICDUMP_PORT_H

#include "panicdump.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANICDUMP_EXCEPTION_FRAME_WORDS 8u
#define PANICDUMP_EXCEPTION_FRAME_BYTES  (PANICDUMP_EXCEPTION_FRAME_WORDS * 4u)

#define PANICDUMP_SCB_CFSR   ((volatile uint32_t *)0xE000ED28u)
#define PANICDUMP_SCB_HFSR   ((volatile uint32_t *)0xE000ED2Cu)
#define PANICDUMP_SCB_DFSR   ((volatile uint32_t *)0xE000ED30u)
#define PANICDUMP_SCB_MMFAR  ((volatile uint32_t *)0xE000ED34u)
#define PANICDUMP_SCB_BFAR   ((volatile uint32_t *)0xE000ED38u)
#define PANICDUMP_SCB_AFSR   ((volatile uint32_t *)0xE000ED3Cu)
#define PANICDUMP_SCB_SHCSR  ((volatile uint32_t *)0xE000ED24u)
#define PANICDUMP_SCB_AIRCR  ((volatile uint32_t *)0xE000ED0Cu)

#if defined(PANICDUMP_HOST_BUILD)
#define PANICDUMP_PORT_ARCH_ID PANICDUMP_ARCH_CORTEXM4
#else
#if !defined(__ARM_ARCH_7M__) && !defined(__ARM_ARCH_7EM__)
#error "panicdump requires a Cortex-M3 or Cortex-M4 non-FPU build"
#endif

#if defined(__ARM_ARCH_7EM__) && defined(__ARM_FP)
#error "panicdump does not support Cortex-M4F/M7 FPU builds yet"
#endif

#if !defined(PANICDUMP_RAM_BASE) || !defined(PANICDUMP_RAM_END)
#error "PANICDUMP_RAM_BASE and PANICDUMP_RAM_END are required for Cortex-M builds"
#endif

#if (PANICDUMP_RAM_END <= PANICDUMP_RAM_BASE)
#error "PANICDUMP_RAM_END must be greater than PANICDUMP_RAM_BASE"
#endif

#if defined(__ARM_ARCH_7EM__)
#define PANICDUMP_PORT_ARCH_ID PANICDUMP_ARCH_CORTEXM4
#else
#define PANICDUMP_PORT_ARCH_ID PANICDUMP_ARCH_CORTEXM3
#endif
#endif

void panicdump_port_capture_sw_regs(panicdump_regs_t *out_regs,
                                    panicdump_stack_t *out_stack);

void panicdump_port_publish_barrier(void);
void panicdump_port_reset(void) PANICDUMP_NORETURN;

void panicdump_commit_snapshot(const panicdump_regs_t *regs,
                               const panicdump_stack_t *stack,
                               uint32_t fault_reason,
                               uint32_t flags,
                               uint32_t exc_return);

void panicdump_fault_handler_c(const uint32_t *exc_frame,
                               uint32_t fault_reason,
                               int use_psp,
                               uint32_t exc_return);

#ifdef __cplusplus
}
#endif

#endif /* PANICDUMP_PORT_H */
