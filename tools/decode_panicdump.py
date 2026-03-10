#!/usr/bin/env python3
"""
decode_panicdump.py — Offline decoder for panicdump v1 crash dumps.

Usage:
    decode_panicdump.py dump.bin
    decode_panicdump.py --hex dump.txt       # hex-framed UART capture
    decode_panicdump.py --json dump.bin      # JSON output
    decode_panicdump.py --verify dump.bin    # CRC check only

Format reference: docs/FORMAT.md
"""

import argparse
import struct
import sys
import json
import zlib
import re
from dataclasses import dataclass, asdict
from typing import Optional

# ---------------------------------------------------------------------------
# Constants — must match panicdump.h
# ---------------------------------------------------------------------------

MAGIC           = 0x50444331   # 'PDC1'
VERSION         = 1
STACK_SLICE     = 64

ARCH_NAMES = {
    0x0003: "Cortex-M3",
    0x0004: "Cortex-M4",
    0xFFFF: "Unknown/Host",
}

def fnv1a_32(s: str) -> int:
    """FNV-1a 32-bit hash — matches panicdump_trigger() encoding."""
    h = 0x811C9DC5
    for c in s.encode():
        h ^= c
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h

# Known reason_tag strings → their FNV-1a hash for reverse lookup
KNOWN_REASON_TAGS = {
    fnv1a_32(s): s for s in [
        "assert", "watchdog", "stack_overflow", "malloc_fail",
        "hw_error", "comms_timeout", "sensor_fail", "invalid_state",
    ]
}

FAULT_NAMES = {
    0: "Unknown",
    1: "HardFault",
    2: "MemManage",
    3: "BusFault",
    4: "UsageFault",
    5: "SW_Trigger",
}


# ---------------------------------------------------------------------------
# Struct layout — must match packed C structs in panicdump.h
# All little-endian, no padding (#pragma pack(push,1))
# ---------------------------------------------------------------------------

HEADER_FMT = "<IHHIIIIIII"   # magic, version, header_size, total_size, flags,
                              # arch_id, fault_reason, sequence, user_tag, crc32
HEADER_SIZE = struct.calcsize(HEADER_FMT)

REGS_FMT = "<" + "I" * 21   # 21 x uint32_t
REGS_SIZE = struct.calcsize(REGS_FMT)
REGS_FIELDS = [
    "r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr",
    "msp", "psp", "control", "primask", "basepri", "faultmask",
    "cfsr", "hfsr", "dfsr", "mmfar", "bfar", "afsr", "shcsr",
]

STACK_FMT = f"<II{STACK_SLICE}s"
STACK_SIZE = struct.calcsize(STACK_FMT)

DUMP_SIZE = HEADER_SIZE + REGS_SIZE + STACK_SIZE

# ---------------------------------------------------------------------------
# CRC32 — matches panicdump_crc32.h (ISO 3309, poly 0xEDB88320)
# zlib.crc32 uses the same polynomial.
# ---------------------------------------------------------------------------

def compute_crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF

def verify_crc(raw: bytes) -> tuple[bool, int, int]:
    """Returns (ok, stored_crc, computed_crc)."""
    # CRC field is at offset: magic(4)+version(2)+header_size(2)+total_size(4)+
    #                          flags(4)+arch_id(4)+fault_reason(4)+sequence(4)+user_tag(4) = 32
    CRC_OFFSET = 4 + 2 + 2 + 4 + 4 + 4 + 4 + 4 + 4   # = 32

    stored = struct.unpack_from("<I", raw, CRC_OFFSET)[0]

    # Zero out CRC field for computation
    mutable = bytearray(raw)
    struct.pack_into("<I", mutable, CRC_OFFSET, 0)
    computed = compute_crc32(bytes(mutable))

    return (stored == computed, stored, computed)

# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

@dataclass
class PanicDump:
    # header
    magic:        int
    version:      int
    header_size:  int
    total_size:   int
    flags:        int
    arch_id:      int
    fault_reason: int
    sequence:     int
    user_tag:     int
    crc32:        int
    crc_ok:       bool
    # registers
    regs:         dict
    # stack
    stack_sp:     int
    stack_bytes:  int
    stack_data:   bytes


def parse(raw: bytes) -> PanicDump:
    if len(raw) < DUMP_SIZE:
        raise ValueError(f"Dump too short: {len(raw)} bytes, expected {DUMP_SIZE}")

    # Header
    hdr = struct.unpack_from(HEADER_FMT, raw, 0)
    magic, version, header_size, total_size, flags, \
        arch_id, fault_reason, sequence, user_tag, crc32 = hdr

    crc_ok, _, _ = verify_crc(raw[:DUMP_SIZE])

    # Registers
    reg_vals = struct.unpack_from(REGS_FMT, raw, HEADER_SIZE)
    regs = dict(zip(REGS_FIELDS, reg_vals))

    # Stack slice
    stack_sp, stack_bytes, stack_data = struct.unpack_from(STACK_FMT, raw, HEADER_SIZE + REGS_SIZE)

    return PanicDump(
        magic=magic, version=version, header_size=header_size,
        total_size=total_size, flags=flags, arch_id=arch_id,
        fault_reason=fault_reason, sequence=sequence, user_tag=user_tag,
        crc32=crc32, crc_ok=crc_ok, regs=regs,
        stack_sp=stack_sp, stack_bytes=stack_bytes,
        stack_data=stack_data[:stack_bytes],
    )

# ---------------------------------------------------------------------------
# CFSR decoder — helps explain what caused the fault
# ---------------------------------------------------------------------------

def decode_cfsr(cfsr: int) -> list[str]:
    bits = []
    # UFSR (bits 31:16)
    ufsr = (cfsr >> 16) & 0xFFFF
    if ufsr & (1 << 9): bits.append("DIVBYZERO")
    if ufsr & (1 << 8): bits.append("UNALIGNED")
    if ufsr & (1 << 3): bits.append("NOCP (coprocessor fault)")
    if ufsr & (1 << 2): bits.append("INVPC (invalid PC load)")
    if ufsr & (1 << 1): bits.append("INVSTATE (invalid state)")
    if ufsr & (1 << 0): bits.append("UNDEFINSTR (undefined instruction)")
    # BFSR (bits 15:8)
    bfsr = (cfsr >> 8) & 0xFF
    if bfsr & (1 << 7): bits.append("BFARVALID (BFAR holds address)")
    if bfsr & (1 << 5): bits.append("LSPERR (FP lazy stack)")
    if bfsr & (1 << 4): bits.append("STKERR (stack push fault)")
    if bfsr & (1 << 3): bits.append("UNSTKERR (stack pop fault)")
    if bfsr & (1 << 2): bits.append("IMPRECISERR (imprecise data bus)")
    if bfsr & (1 << 1): bits.append("PRECISERR (precise data bus)")
    if bfsr & (1 << 0): bits.append("IBUSERR (instruction bus fault)")
    # MMFSR (bits 7:0)
    mmfsr = cfsr & 0xFF
    if mmfsr & (1 << 7): bits.append("MMARVALID (MMFAR holds address)")
    if mmfsr & (1 << 5): bits.append("MLSPERR (FP lazy stack MPU)")
    if mmfsr & (1 << 4): bits.append("MSTKERR (stack push MPU)")
    if mmfsr & (1 << 3): bits.append("MUNSTKERR (stack pop MPU)")
    if mmfsr & (1 << 1): bits.append("DACCVIOL (data access violation)")
    if mmfsr & (1 << 0): bits.append("IACCVIOL (instruction access violation)")
    return bits

# ---------------------------------------------------------------------------
# Report rendering
# ---------------------------------------------------------------------------

SEP = "─" * 60

def render_report(d: PanicDump) -> str:
    lines = []
    a = lines.append

    a(SEP)
    a("  panicdump crash report")
    a(SEP)

    # Basic info
    a(f"  version     : {d.version}")
    a(f"  arch        : {ARCH_NAMES.get(d.arch_id, f'0x{d.arch_id:04X}')}")
    a(f"  fault       : {FAULT_NAMES.get(d.fault_reason, f'0x{d.fault_reason:08X}')}")

    # user_tag: show raw value + reverse-lookup FNV reason_tag if SW_Trigger
    tag_str = f"{d.user_tag}  (0x{d.user_tag:08X})"
    if d.fault_reason == 5:  # SW_Trigger
        known = KNOWN_REASON_TAGS.get(d.user_tag)
        if known:
            tag_str += f'  → reason_tag="{known}"'
        else:
            tag_str += "  (unknown reason_tag — add to KNOWN_REASON_TAGS)"
    a(f"  user_tag    : {tag_str}")
    a(f"  sequence    : {d.sequence}")
    a(f"  crc         : {'OK ✓' if d.crc_ok else 'FAIL ✗  <-- dump may be corrupt!'}")
    a(f"  dump_size   : {d.total_size} bytes")

    a("")
    a("  [ Registers ]")
    a(f"  pc          : 0x{d.regs['pc']:08X}   ← fault address")
    a(f"  lr          : 0x{d.regs['lr']:08X}")
    a(f"  sp (msp)    : 0x{d.regs['msp']:08X}")
    a(f"  sp (psp)    : 0x{d.regs['psp']:08X}")
    a(f"  xpsr        : 0x{d.regs['xpsr']:08X}")
    a(f"  r0          : 0x{d.regs['r0']:08X}")
    a(f"  r1          : 0x{d.regs['r1']:08X}")
    a(f"  r2          : 0x{d.regs['r2']:08X}")
    a(f"  r3          : 0x{d.regs['r3']:08X}")
    a(f"  r12         : 0x{d.regs['r12']:08X}")
    a(f"  control     : 0x{d.regs['control']:08X}")
    a(f"  primask     : 0x{d.regs['primask']:08X}")
    a(f"  basepri     : 0x{d.regs['basepri']:08X}")
    a(f"  faultmask   : 0x{d.regs['faultmask']:08X}")

    a("")
    a("  [ Fault Status Registers ]")
    a(f"  cfsr        : 0x{d.regs['cfsr']:08X}")
    cfsr_bits = decode_cfsr(d.regs['cfsr'])
    for bit in cfsr_bits:
        a(f"                  → {bit}")
    a(f"  hfsr        : 0x{d.regs['hfsr']:08X}")
    if d.regs['hfsr'] & (1 << 30):
        a("                  → FORCED (escalated from lower fault)")
    if d.regs['hfsr'] & (1 << 1):
        a("                  → VECTTBL (vector table read fault)")
    a(f"  dfsr        : 0x{d.regs['dfsr']:08X}")
    a(f"  mmfar       : 0x{d.regs['mmfar']:08X}")
    a(f"  bfar        : 0x{d.regs['bfar']:08X}")
    a(f"  afsr        : 0x{d.regs['afsr']:08X}")
    a(f"  shcsr       : 0x{d.regs['shcsr']:08X}")

    a("")
    a(f"  [ Stack Slice @ 0x{d.stack_sp:08X} ({d.stack_bytes} bytes) ]")
    for i in range(0, len(d.stack_data), 16):
        chunk = d.stack_data[i:i+16]
        hex_part  = " ".join(f"{b:02X}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        a(f"  {d.stack_sp + i:08X}:  {hex_part:<47}  {ascii_part}")

    a("")
    a(SEP)

    return "\n".join(lines)

# ---------------------------------------------------------------------------
# Input loading
# ---------------------------------------------------------------------------

def load_bin(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()

def load_hex_framed(path: str) -> bytes:
    """Parse hex-framed UART export:
       === PANICDUMP BEGIN ===
       hex bytes ...
       === PANICDUMP END ===
    """
    with open(path) as f:
        content = f.read()

    m = re.search(
        r"=== PANICDUMP BEGIN ===\r?\n(.*?)=== PANICDUMP END ===",
        content, re.DOTALL
    )
    if not m:
        raise ValueError("Could not find PANICDUMP BEGIN/END markers in file")

    hex_str = re.sub(r"\s+", "", m.group(1))
    return bytes.fromhex(hex_str)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="panicdump v1 offline decoder",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("file", help="Dump file (.bin or hex-framed .txt)")
    parser.add_argument("--hex",    action="store_true", help="Input is hex-framed text (UART export)")
    parser.add_argument("--json",   action="store_true", help="Output JSON instead of human report")
    parser.add_argument("--verify", action="store_true", help="CRC check only, exit 0=ok 1=fail")
    args = parser.parse_args()

    try:
        raw = load_hex_framed(args.file) if args.hex else load_bin(args.file)
    except FileNotFoundError:
        print(f"error: file not found: {args.file}", file=sys.stderr)
        sys.exit(2)
    except Exception as e:
        print(f"error loading file: {e}", file=sys.stderr)
        sys.exit(2)

    try:
        dump = parse(raw)
    except Exception as e:
        print(f"error parsing dump: {e}", file=sys.stderr)
        sys.exit(2)

    # Sanity check magic
    if dump.magic != MAGIC:
        print(f"error: bad magic 0x{dump.magic:08X} (expected 0x{MAGIC:08X})", file=sys.stderr)
        sys.exit(2)

    if args.verify:
        ok_str = "CRC OK" if dump.crc_ok else "CRC FAIL"
        print(ok_str)
        sys.exit(0 if dump.crc_ok else 1)

    if args.json:
        out = asdict(dump)
        out["stack_data"] = dump.stack_data.hex()
        out["arch_name"] = ARCH_NAMES.get(dump.arch_id, "unknown")
        out["fault_name"] = FAULT_NAMES.get(dump.fault_reason, "unknown")
        out["cfsr_decoded"] = decode_cfsr(dump.regs["cfsr"])
        print(json.dumps(out, indent=2))
    else:
        print(render_report(dump))

    if not dump.crc_ok:
        sys.exit(1)

if __name__ == "__main__":
    main()
