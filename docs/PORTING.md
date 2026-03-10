# panicdump Porting Guide

> **v1 scope:** Only Cortex-M3/M4 is supported. This document describes
> how a future port (Cortex-M0+, RISC-V, etc.) would be added.

---

## Port interface

A port must implement three things:

### 1. `panicdump_port_capture_sw_regs()`

Called by `panicdump_trigger()` for software-triggered panics.
Must fill `panicdump_regs_t` and `panicdump_stack_t` from the current
CPU context.

```c
void panicdump_port_capture_sw_regs(panicdump_regs_t  *out_regs,
                                    panicdump_stack_t *out_stack);
```

### 2. `panicdump_port_reset()`

Issue a system reset. Must not return.

```c
void panicdump_port_reset(void) __attribute__((noreturn));
```

### 3. Fault entry stubs (assembly)

Must call `panicdump_fault_handler_c(exc_frame, fault_reason, use_psp)`
with:
- `exc_frame` — pointer to the hardware-stacked exception frame
- `fault_reason` — one of the `PANICDUMP_FAULT_*` codes
- `use_psp` — 1 if PSP was active, 0 if MSP

### 4. `PANICDUMP_PORT_ARCH_ID`

Define in `panicdump_port.h` (via the `#elif` chain) to identify the
architecture in the dump header.

---

## Cortex-M0/M0+ considerations

Cortex-M0/M0+ is missing several registers present in the M3/M4 dump:

- No CFSR, HFSR, DFSR, MMFAR, BFAR, AFSR (no configurable fault system)
- No BASEPRI, FAULTMASK
- Only HardFault (no MemManage, BusFault, UsageFault)

A Cortex-M0 port would need a reduced `panicdump_regs_t` or must zero
the missing fields. A new arch_id (e.g. `0x0000` for M0) would signal
to the decoder that these fields are not meaningful.

---

## Register block compatibility

The current `panicdump_regs_t` stores all 21 Cortex-M3/M4 registers.
A port for a different arch should:

1. Define a new `arch_id`
2. Fill what is available
3. Zero fields that don't exist on the target
4. Document which fields are valid for the arch in `FORMAT.md`

The decoder uses `arch_id` to annotate which fields are meaningful.

---

## File naming convention

```
src/panicdump_port_<arch>.c
ports/<arch>/panicdump_fault_entry.S
ports/<arch>/panicdump_port.h         (optional arch-specific overrides)
```

---

## What does NOT need porting

- `panicdump.c` — core is fully portable (pure C, no arch-specific code)
- `panicdump_crc32.h` — pure C
- `decode_panicdump.py` — host tool, reads `arch_id` from dump
- `make_mock_dump.py` — host tool

Only `panicdump_port_<arch>.c` and the assembly entry stub are arch-specific.
