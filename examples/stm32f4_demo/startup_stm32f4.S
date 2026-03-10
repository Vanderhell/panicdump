/**
 * startup_stm32f4.S — Minimal Cortex-M4 startup for STM32F4.
 *
 * Critical for panicdump: this startup intentionally does NOT zero
 * the .noinit section. This allows crash dumps to survive warm resets.
 *
 * Startup sequence:
 *   1. Set SP to _estack
 *   2. Copy .data from Flash to RAM
 *   3. Zero .bss (but NOT .noinit)
 *   4. Call SystemInit (optional)
 *   5. Call main()
 *   6. Infinite loop if main returns
 */

    .syntax unified
    .cpu cortex-m4
    .thumb

    .global Reset_Handler
    .global Default_Handler

/* -------------------------------------------------------------------------
 * Vector table
 * ---------------------------------------------------------------------- */

    .section .isr_vector, "a", %progbits
    .type _vector_table, %object

_vector_table:
    /* Core exceptions */
    .word _estack                           /* Stack pointer initial value */
    .word Reset_Handler                     /* Reset */
    .word Default_Handler                   /* NMI */
    .word panicdump_HardFault_Handler       /* HardFault  ← panicdump */
    .word panicdump_MemManage_Handler       /* MemManage  ← panicdump */
    .word panicdump_BusFault_Handler        /* BusFault   ← panicdump */
    .word panicdump_UsageFault_Handler      /* UsageFault ← panicdump */
    .word 0                                 /* Reserved */
    .word 0                                 /* Reserved */
    .word 0                                 /* Reserved */
    .word 0                                 /* Reserved */
    .word Default_Handler                   /* SVCall */
    .word Default_Handler                   /* Debug Monitor */
    .word 0                                 /* Reserved */
    .word Default_Handler                   /* PendSV */
    .word Default_Handler                   /* SysTick */

    /* IRQs 0..67 (STM32F407 — fill as needed) */
    .rept 68
    .word Default_Handler
    .endr

    .size _vector_table, . - _vector_table

/* -------------------------------------------------------------------------
 * External symbols
 * ---------------------------------------------------------------------- */

    .extern panicdump_HardFault_Handler
    .extern panicdump_MemManage_Handler
    .extern panicdump_BusFault_Handler
    .extern panicdump_UsageFault_Handler

    .extern _sidata     /* Flash load address of .data */
    .extern _sdata      /* RAM start of .data */
    .extern _edata      /* RAM end of .data */
    .extern _sbss       /* RAM start of .bss */
    .extern _ebss       /* RAM end of .bss */
    /* NOTE: _snoinit/_enoinit deliberately NOT zeroed */

    .extern SystemInit
    .extern main

/* -------------------------------------------------------------------------
 * Reset handler
 * ---------------------------------------------------------------------- */

    .section .text.Reset_Handler
    .thumb_func
    .type Reset_Handler, %function

Reset_Handler:
    /* 1. Copy .data section from Flash to RAM */
    ldr     r0, =_sidata
    ldr     r1, =_sdata
    ldr     r2, =_edata
    b       .copy_check
.copy_loop:
    ldr     r3, [r0], #4
    str     r3, [r1], #4
.copy_check:
    cmp     r1, r2
    bcc     .copy_loop

    /* 2. Zero .bss section
     * IMPORTANT: .noinit is NOT zeroed — this preserves crash dumps.
     */
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    mov     r2, #0
    b       .bss_check
.bss_loop:
    str     r2, [r0], #4
.bss_check:
    cmp     r0, r1
    bcc     .bss_loop

    /* 3. SystemInit (clock setup etc.) — optional, weak symbol */
    bl      SystemInit

    /* 4. Call main */
    bl      main

    /* 5. If main returns, halt */
.halt:
    b       .halt

    .size Reset_Handler, . - Reset_Handler

/* -------------------------------------------------------------------------
 * Default handler (infinite loop)
 * ---------------------------------------------------------------------- */

    .section .text.Default_Handler
    .thumb_func
    .type Default_Handler, %function
    .weak Default_Handler

Default_Handler:
    b       Default_Handler

    .size Default_Handler, . - Default_Handler

/* -------------------------------------------------------------------------
 * Weak SystemInit (overridable by user/CMSIS)
 * ---------------------------------------------------------------------- */

    .section .text.SystemInit
    .thumb_func
    .type SystemInit, %function
    .weak SystemInit

SystemInit:
    bx      lr

    .size SystemInit, . - SystemInit
