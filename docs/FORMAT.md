# panicdump Wire Format

panicdump stores a fixed 192-byte little-endian wire image.

## Layout

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 4 | magic | `0x50444331`, written last |
| 4 | 2 | version | `1` |
| 6 | 2 | header_size | `36` |
| 8 | 4 | total_size | `192` |
| 12 | 4 | flags | Bitfield |
| 16 | 4 | arch_id | `0x0003` or `0x0004` |
| 20 | 4 | fault_reason | `0..5` |
| 24 | 4 | sequence | EXC_RETURN for fault captures, `0` for software trigger |
| 28 | 4 | user_tag | Last application tag |
| 32 | 4 | crc32 | CRC-32 over the whole image with this field treated as zero |
| 36 | 84 | regs | 21 little-endian `uint32_t` values |
| 120 | 72 | stack | `captured_sp`, `stack_bytes`, and 64 bytes of data |

## Flags

- bit 0: invalid frame
- bit 1: PSP active
- bit 2: stack slice valid
- bit 3: exception frame valid

## Validity

A dump is valid when:

1. `magic == 0x50444331`
2. `version == 1`
3. `header_size == 36`
4. `total_size == 192`
5. `arch_id` is supported
6. `fault_reason` is supported
7. `stack_bytes` is `0` or `64`
8. CRC-32 matches

## Commit order

The commit marker is published after the payload and CRC are written:

1. Clear the magic field
2. Write the header, registers, and stack slice
3. Compute the CRC with the CRC field treated as zero
4. Write the CRC
5. Publish the final magic with one aligned store

The Python decoder and C validator use the same CRC definition.
