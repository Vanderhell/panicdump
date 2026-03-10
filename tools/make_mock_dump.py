#!/usr/bin/env python3
"""
make_mock_dump.py — Generate synthetic panicdump v1 binary files.

Used for:
  - Testing decode_panicdump.py without real hardware
  - Generating golden test vectors
  - CI/CD verification

Usage:
    make_mock_dump.py                         # valid HardFault dump
    make_mock_dump.py --fault memmanage
    make_mock_dump.py --bad-crc               # corrupt CRC
    make_mock_dump.py --partial               # incomplete dump (no magic)
    make_mock_dump.py --all                   # generate all variants
    make_mock_dump.py -o my_dump.bin
"""

import argparse
import struct
import zlib
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants — must stay in sync with panicdump.h
# ---------------------------------------------------------------------------

MAGIC         = 0x50444331
VERSION       = 1
STACK_SLICE   = 64

ARCH_CORTEXM4 = 0x0004

FAULT_CODES = {
    "unknown":    0,
    "hardfault":  1,
    "memmanage":  2,
    "busfault":   3,
    "usagefault": 4,
    "sw_trigger": 5,
}

HEADER_FMT = "<IHHIIIIIII"
REGS_FMT   = "<" + "I" * 21
STACK_FMT  = f"<II{STACK_SLICE}s"

DUMP_SIZE = struct.calcsize(HEADER_FMT) + struct.calcsize(REGS_FMT) + struct.calcsize(STACK_FMT)

# ---------------------------------------------------------------------------
# CRC32 (zlib = ISO 3309, same as panicdump_crc32.h)
# ---------------------------------------------------------------------------

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF

# ---------------------------------------------------------------------------
# Build scenarios
# ---------------------------------------------------------------------------

def make_regs(fault: str) -> list:
    """Return realistic-looking register values for a given fault type."""
    base = {
        "hardfault": {
            "pc": 0x08001234, "lr": 0xFFFFFFF9, "xpsr": 0x21000000,
            "r0": 0x00000000, "r1": 0x20001000, "r2": 0x00000010, "r3": 0xDEADBEEF,
            "r12": 0x00000000,
            "msp": 0x20007FF0, "psp": 0x00000000,
            "control": 0x00000000, "primask": 0x00000000,
            "basepri": 0x00000000, "faultmask": 0x00000000,
            "cfsr": 0x00008200,   # BFARVALID + PRECISERR
            "hfsr": 0x40000000,   # FORCED
            "dfsr": 0x00000000,
            "mmfar": 0x00000000,
            "bfar": 0x20010004,   # bad access address
            "afsr": 0x00000000,
            "shcsr": 0x00000000,
        },
        "memmanage": {
            "pc": 0x08002345, "lr": 0xFFFFFFF9, "xpsr": 0x21000000,
            "r0": 0x00000000, "r1": 0x00000001, "r2": 0x00000002, "r3": 0x00000003,
            "r12": 0x00000000,
            "msp": 0x20007FE0, "psp": 0x00000000,
            "control": 0x00000000, "primask": 0x00000000,
            "basepri": 0x00000000, "faultmask": 0x00000000,
            "cfsr": 0x00000082,   # MMARVALID + DACCVIOL
            "hfsr": 0x00000000,
            "dfsr": 0x00000000,
            "mmfar": 0x08000000,  # tried to write to Flash
            "bfar": 0x00000000,
            "afsr": 0x00000000,
            "shcsr": 0x00010000,  # MemManage active
        },
        "usagefault": {
            "pc": 0x08003456, "lr": 0xFFFFFFF9, "xpsr": 0x21000000,
            "r0": 0xCAFEBABE, "r1": 0x00000000, "r2": 0x00000000, "r3": 0x00000000,
            "r12": 0x00000000,
            "msp": 0x20007FC0, "psp": 0x00000000,
            "control": 0x00000000, "primask": 0x00000000,
            "basepri": 0x00000000, "faultmask": 0x00000000,
            "cfsr": 0x02000000,   # DIVBYZERO
            "hfsr": 0x40000000,
            "dfsr": 0x00000000,
            "mmfar": 0x00000000,
            "bfar": 0x00000000,
            "afsr": 0x00000000,
            "shcsr": 0x00000000,
        },
        "sw_trigger": {
            "pc": 0x08004567, "lr": 0x08004555, "xpsr": 0x01000000,
            "r0": 0x00000000, "r1": 0x00000000, "r2": 0x00000000, "r3": 0x00000000,
            "r12": 0x00000000,
            "msp": 0x20007FA0, "psp": 0x00000000,
            "control": 0x00000000, "primask": 0x00000000,
            "basepri": 0x00000000, "faultmask": 0x00000000,
            "cfsr": 0x00000000,
            "hfsr": 0x00000000,
            "dfsr": 0x00000000,
            "mmfar": 0x00000000,
            "bfar": 0x00000000,
            "afsr": 0x00000000,
            "shcsr": 0x00000000,
        },
    }
    template = base.get(fault, base["hardfault"])
    fields = ["r0","r1","r2","r3","r12","lr","pc","xpsr",
              "msp","psp","control","primask","basepri","faultmask",
              "cfsr","hfsr","dfsr","mmfar","bfar","afsr","shcsr"]
    return [template.get(f, 0) for f in fields]


def make_stack_slice(sp: int) -> bytes:
    """Return a plausible-looking stack slice."""
    data = bytearray(STACK_SLICE)
    # Simulate some return addresses and saved registers on the stack
    addrs = [0x08001100, 0x08002200, 0x08003300, 0x20001000]
    for i, addr in enumerate(addrs):
        struct.pack_into("<I", data, i * 4, addr)
    return bytes(data)


def build_dump(
    fault_name: str = "hardfault",
    user_tag: int   = 42,
    sequence: int   = 0,
    corrupt_crc: bool = False,
    partial: bool     = False,
) -> bytes:
    fault_code = FAULT_CODES.get(fault_name.lower(), 0)
    regs = make_regs(fault_name.lower())
    sp = regs[8]  # msp
    stack_data = make_stack_slice(sp)

    # Registers block
    regs_bytes = struct.pack(REGS_FMT, *regs)

    # Stack block
    stack_bytes = struct.pack(STACK_FMT, sp, STACK_SLICE, stack_data)

    header_size = struct.calcsize(HEADER_FMT)
    total_size  = DUMP_SIZE

    # Pack header with crc32 = 0 first
    header = struct.pack(
        HEADER_FMT,
        MAGIC,
        VERSION,
        header_size,
        total_size,
        0,             # flags
        ARCH_CORTEXM4,
        fault_code,
        sequence,
        user_tag,
        0,             # crc32 placeholder
    )

    raw_no_crc = header + regs_bytes + stack_bytes
    checksum = crc32(raw_no_crc)

    if corrupt_crc:
        checksum ^= 0xDEADBEEF

    # Re-pack with real CRC
    header = struct.pack(
        HEADER_FMT,
        MAGIC,
        VERSION,
        header_size,
        total_size,
        0,
        ARCH_CORTEXM4,
        fault_code,
        sequence,
        user_tag,
        checksum,
    )

    result = header + regs_bytes + stack_bytes

    if partial:
        # Truncate to simulate incomplete write (no magic committed)
        result = struct.pack(HEADER_FMT,
            0,  # magic = 0 (not yet committed)
            VERSION, header_size, total_size, 0,
            ARCH_CORTEXM4, fault_code, sequence, user_tag, checksum,
        ) + regs_bytes + stack_bytes

    return result


def build_hex_framed(dump_bytes: bytes) -> str:
    """Wrap dump bytes in UART hex-frame export format."""
    lines = ["=== PANICDUMP BEGIN ==="]
    raw = dump_bytes.hex().upper()
    for i in range(0, len(raw), 64):
        lines.append(raw[i:i+64])
    lines.append("=== PANICDUMP END ===")
    return "\r\n".join(lines) + "\r\n"

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

VARIANTS = [
    ("valid_hardfault",  dict(fault_name="hardfault",  user_tag=42)),
    ("valid_memmanage",  dict(fault_name="memmanage",  user_tag=99)),
    ("valid_usagefault", dict(fault_name="usagefault", user_tag=7)),
    ("valid_sw_trigger", dict(fault_name="sw_trigger", user_tag=0)),
    ("bad_crc",          dict(fault_name="hardfault",  corrupt_crc=True)),
    ("partial",          dict(fault_name="hardfault",  partial=True)),
]


def main():
    parser = argparse.ArgumentParser(description="panicdump v1 mock dump generator")
    parser.add_argument("--fault",    default="hardfault",
                        choices=list(FAULT_CODES.keys()),
                        help="Fault type for single-file output")
    parser.add_argument("--tag",      type=lambda x: int(x,0), default=42,
                        help="User tag value (hex or decimal)")
    parser.add_argument("--bad-crc",  action="store_true",
                        help="Generate dump with corrupt CRC")
    parser.add_argument("--partial",  action="store_true",
                        help="Generate incomplete dump (magic not committed)")
    parser.add_argument("--hex-export", action="store_true",
                        help="Also write hex-framed .txt export")
    parser.add_argument("--all",      action="store_true",
                        help="Generate all test variants into ./test_vectors/")
    parser.add_argument("-o", "--out", default="mock_dump.bin",
                        help="Output file (single variant)")
    args = parser.parse_args()

    if args.all:
        out_dir = Path("test_vectors")
        out_dir.mkdir(exist_ok=True)
        for name, kw in VARIANTS:
            data = build_dump(**kw)
            bin_path = out_dir / f"{name}.bin"
            bin_path.write_bytes(data)
            # Also write hex-framed version
            txt_path = out_dir / f"{name}.txt"
            txt_path.write_text(build_hex_framed(data))
            crc_label = "BAD CRC" if kw.get("corrupt_crc") else "partial" if kw.get("partial") else "OK"
            print(f"  {bin_path}  ({len(data)} bytes, crc={crc_label})")
        print(f"\nGenerated {len(VARIANTS)} test vectors in {out_dir}/")
        return

    # Single file
    data = build_dump(
        fault_name=args.fault,
        user_tag=args.tag,
        corrupt_crc=args.bad_crc,
        partial=args.partial,
    )
    out_path = Path(args.out)
    out_path.write_bytes(data)
    print(f"Written: {out_path} ({len(data)} bytes)")

    if args.hex_export:
        txt_path = out_path.with_suffix(".txt")
        txt_path.write_text(build_hex_framed(data))
        print(f"Written: {txt_path} (hex-framed)")


if __name__ == "__main__":
    main()
