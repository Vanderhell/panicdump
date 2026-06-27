# panicdump Limitations

These are current scope limits, not bugs.

| Feature | Status |
| --- | --- |
| Cortex-M0/M0+ | Unsupported |
| Cortex-M4F / Cortex-M7 FPU builds | Rejected for now |
| RTOS awareness | Unsupported |
| Flash backend | Unsupported |
| Stack unwinding | Unsupported |
| Symbol resolution | Unsupported |
| Multiple dump slots | Unsupported |
| Encryption / compression | Unsupported |

## Fault capture

Fault capture is best effort. If the exception frame is outside the
configured retained RAM window, the dump records the safe registers and
marks the frame invalid instead of dereferencing the pointer.

## RAM bounds

`PANICDUMP_RAM_BASE` and `PANICDUMP_RAM_END` are required for Cortex-M
builds. The default CMake configuration supplies a common SRAM window, but
board-specific builds should override the values.

## Software trigger

`panicdump_trigger_tag()` hashes at most `PANICDUMP_REASON_TAG_MAX_LEN`
bytes. Use `panicdump_trigger_reason()` if you already have a numeric tag.
