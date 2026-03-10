# panicdump Integration Guide

## Prerequisites

- Cortex-M3 or M4 target (bare-metal, no RTOS)
- arm-none-eabi-gcc toolchain
- Linker script you control

---

## Step 1: Add source files to your build

```
panicdump/src/panicdump.c
panicdump/src/panicdump_port_cortexm.c
panicdump/ports/cortexm/panicdump_fault_entry.S
```

Include path: `panicdump/include/`

**CMake:**
```cmake
add_subdirectory(panicdump)
target_link_libraries(your_target PRIVATE panicdump)
```

**Makefile:**
```makefile
SRCS += panicdump/src/panicdump.c
SRCS += panicdump/src/panicdump_port_cortexm.c
SRCS += panicdump/ports/cortexm/panicdump_fault_entry.S
CFLAGS += -Ipanicdump/include
```

---

## Step 2: Add `.noinit` section to your linker script

```ld
.noinit (NOLOAD) :
{
    _snoinit = .;
    KEEP(*(.noinit))
    _enoinit = .;
} >RAM
```

Place it **after** `.bss` and **before** the stack. The `(NOLOAD)` attribute
tells the linker this section has no initialisation data in Flash.

**Critical:** Your startup code must **not** zero or initialise this section.

## Step 2b: Configure RAM bounds for your MCU

`panicdump_port.h` ships with defaults for STM32F4 SRAM1 (128KB).
**Override these for your specific device** — wrong values mean the stack
slice either captures nothing (safe but useless) or risks a nested fault.

```c
// In your board config header, included before panicdump_port.h:
#define PANICDUMP_RAM_BASE  0x20000000UL
#define PANICDUMP_RAM_END   0x20005000UL   // 20KB for STM32F103
```

Or as compiler flags in your build system:
```makefile
CFLAGS += -DPANICDUMP_RAM_BASE=0x20000000 -DPANICDUMP_RAM_END=0x20005000
```

See `docs/LIMITATIONS.md` for a table of common MCU SRAM regions.

---

## Step 3: Route fault handlers in your vector table

In your startup file or `vectors.c`:

```c
extern void panicdump_HardFault_Handler(void);
extern void panicdump_MemManage_Handler(void);
extern void panicdump_BusFault_Handler(void);
extern void panicdump_UsageFault_Handler(void);
```

In the vector table array:
```c
[3]  = panicdump_HardFault_Handler,    // offset 0x0C
[4]  = panicdump_MemManage_Handler,    // offset 0x10
[5]  = panicdump_BusFault_Handler,     // offset 0x14
[6]  = panicdump_UsageFault_Handler,   // offset 0x18
```

If you use a `.S` startup file, replace the handler symbols directly:
```asm
.word panicdump_HardFault_Handler
.word panicdump_MemManage_Handler
.word panicdump_BusFault_Handler
.word panicdump_UsageFault_Handler
```

**Make sure UsageFault and MemManage faults are enabled** in SHCSR
(they are disabled by default on Cortex-M):
```c
// Enable UsageFault, BusFault, MemManage faults (SCB->SHCSR)
*((volatile uint32_t *)0xE000ED24) |= (1u << 18) | (1u << 17) | (1u << 16);
```

---

## Step 4: Check for dump at boot

Place this as early as possible in `main()`, before any code that might crash:

```c
#include "panicdump.h"

int main(void)
{
    your_uart_init();

    // Check for crash dump from previous reset
    panicdump_boot_check_and_export(your_uart_putchar);

    // ... rest of your application
}
```

`panicdump_boot_check_and_export()` does nothing if no valid dump is present.
If a dump exists, it exports it via your UART callback and then clears it.

---

## Step 5: Set user tags

Call `panicdump_set_user_tag()` at key application milestones so you know
where the MCU was when it crashed:

```c
// Define meaningful tags for your system
#define TAG_BOOT        0x01
#define TAG_SENSOR_INIT 0x10
#define TAG_MAIN_LOOP   0x20
#define TAG_COMMS_TX    0x30

int main(void) {
    panicdump_set_user_tag(TAG_BOOT);
    sensor_init();

    panicdump_set_user_tag(TAG_SENSOR_INIT);
    ...

    while (1) {
        panicdump_set_user_tag(TAG_MAIN_LOOP);
        ...
        panicdump_set_user_tag(TAG_COMMS_TX);
        transmit_data();
    }
}
```

The last set tag appears in the crash dump as `user_tag`. When you see the
decoded dump, `user_tag: 0x30` tells you the crash happened during `TAG_COMMS_TX`.

---

## Step 6: Decode the dump

Capture UART output to a file (e.g. with `minicom -C uart.log` or PuTTY log),
then decode:

```sh
# Hex-framed UART output
python3 tools/decode_panicdump.py --hex uart.log

# Raw binary (if you save dump to file over SWD/J-Link)
python3 tools/decode_panicdump.py dump.bin

# JSON (for scripting)
python3 tools/decode_panicdump.py --json dump.bin
```

---

## Optional: Software panic

For `assert()` failures or watchdog handlers:

```c
void assert_failed(const char *file, int line) {
    (void)file; (void)line;
    panicdump_trigger("assert");  // saves dump, resets — no return
}
```

---

## Optional: Halt instead of reset (debug builds)

If you want to attach a debugger instead of resetting after a fault:

```c
// CMake
target_compile_definitions(your_target PRIVATE PANICDUMP_HALT_ON_FAULT)

// Makefile
CFLAGS += -DPANICDUMP_HALT_ON_FAULT
```

With this flag, panicdump saves the dump and then executes `BKPT #0` +
infinite loop instead of resetting. The debugger will break here, and you
can inspect the dump in retained RAM.

---

## Startup code checklist

- [ ] `.noinit` section present in linker script
- [ ] `.noinit` is **not** zeroed in startup code
- [ ] `PANICDUMP_RAM_BASE` and `PANICDUMP_RAM_END` configured for your MCU
- [ ] All four fault handlers point to panicdump
- [ ] MemManage, BusFault, UsageFault enabled in `SCB->SHCSR` (if needed)
- [ ] `panicdump_boot_check_and_export()` called early in `main()`
- [ ] User tags set at key application milestones

---

## Verifying the setup without real hardware

```sh
cd panicdump/tools

# Generate a mock HardFault dump
python3 make_mock_dump.py --fault hardfault --hex-export -o test.bin

# Decode it
python3 decode_panicdump.py --hex test.txt
```

This verifies the decoder works before you even touch hardware.
