/**
 * generic_cortexm_demo.c — panicdump integration reference
 *
 * This file shows the minimal steps to integrate panicdump into
 * ANY bare-metal Cortex-M3/M4 project, regardless of vendor.
 *
 * Steps:
 *   1. Route fault vectors to panicdump handlers (in your vector table)
 *   2. Add .noinit section to your linker script
 *   3. Call panicdump_boot_check_and_export() at boot
 *   4. Call panicdump_set_user_tag() at key checkpoints
 *
 * See docs/INTEGRATION.md for full integration guide.
 */

#include "panicdump.h"

/* =========================================================================
 * STEP 1: Vector table hookup
 *
 * In your vector table (startup file or vectors.c), redirect fault handlers:
 *
 *   extern void panicdump_HardFault_Handler(void);
 *   extern void panicdump_MemManage_Handler(void);
 *   extern void panicdump_BusFault_Handler(void);
 *   extern void panicdump_UsageFault_Handler(void);
 *
 *   __attribute__((section(".isr_vector")))
 *   void (* const g_vectors[])(void) = {
 *       (void*)&_estack,
 *       Reset_Handler,
 *       NMI_Handler,
 *       panicdump_HardFault_Handler,    // ← panicdump
 *       panicdump_MemManage_Handler,    // ← panicdump
 *       panicdump_BusFault_Handler,     // ← panicdump
 *       panicdump_UsageFault_Handler,   // ← panicdump
 *       ...
 *   };
 * ========================================================================= */

/* =========================================================================
 * STEP 2: Linker script — add .noinit section
 *
 *   .noinit (NOLOAD) :
 *   {
 *       _snoinit = .;
 *       KEEP(*(.noinit))
 *       _enoinit = .;
 *   } >RAM
 *
 * IMPORTANT: Your startup code must NOT zero this region.
 * ========================================================================= */

/* =========================================================================
 * STEP 3 & 4: Application integration
 * ========================================================================= */

/*
 * Provide your own UART write function.
 * The signature must be: void write_char(char c)
 */
extern void your_uart_putchar(char c);   /* implement this */

/*
 * Application state tags — define these to match your system.
 * The last set tag appears in the crash dump as 'user_tag'.
 */
#define TAG_BOOT           0x01u
#define TAG_PERIPH_INIT    0x10u
#define TAG_SENSORS_INIT   0x20u
#define TAG_MAIN_LOOP      0x30u
#define TAG_COMMS          0x40u

int main(void)
{
    /* Tag: starting up */
    panicdump_set_user_tag(TAG_BOOT);

    /* --- Your hardware init --- */
    /* your_bsp_init(); */
    panicdump_set_user_tag(TAG_PERIPH_INIT);

    /* ---------------------------------------------------------------
     * Check for crash dump from previous boot.
     *
     * Place this EARLY in main(), before any code that might itself crash.
     * The dump will be printed via your_uart_putchar, then cleared.
     * --------------------------------------------------------------- */
    if (panicdump_has_valid()) {
        /* Optional: signal to the user that a dump exists */
        /* your_led_blink_error(); */

        panicdump_boot_check_and_export(your_uart_putchar);
    }

    /* --- More init --- */
    panicdump_set_user_tag(TAG_SENSORS_INIT);
    /* your_sensors_init(); */

    /* --- Main loop --- */
    panicdump_set_user_tag(TAG_MAIN_LOOP);

    while (1) {
        /* Update tag to reflect what the system is doing */
        panicdump_set_user_tag(TAG_COMMS);
        /* your_comms_task(); */

        panicdump_set_user_tag(TAG_MAIN_LOOP);
        /* your_main_task(); */
    }

    return 0;
}

/* =========================================================================
 * Software panic example
 *
 * If you have an assert or watchdog handler, you can explicitly trigger
 * a dump before resetting:
 * ========================================================================= */

void assert_failed(const char *file, int line)
{
    (void)file;
    (void)line;

    /* panicdump_trigger() saves dump + resets — does not return */
    panicdump_trigger("assert_failed");
}
