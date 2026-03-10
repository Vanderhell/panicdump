/**
 * main.c — panicdump STM32F4 demo
 *
 * Demonstrates the full crash dump workflow:
 *   1. On boot: check for previous crash dump, export it via UART, clear it
 *   2. Set user tags to track application state
 *   3. Deliberately trigger a HardFault after 3 seconds (demo only)
 *
 * To use in a real application:
 *   - Remove the demo fault trigger
 *   - Add your application code between the tags
 *   - Keep panicdump_boot_check_and_export() at the top of main()
 *
 * Hardware: STM32F4 Discovery or Nucleo-F4xx
 *   UART output: PA2 (USART2 TX) → 115200 8N1
 *
 * Decode the output:
 *   python3 tools/decode_panicdump.py --hex uart_capture.txt
 */

#include "panicdump.h"
#include "stm32f4_uart.h"

#include <stdint.h>

/* -------------------------------------------------------------------------
 * System clock
 * Using HSI (16MHz) for simplicity — no PLL setup needed for demo.
 * APB1 = 16MHz (no prescaler).
 * ---------------------------------------------------------------------- */

#define SYSCLK_HZ    16000000UL
#define APB1_HZ      16000000UL

/* -------------------------------------------------------------------------
 * Application state tags
 * These help you know WHERE the MCU was when it crashed.
 * ---------------------------------------------------------------------- */

typedef enum {
    APP_TAG_BOOT        = 0x00,
    APP_TAG_HW_INIT     = 0x10,
    APP_TAG_DUMP_CHECK  = 0x20,
    APP_TAG_RUNNING     = 0x30,
    APP_TAG_WAIT_FAULT  = 0x40,
} app_tag_t;

/* -------------------------------------------------------------------------
 * Tiny delay (busy-wait, not calibrated — demo only)
 * ---------------------------------------------------------------------- */

static void delay_ms(uint32_t ms)
{
    /* Rough: 16MHz, ~4 cycles per iteration */
    volatile uint32_t count = ms * (SYSCLK_HZ / 4000);
    while (count--) {}
}

/* -------------------------------------------------------------------------
 * Deliberate fault trigger (demo only — remove in real application)
 *
 * Triggers a BusFault by writing to an invalid address.
 * This causes panicdump_BusFault_Handler to fire, capture the dump,
 * and reset the MCU.
 *
 * On the next boot, panicdump_boot_check_and_export() will print the dump.
 * ---------------------------------------------------------------------- */

static void __attribute__((noinline)) demo_trigger_fault(void)
{
    uart2_puts("\r\n[demo] Triggering deliberate BusFault in 1 second...\r\n");
    delay_ms(1000);

    /* Write to reserved/invalid address → BusFault */
    volatile uint32_t *bad_addr = (volatile uint32_t *)0xDEAD0000;
    *bad_addr = 0xCAFEBABE;

    /* Should never reach here */
    uart2_puts("[demo] ERROR: fault did not fire!\r\n");
}

/* -------------------------------------------------------------------------
 * UART callback for panicdump
 * ---------------------------------------------------------------------- */

static void uart_write(char c)
{
    uart2_putchar(c);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void)
{
    /* ----------------------------------------------------------------
     * Stage 1: Hardware init
     * ---------------------------------------------------------------- */
    panicdump_set_user_tag(APP_TAG_HW_INIT);
    uart2_init(APB1_HZ, 115200);

    uart2_puts("\r\n");
    uart2_puts("========================================\r\n");
    uart2_puts("  panicdump STM32F4 demo\r\n");
    uart2_puts("========================================\r\n");

    /* ----------------------------------------------------------------
     * Stage 2: Check for crash dump from previous boot
     *
     * If a crash happened last time, this exports the dump via UART
     * in hex-framed format, then clears it.
     *
     * Capture terminal output to a file, then decode:
     *   python3 tools/decode_panicdump.py --hex uart.txt
     * ---------------------------------------------------------------- */
    panicdump_set_user_tag(APP_TAG_DUMP_CHECK);

    if (panicdump_has_valid()) {
        uart2_puts("\r\n[panicdump] Previous crash dump found!\r\n");
        uart2_puts("[panicdump] Exporting...\r\n\r\n");
        panicdump_export_hex(uart_write);
        uart2_puts("\r\n[panicdump] Dump exported. Clearing.\r\n\r\n");
        panicdump_clear();
    } else {
        uart2_puts("[panicdump] No previous crash dump.\r\n");
    }

    /* ----------------------------------------------------------------
     * Stage 3: Application running
     * ---------------------------------------------------------------- */
    panicdump_set_user_tag(APP_TAG_RUNNING);
    uart2_puts("[app] Running. Tag=0x30\r\n");

    delay_ms(1000);

    /* ----------------------------------------------------------------
     * Stage 4: Demo fault trigger
     *
     * In a real application, replace this with your actual code.
     * The crash will be captured on the NEXT boot.
     * ---------------------------------------------------------------- */
    panicdump_set_user_tag(APP_TAG_WAIT_FAULT);

#if defined(PANICDUMP_DEMO_TRIGGER_FAULT)
    demo_trigger_fault();
#else
    /* Normal operation — just blink or run your app */
    uart2_puts("[app] Running normally (no demo fault). Set PANICDUMP_DEMO_TRIGGER_FAULT to test.\r\n");
    while (1) {
        delay_ms(500);
        uart2_puts(".\r\n");
    }
#endif

    return 0;
}
