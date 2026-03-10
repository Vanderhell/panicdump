/*
 * stm32f407vg.ld — Minimal linker script for STM32F407VG (1MB Flash, 192KB RAM)
 *
 * Key feature: .noinit section in retained RAM — NOT zeroed by startup code.
 * This is where panicdump stores crash dumps across warm resets.
 *
 * Adjust MEMORY regions for your specific device.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (rw) : ORIGIN = 0x10000000, LENGTH = 64K    /* Core-coupled, loses power on reset */
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* -----------------------------------------------------------------------
     * Flash sections
     * -------------------------------------------------------------------- */

    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } >FLASH

    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } >FLASH

    .ARM.extab :
    {
        *(.ARM.extab*)
        *(.gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /* Load addresses for data init */
    _sidata = LOADADDR(.data);

    /* -----------------------------------------------------------------------
     * RAM sections
     * -------------------------------------------------------------------- */

    .data :
    {
        _sdata = .;
        *(.data)
        *(.data.*)
        _edata = .;
    } >RAM AT>FLASH

    .bss :
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    } >RAM

    /*
     * .noinit — NOT zeroed by startup code.
     * panicdump uses this to store crash dumps across warm resets.
     *
     * IMPORTANT: startup code MUST NOT zero or initialise this region.
     * See startup_stm32f4.S for the startup implementation.
     *
     * Layout:
     *   .noinit is placed at the END of RAM so it doesn't interfere
     *   with normal heap/stack growth.
     */
    .noinit (NOLOAD) :
    {
        _snoinit = .;
        KEEP(*(.noinit))
        _enoinit = .;
    } >RAM

    /* Stack at the very end of RAM (grows downward) */
    ._stack (NOLOAD) :
    {
        . = ALIGN(8);
        _sstack = .;
        . = . + 0x2000;    /* 8KB stack */
        . = ALIGN(8);
        _estack = .;
    } >RAM

    /* -----------------------------------------------------------------------
     * Debug / discard
     * -------------------------------------------------------------------- */

    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}

/*
 * Symbols used by startup code
 */
PROVIDE(_estack   = ORIGIN(RAM) + LENGTH(RAM));
PROVIDE(_Min_Heap_Size  = 0x400);
PROVIDE(_Min_Stack_Size = 0x800);
