# panicdump v1 — Known Limitations

## Scope Limitations (by design)

These are **not bugs** — they are explicit v1 scope decisions.

| Feature              | Status         | Rationale                              |
|----------------------|----------------|----------------------------------------|
| Cortex-M0/M0+        | Not supported  | No CFSR/BFAR/MMFAR on M0              |
| RISC-V, Xtensa       | Not supported  | Different fault model                  |
| RTOS awareness       | Not supported  | Out of scope for bare-metal library    |
| Flash backend        | Not supported  | Complexity, wear, platform-specific    |
| Stack unwinding      | Not supported  | Unreliable without frame pointers      |
| Symbol resolution    | Not supported  | Requires ELF + toolchain integration   |
| Multiple dump slots  | Not supported  | Single slot in .noinit only            |
| Encryption           | Not supported  | —                                      |
| Compression          | Not supported  | —                                      |

## Fault Capture — Best-Effort Guarantee

Dump capture is **best-effort**. The following scenarios may prevent a
complete or valid dump from being saved:

- **Double fault / fault in fault handler**: If the HardFault handler itself
  faults (e.g. due to severe stack corruption), no dump will be committed.
  The magic field will remain 0, so the next boot will see no valid dump.

- **Stack overflow into fault handler**: If the main stack has overflowed to
  the point that the fault handler cannot execute, behaviour is undefined.
  Mitigation: configure sufficient stack guard regions if MPU is available.

- **Corrupted .noinit region**: If retained RAM is not stable across resets
  on a given board (e.g. deep power-down resets the entire RAM), the dump
  will not survive reboot.

- **Interrupted write sequence**: Power loss between the CRC write and the
  final magic write will leave an invalid dump (magic = 0). This is
  intentional — a partial dump is not reported as valid.

These limitations are documented here and **not silently handled**.
The CRC and magic-last protocol protect against silent corruption;
they do not protect against catastrophic hardware failure.

## Stack Slice

The 64-byte stack slice is captured with bounds checking against
`PANICDUMP_RAM_BASE` / `PANICDUMP_RAM_END` (defined in `panicdump_port.h`).
If the captured SP is outside this window, `stack_bytes` is set to 0 and
no memory is dereferenced — the SP value is still recorded for diagnostics.

**You must configure these bounds correctly for your MCU.** See below.

## RAM Bounds Configuration — Required

`PANICDUMP_RAM_BASE` and `PANICDUMP_RAM_END` define the valid SRAM window
for stack-slice capture. The defaults in `panicdump_port.h` cover STM32F4
SRAM1 (128KB at 0x20000000). **You must override these for other MCUs.**

Set them before including `panicdump_port.h`, or define them in your build
system:

```c
// In your board config header, or as compiler flags:
#define PANICDUMP_RAM_BASE  0x20000000UL   // start of your SRAM
#define PANICDUMP_RAM_END   0x20005000UL   // end   of your SRAM (20KB = STM32F103)
```

Or in your Makefile/CMake:
```makefile
CFLAGS += -DPANICDUMP_RAM_BASE=0x20000000 -DPANICDUMP_RAM_END=0x20005000
```

Common MCU SRAM regions:

| MCU family        | RAM_BASE     | RAM_END      | Size  |
|-------------------|--------------|--------------|-------|
| STM32F4xx         | 0x20000000   | 0x20020000   | 128KB |
| STM32F1xx (20KB)  | 0x20000000   | 0x20005000   | 20KB  |
| STM32L4xx         | 0x20000000   | 0x20014000   | 80KB  |
| nRF52840          | 0x20000000   | 0x20040000   | 256KB |

If wrong: SP inside RAM but outside window → 0 bytes captured (safe).
If wrong other direction: SP outside RAM but inside window → potential nested fault.
**When in doubt, use a smaller/tighter window.**

## FNV-1a Hash Collisions in `panicdump_trigger()`

`reason_tag` strings are encoded as FNV-1a 32-bit hashes into `user_tag`.
A 32-bit hash can collide — two different strings could produce the same value
(probability ~1 in 4 billion per pair). In practice, with a small set of known
reason tags this is not a concern.

The decoder maintains a lookup table of known strings. If you add custom
reason tags, add them to `KNOWN_REASON_TAGS` in `decode_panicdump.py`:

```python
KNOWN_REASON_TAGS = {
    fnv1a_32(s): s for s in [
        "assert", "watchdog", "stack_overflow",
        "your_custom_tag",   # ← add here
    ]
}
```

If a collision were to occur, the decoder would display the wrong string label
— the raw hash value is always shown alongside, so the dump data itself is
unaffected.

## Single Dump Slot — Overwrite Without Warning

panicdump v1 has a single dump slot in `.noinit`. If a second crash occurs
before the first dump is fetched and cleared, the second dump **silently
overwrites the first**.

This is a known v1 limitation. Implications:

- Two crashes in rapid succession → only the second is preserved
- A crash during the boot-time export phase → the export dump is overwritten

Mitigation in v1: call `panicdump_boot_check_and_export()` as early as
possible in `main()`, before any code that might itself crash.

Multiple dump slots are a v2 feature candidate.

## `const panicdump_dump_t*` is Convention, Not Hardware Enforcement

`panicdump_get()` returns `const panicdump_dump_t*`. This prevents accidental
modification in normal usage, but C allows casting away `const`. If you do
this and modify the dump, you will corrupt it — the CRC will no longer match
and `panicdump_has_valid()` will return false.

Do not cast away `const` on the returned pointer.

## Retained RAM (.noinit)

The library assumes the `.noinit` section is:
- Not zeroed by the startup code
- Preserved across warm resets (not power-on resets)

This must be configured correctly in the linker script. See `examples/` for
reference linker script fragments.
