/**
 * stm32f4_uart.c — Minimal USART2 driver (direct register access).
 *
 * Register addresses for STM32F4xx (RM0090).
 * Tested on STM32F407, STM32F411, STM32F446.
 */

#include "stm32f4_uart.h"

/* -------------------------------------------------------------------------
 * Register addresses (STM32F4xx Reference Manual RM0090)
 * ---------------------------------------------------------------------- */

/* RCC */
#define RCC_BASE        UINT32_C(0x40023800)
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

/* GPIOA */
#define GPIOA_BASE      UINT32_C(0x40020000)
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL      (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

/* USART2 */
#define USART2_BASE     UINT32_C(0x40004400)
#define USART2_SR       (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR       (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR      (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1      (*(volatile uint32_t *)(USART2_BASE + 0x0C))

/* Bit definitions */
#define RCC_AHB1ENR_GPIOAEN  (1u << 0)
#define RCC_APB1ENR_USART2EN (1u << 17)
#define USART_SR_TXE         (1u << 7)
#define USART_SR_TC          (1u << 6)
#define USART_CR1_UE         (1u << 13)
#define USART_CR1_TE         (1u << 3)

/* -------------------------------------------------------------------------
 * UART init
 *
 * sysclk_hz — APB1 clock (USART2 is on APB1).
 *             For STM32F4 running at 168MHz with /4 APB1 prescaler → 42MHz
 *             For STM32F411 at 100MHz with /2 APB1 → 50MHz
 *             For Nucleo default clocks → often 16MHz (HSI)
 * ---------------------------------------------------------------------- */

void uart2_init(uint32_t sysclk_hz, uint32_t baud)
{
    /* Enable clocks */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;

    /* PA2 → AF7 (USART2 TX): MODER=10 (alternate), AFRL AF7 */
    GPIOA_MODER &= ~(3u << 4);   /* clear PA2 */
    GPIOA_MODER |=  (2u << 4);   /* PA2 = alternate function */
    GPIOA_AFRL  &= ~(0xFu << 8); /* clear AF for PA2 */
    GPIOA_AFRL  |=  (7u << 8);   /* PA2 = AF7 (USART2) */

    /* Baud rate: BRR = fCK / baud (OVER8=0 → 16x oversampling) */
    USART2_BRR = (uint32_t)(sysclk_hz / baud);

    /* Enable USART, transmitter only */
    USART2_CR1 = USART_CR1_UE | USART_CR1_TE;
}

/* -------------------------------------------------------------------------
 * Blocking single-character output
 * ---------------------------------------------------------------------- */

void uart2_putchar(char c)
{
    /* Wait for TX register empty */
    while (!(USART2_SR & USART_SR_TXE)) {}
    USART2_DR = (uint32_t)(unsigned char)c;
}

void uart2_puts(const char *s)
{
    while (*s) {
        uart2_putchar(*s++);
    }
}
