# panicdump v1 Binary Format

## Overview

A panicdump v1 dump is a fixed-size, little-endian binary blob of **192 bytes**.
All structs are packed (`#pragma pack(push,1)`) — no padding.

## Layout

```
Offset  Size  Field            Description
──────────────────────────────────────────────────────────────
 0      4     magic            0x50444331 ('PDC1') — written LAST
 4      2     version          Always 1
 6      2     header_size      Offset to register block = 36
 8      4     total_size       Total dump size = 192
12      4     flags            Reserved, always 0
16      4     arch_id          0x0003=Cortex-M3, 0x0004=Cortex-M4
20      4     fault_reason     See fault reason codes below
24      4     sequence         Reserved, always 0 in v1
28      4     user_tag         Last value from panicdump_set_user_tag()
32      4     crc32            CRC-32 (ISO 3309) over entire dump with this field = 0
──────────────────────────────────────────────────────────────
36     84     Register block   See below
──────────────────────────────────────────────────────────────
120    72     Stack slice      See below
──────────────────────────────────────────────────────────────
Total: 192 bytes
```

## Register Block (offset 36, 84 bytes)

21 × uint32_t, little-endian:

```
 r0, r1, r2, r3, r12, lr, pc, xpsr          (from hardware exception frame)
 msp, psp, control, primask, basepri,
 faultmask                                   (special registers)
 cfsr, hfsr, dfsr, mmfar, bfar, afsr, shcsr  (SCB fault status registers)
```

## Stack Slice (offset 120, 72 bytes)

```
Offset  Size  Field          Description
 0      4     captured_sp    Stack pointer value at capture time
 4      4     stack_bytes    Always 64 in v1
 8      64    data           Raw bytes from captured_sp onwards
```

## Fault Reason Codes

| Code | Name         |
|------|--------------|
|  0   | Unknown      |
|  1   | HardFault    |
|  2   | MemManage    |
|  3   | BusFault     |
|  4   | UsageFault   |
|  5   | SW_Trigger   |

## Validity Rules

A dump is **valid** if and only if:
1. `magic == 0x50444331`
2. `version == 1`
3. `total_size == 192`
4. CRC-32 of entire dump (with `crc32` field zeroed) matches `crc32` field

## Write Protocol

The write order is critical to prevent partial dumps appearing valid:

1. Set `magic = 0` (invalidate)
2. Write all fields except `magic` and `crc32`
3. Compute CRC-32 over entire struct with `crc32 = 0`
4. Write `crc32`
5. Write `magic = 0x50444331` (commit — last write)

## UART Hex-Framed Export

```
=== PANICDUMP BEGIN ===\r\n
<hex bytes, 64 hex chars per line>\r\n
...
=== PANICDUMP END ===\r\n
```

`decode_panicdump.py --hex` parses this format directly.

## Versioning

- `version` field allows future format changes.
- `header_size` allows the decoder to skip unknown header fields.
- v1 decoder must reject dumps where `version != 1`.
- Future versions should maintain backward-compatible CRC scheme.
