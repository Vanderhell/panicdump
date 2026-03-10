#ifndef STM32F4_UART_H
#define STM32F4_UART_H

/**
 * stm32f4_uart.h — Minimal USART2 driver for panicdump demo.
 *
 * Uses USART2 on PA2 (TX) / PA3 (RX), 115200 8N1, no interrupts.
 * No HAL, no CMSIS — direct register access only.
 *
 * Hardware setup (STM32F4 Discovery / Nucleo):
 *   PA2 → USART2 TX → USB-UART bridge → PC
 *   PA3 → USART2 RX (optional)
 *
 * On Nucleo-F411RE/F446RE, USART2 is connected to the ST-Link VCP.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    uart2_init(uint32_t sysclk_hz, uint32_t baud);
void    uart2_putchar(char c);
void    uart2_puts(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* STM32F4_UART_H */
