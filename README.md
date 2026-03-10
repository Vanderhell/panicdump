# panicdump

**Tiny crash dump library for bare-metal Cortex-M MCUs.**

When your MCU faults, panicdump captures the minimal crash context, stores it in
retained RAM, survives the reboot, and lets you decode it offline.

```
MCU faults
  └─► fault handler captures registers + stack slice
        └─► saves to .noinit retained RAM (survives reset)
              └─► on next boot: export via UART
                    └─► decode offline with Python tool
```

## Quick Start

```c
// main.c

#include "panicdump.h"

// 1. Provide a UART write callback
static void uart_putchar(char c) { /* ... your UART ... */ }

// 2. At boot — check for a previous crash dump
int main(void) {
    panicdump_boot_check_and_export(uart_putchar);  // prints dump, then clears it

    // 3. Tag your application state for post-mortem context
    panicdump_set_user_tag(STATE_INIT);

    // ... your app ...
    panicdump_set_user_tag(STATE_RUNNING);
}
```

```asm
; In your vector table — redirect faults to panicdump handlers
HardFault_Handler  = panicdump_HardFault_Handler
MemManage_Handler  = panicdump_MemManage_Handler
BusFault_Handler   = panicdump_BusFault_Handler
UsageFault_Handler = panicdump_UsageFault_Handler
```

Capture UART output to a file, then decode:

```sh
python3 tools/decode_panicdump.py --hex uart_capture.txt
```

Output:

```
────────────────────────────────────────────────────────────
  panicdump crash report
────────────────────────────────────────────────────────────
  version     : 1
  arch        : Cortex-M4
  fault       : HardFault
  user_tag    : 42  (0x0000002A)
  crc         : OK ✓

  [ Registers ]
  pc          : 0x08001234   ← fault address
  lr          : 0xFFFFFFF9
  cfsr        : 0x00008200
                  → BFARVALID (BFAR holds address)
                  → PRECISERR (precise data bus)
  bfar        : 0x20010004
  ...
```

## What it does

- Captures fault type, all CPU registers, SCB fault status registers
- Saves a 64-byte stack slice at the faulting SP
- Stores a user-defined tag (your last known application state)
- Computes CRC-32 over the entire dump for integrity verification
- Survives warm reset via `.noinit` retained RAM
- Exports via UART as hex-framed text
- Offline Python decoder with CFSR bit decoding and JSON output

## What it does NOT do

- No stack unwinding
- No symbol resolution
- No RTOS support
- No flash storage backend
- No multi-dump slots
- No Cortex-M0/M0+, ESP32, RISC-V (v1)

These are intentional scope limits, not missing features. See `docs/LIMITATIONS.md`.

## API

```c
// Check for a valid dump in retained RAM
bool panicdump_has_valid(void);

// Get read-only pointer to dump (NULL if none)
const panicdump_dump_t *panicdump_get(void);

// Export as UART hex-frame (calls write_char per character)
void panicdump_export_hex(void (*write_char)(char c));

// Erase dump
void panicdump_clear(void);

// Set user-defined tag (call at key app milestones)
void panicdump_set_user_tag(uint32_t tag);

// Convenience: export + clear at boot
void panicdump_boot_check_and_export(void (*write_char)(char c));

// Software panic (saves dump, resets MCU)
void panicdump_trigger(const char *reason_tag);
```

## Decoder

```sh
# From binary file
python3 tools/decode_panicdump.py dump.bin

# From UART hex-framed capture
python3 tools/decode_panicdump.py --hex uart.txt

# JSON output
python3 tools/decode_panicdump.py --json dump.bin

# CRC check only (exit 0=ok, 1=fail)
python3 tools/decode_panicdump.py --verify dump.bin
```

## Mock Generator (no hardware needed)

```sh
# Generate all test vectors
python3 tools/make_mock_dump.py --all

# Generate specific fault type
python3 tools/make_mock_dump.py --fault memmanage --tag 0x1337 -o my_dump.bin

# With hex-framed export
python3 tools/make_mock_dump.py --fault hardfault --hex-export
```

## Build Integration

Add to your build:

```
src/panicdump.c
src/panicdump_port_cortexm.c
ports/cortexm/panicdump_fault_entry.S
```

Include path: `include/`

Required in linker script:

```ld
/* Retained RAM — not zeroed by startup code */
.noinit (NOLOAD) :
{
    KEEP(*(.noinit))
} >RAM
```

Make sure your startup code does **not** zero-initialise `.noinit`.

## Footprint

Measured on Cortex-M4, `-Os`, arm-none-eabi-gcc:

| Metric | Value |
|--------|-------|
| Retained RAM (`.noinit`) | **196 bytes** — 192B dump slot + 4B user tag |
| Flash (code) | ~600–900 bytes (depends on optimisation level) |
| Stack usage in fault handler | ~64 bytes (no dynamic allocation) |
| Dependencies | None — no stdlib, no CMSIS required |

The 192-byte dump format breakdown:

```
 36B  header   (magic, version, arch, fault reason, CRC, ...)
 84B  registers  (21 × uint32_t: r0–r12, SP, LR, PC, xPSR, SCB regs)
 72B  stack slice  (4B SP value + 4B count + 64B raw stack data)
────
192B  total
```

## Running Tests

```sh
PYTHONPATH=tools python3 -m pytest tests/ -v
```

**50 tests, no hardware required.** Coverage:

| Test file | What it covers |
|-----------|---------------|
| `test_format.py` | Struct sizes, field parsing, CRC computation, partial dump detection, hex-frame round-trip |
| `test_decoder_cli.py` | Full CLI black-box: all fault types, JSON output, `--verify` mode, hex-framed input, bad magic, truncated file, empty file |

Note: Assembly entry stubs and inline asm register capture require real Cortex-M hardware to test. The mock generator provides golden vectors for the C/Python layers.

## Format

See `docs/FORMAT.md` for the binary format specification.
Fixed size: **192 bytes**, little-endian, CRC-32 protected.

## v1 Support Matrix

| Target      | Status      |
|-------------|-------------|
| Cortex-M3   | Supported   |
| Cortex-M4   | Supported   |
| Cortex-M4F  | Supported   |
| Cortex-M0+  | Not in v1   |
| RISC-V      | Not in v1   |
| ESP32/Xtensa| Not in v1   |

## License

MIT
