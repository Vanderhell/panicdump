#!/usr/bin/env python3
"""
decode_panicdump.py - Offline decoder for panicdump crash dumps.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zlib
from dataclasses import asdict, dataclass

MAGIC = 0x50444331
VERSION = 1
STACK_SLICE = 64
HEADER_SIZE = 36
TOTAL_SIZE = 192
VALID_FLAGS_MASK = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)
ARCH_NAMES = {
    0x0003: "Cortex-M3",
    0x0004: "Cortex-M4",
}
BEGIN_MARKER = "=== PANICDUMP BEGIN ==="
END_MARKER = "=== PANICDUMP END ==="


def fnv1a_32(s: str) -> int:
    h = 0x811C9DC5
    for c in s.encode():
        h ^= c
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


KNOWN_REASON_TAGS = {
    fnv1a_32(s): s
    for s in [
        "assert",
        "watchdog",
        "stack_overflow",
        "malloc_fail",
        "hw_error",
        "comms_timeout",
        "sensor_fail",
        "invalid_state",
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

HEADER_FMT = "<IHHIIIIIII"
HEADER_STRUCT_SIZE = struct.calcsize(HEADER_FMT)
REGS_FMT = "<" + "I" * 21
REGS_SIZE = struct.calcsize(REGS_FMT)
REGS_FIELDS = [
    "r0",
    "r1",
    "r2",
    "r3",
    "r12",
    "lr",
    "pc",
    "xpsr",
    "msp",
    "psp",
    "control",
    "primask",
    "basepri",
    "faultmask",
    "cfsr",
    "hfsr",
    "dfsr",
    "mmfar",
    "bfar",
    "afsr",
    "shcsr",
]
STACK_FMT = f"<II{STACK_SLICE}s"
STACK_SIZE = struct.calcsize(STACK_FMT)
DUMP_SIZE = HEADER_STRUCT_SIZE + REGS_SIZE + STACK_SIZE


def compute_crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def verify_crc(raw: bytes) -> tuple[bool, int, int]:
    crc_offset = 32
    stored = struct.unpack_from("<I", raw, crc_offset)[0]
    mutable = bytearray(raw)
    struct.pack_into("<I", mutable, crc_offset, 0)
    computed = compute_crc32(bytes(mutable))
    return stored == computed, stored, computed


@dataclass
class PanicDump:
    magic: int
    version: int
    header_size: int
    total_size: int
    flags: int
    arch_id: int
    fault_reason: int
    sequence: int
    user_tag: int
    crc32: int
    crc_ok: bool
    regs: dict
    stack_sp: int
    stack_bytes: int
    stack_data: bytes


def parse(raw: bytes) -> PanicDump:
    if len(raw) != DUMP_SIZE:
        raise ValueError(f"Dump must be exactly {DUMP_SIZE} bytes, got {len(raw)}")

    hdr = struct.unpack_from(HEADER_FMT, raw, 0)
    magic, version, header_size, total_size, flags, arch_id, fault_reason, sequence, user_tag, crc32 = hdr
    if magic != MAGIC:
        raise ValueError(f"Bad magic: 0x{magic:08X} (expected 0x{MAGIC:08X})")

    reg_vals = struct.unpack_from(REGS_FMT, raw, HEADER_SIZE)
    regs = dict(zip(REGS_FIELDS, reg_vals))
    stack_sp, stack_bytes, stack_data = struct.unpack_from(STACK_FMT, raw, HEADER_SIZE + REGS_SIZE)

    if version != VERSION:
        raise ValueError(f"Bad version: {version} (expected {VERSION})")
    if header_size != HEADER_SIZE:
        raise ValueError(f"Bad header_size: {header_size} (expected {HEADER_SIZE})")
    if total_size != TOTAL_SIZE:
        raise ValueError(f"Bad total_size: {total_size} (expected {TOTAL_SIZE})")
    if flags & ~VALID_FLAGS_MASK:
        raise ValueError(f"Bad flags: 0x{flags:08X}")
    if arch_id not in ARCH_NAMES:
        raise ValueError(f"Bad arch_id: 0x{arch_id:08X}")
    if fault_reason not in FAULT_NAMES:
        raise ValueError(f"Bad fault_reason: 0x{fault_reason:08X}")
    if stack_bytes > STACK_SLICE:
        raise ValueError(f"Bad stack_bytes: {stack_bytes}")
    if stack_bytes not in (0, STACK_SLICE):
        raise ValueError(f"Unsupported stack_bytes length: {stack_bytes}")

    crc_ok, _, _ = verify_crc(raw)
    return PanicDump(
        magic=magic,
        version=version,
        header_size=header_size,
        total_size=total_size,
        flags=flags,
        arch_id=arch_id,
        fault_reason=fault_reason,
        sequence=sequence,
        user_tag=user_tag,
        crc32=crc32,
        crc_ok=crc_ok,
        regs=regs,
        stack_sp=stack_sp,
        stack_bytes=stack_bytes,
        stack_data=stack_data[:stack_bytes],
    )


_UFSR_BITS = [
    (9, "DIVBYZERO"),
    (8, "UNALIGNED"),
    (3, "NOCP (coprocessor fault)"),
    (2, "INVPC (invalid PC load)"),
    (1, "INVSTATE (invalid state)"),
    (0, "UNDEFINSTR (undefined instruction)"),
]
_BFSR_BITS = [
    (7, "BFARVALID (BFAR holds address)"),
    (5, "LSPERR (FP lazy stack)"),
    (4, "STKERR (stack push fault)"),
    (3, "UNSTKERR (stack pop fault)"),
    (2, "IMPRECISERR (imprecise data bus)"),
    (1, "PRECISERR (precise data bus)"),
    (0, "IBUSERR (instruction bus fault)"),
]
_MMFSR_BITS = [
    (7, "MMARVALID (MMFAR holds address)"),
    (5, "MLSPERR (FP lazy stack MPU)"),
    (4, "MSTKERR (stack push MPU)"),
    (3, "MUNSTKERR (stack pop MPU)"),
    (1, "DACCVIOL (data access violation)"),
    (0, "IACCVIOL (instruction access violation)"),
]


def decode_cfsr(cfsr: int) -> list[str]:
    ufsr = (cfsr >> 16) & 0xFFFF
    bfsr = (cfsr >> 8) & 0xFF
    mmfsr = cfsr & 0xFF
    result = []
    for bit, name in _UFSR_BITS:
        if ufsr & (1 << bit):
            result.append(name)
    for bit, name in _BFSR_BITS:
        if bfsr & (1 << bit):
            result.append(name)
    for bit, name in _MMFSR_BITS:
        if mmfsr & (1 << bit):
            result.append(name)
    return result


def render_report(d: PanicDump) -> str:
    lines = []
    a = lines.append
    sep = "-" * 60
    a(sep)
    a("  panicdump crash report")
    a(sep)
    a(f"  version     : {d.version}")
    a(f"  arch        : {ARCH_NAMES.get(d.arch_id, f'0x{d.arch_id:08X}')}")
    a(f"  fault       : {FAULT_NAMES.get(d.fault_reason, f'0x{d.fault_reason:08X}')}")

    tag_str = f"{d.user_tag}  (0x{d.user_tag:08X})"
    if d.fault_reason == 5:
        known = KNOWN_REASON_TAGS.get(d.user_tag)
        if known:
            tag_str += f'  -> reason_tag="{known}"'
        else:
            tag_str += "  (unknown reason_tag - add to KNOWN_REASON_TAGS)"
    a(f"  user_tag    : {tag_str}")
    a(f"  exc_return  : 0x{d.sequence:08X}")
    a(f"  crc         : {'OK' if d.crc_ok else 'FAIL <-- dump may be corrupt!'}")
    a(f"  dump_size   : {d.total_size} bytes")

    a("")
    a("  [ Registers ]")
    a(f"  pc          : 0x{d.regs['pc']:08X}   <- fault address")
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
    for bit in decode_cfsr(d.regs["cfsr"]):
        a(f"                  -> {bit}")
    a(f"  hfsr        : 0x{d.regs['hfsr']:08X}")
    if d.regs["hfsr"] & (1 << 30):
        a("                  -> FORCED (escalated from lower fault)")
    if d.regs["hfsr"] & (1 << 1):
        a("                  -> VECTTBL (vector table read fault)")
    a(f"  dfsr        : 0x{d.regs['dfsr']:08X}")
    a(f"  mmfar       : 0x{d.regs['mmfar']:08X}")
    a(f"  bfar        : 0x{d.regs['bfar']:08X}")
    a(f"  afsr        : 0x{d.regs['afsr']:08X}")
    a(f"  shcsr       : 0x{d.regs['shcsr']:08X}")

    a("")
    a(f"  [ Stack Slice @ 0x{d.stack_sp:08X} ({d.stack_bytes} bytes) ]")
    for i in range(0, len(d.stack_data), 16):
        chunk = d.stack_data[i:i + 16]
        hex_part = " ".join(f"{b:02X}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        a(f"  {d.stack_sp + i:08X}:  {hex_part:<47}  {ascii_part}")

    a("")
    a(sep)
    return "\n".join(lines)


def load_bin(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def load_hex_framed(path: str) -> bytes:
    with open(path) as f:
        content = f.read()

    if content.count(BEGIN_MARKER) != 1 or content.count(END_MARKER) != 1:
        raise ValueError("Expected exactly one panicdump hex frame")

    start = content.find(BEGIN_MARKER)
    end = content.find(END_MARKER)
    if start < 0 or end < 0 or end < start:
        raise ValueError("Could not find panicdump hex frame markers")

    prefix = content[:start].strip()
    suffix = content[end + len(END_MARKER):].strip()
    if prefix or suffix:
        raise ValueError("Trailing or leading data outside panicdump frame")

    body = content[start + len(BEGIN_MARKER):end]
    hex_str = re.sub(r"\s+", "", body)
    if len(hex_str) != DUMP_SIZE * 2:
        raise ValueError(f"Expected {DUMP_SIZE * 2} hex characters, got {len(hex_str)}")
    if not re.fullmatch(r"[0-9a-fA-F]+", hex_str):
        raise ValueError("Non-hex characters in panicdump frame")
    return bytes.fromhex(hex_str)


def main() -> None:
    parser = argparse.ArgumentParser(description="panicdump decoder")
    parser.add_argument("file", help="Dump file (.bin or hex-framed .txt)")
    parser.add_argument("--hex", action="store_true", help="Input is hex-framed text")
    parser.add_argument("--json", action="store_true", help="Output JSON instead of report")
    parser.add_argument("--verify", action="store_true", help="CRC check only")
    parser.add_argument("--forensic", action="store_true", help="Allow JSON for CRC-failed dumps")
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

    if args.verify:
        print("CRC OK" if dump.crc_ok else "CRC FAIL")
        sys.exit(0 if dump.crc_ok else 1)

    if args.json:
        if not dump.crc_ok and not args.forensic:
            print("error: refusing JSON output for CRC-failed dump", file=sys.stderr)
            sys.exit(1)
        out = asdict(dump)
        out["stack_data"] = dump.stack_data.hex()
        out["arch_name"] = ARCH_NAMES.get(dump.arch_id, "unknown")
        out["fault_name"] = FAULT_NAMES.get(dump.fault_reason, "unknown")
        out["cfsr_decoded"] = decode_cfsr(dump.regs["cfsr"])
        out["exc_return"] = dump.sequence
        print(json.dumps(out, indent=2))
    else:
        print(render_report(dump))

    if not dump.crc_ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
