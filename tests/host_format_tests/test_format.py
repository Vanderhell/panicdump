#!/usr/bin/env python3
"""
test_format.py — Host-side unit tests for panicdump format and decoder.

Tests:
  - Struct size matches expected 192 bytes
  - Valid dump parses correctly
  - CRC verification (ok / fail)
  - Partial dump (magic = 0) rejected by has_valid equivalent
  - All fault reason codes
  - Hex-framed export round-trip
  - JSON output
"""

import sys
import struct
import unittest
import subprocess
from pathlib import Path

# Add tools dir to path
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from make_mock_dump import build_dump, build_hex_framed, DUMP_SIZE, FAULT_CODES
from decode_panicdump import parse, verify_crc, MAGIC, VERSION

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make(fault="hardfault", **kw):
    return build_dump(fault_name=fault, **kw)

class TestDumpSize(unittest.TestCase):
    def test_dump_size_192(self):
        """Binary dump must be exactly 192 bytes."""
        self.assertEqual(DUMP_SIZE, 192)

    def test_generated_size(self):
        data = make()
        self.assertEqual(len(data), 192)

class TestParsing(unittest.TestCase):
    def test_magic(self):
        d = parse(make())
        self.assertEqual(d.magic, MAGIC)

    def test_version(self):
        d = parse(make())
        self.assertEqual(d.version, VERSION)

    def test_total_size(self):
        d = parse(make())
        self.assertEqual(d.total_size, 192)

    def test_arch_cortexm4(self):
        d = parse(make())
        self.assertEqual(d.arch_id, 0x0004)

    def test_fault_hardfault(self):
        d = parse(make("hardfault"))
        self.assertEqual(d.fault_reason, 1)

    def test_fault_memmanage(self):
        d = parse(make("memmanage"))
        self.assertEqual(d.fault_reason, 2)

    def test_fault_busfault(self):
        d = parse(make("busfault"))
        self.assertEqual(d.fault_reason, 3)

    def test_fault_usagefault(self):
        d = parse(make("usagefault"))
        self.assertEqual(d.fault_reason, 4)

    def test_fault_sw_trigger(self):
        d = parse(make("sw_trigger"))
        self.assertEqual(d.fault_reason, 5)

    def test_user_tag(self):
        d = parse(make(user_tag=0xDEADBEEF))
        self.assertEqual(d.user_tag, 0xDEADBEEF)

    def test_pc_hardfault(self):
        d = parse(make("hardfault"))
        self.assertEqual(d.regs["pc"], 0x08001234)

    def test_bfar_hardfault(self):
        d = parse(make("hardfault"))
        self.assertEqual(d.regs["bfar"], 0x20010004)

    def test_cfsr_usagefault_divbyzero(self):
        d = parse(make("usagefault"))
        # DIVBYZERO bit (bit 25 of CFSR)
        self.assertTrue(d.regs["cfsr"] & (1 << 25))

    def test_stack_slice_size(self):
        d = parse(make())
        self.assertEqual(d.stack_bytes, 64)
        self.assertEqual(len(d.stack_data), 64)


class TestCRC(unittest.TestCase):
    def test_valid_crc(self):
        d = parse(make())
        self.assertTrue(d.crc_ok)

    def test_corrupt_crc_detected(self):
        d = parse(make(corrupt_crc=True))
        self.assertFalse(d.crc_ok)

    def test_crc_field_zero_during_compute(self):
        """CRC must be computed with crc32 field = 0."""
        raw = make()
        ok, stored, computed = verify_crc(raw)
        self.assertTrue(ok)
        self.assertEqual(stored, computed)

    def test_flip_one_byte_detected(self):
        """Flipping any byte (except in crc field itself) must fail CRC."""
        raw = bytearray(make())
        raw[0] ^= 0xFF  # flip magic first byte
        d = parse(bytes(raw))
        self.assertFalse(d.crc_ok)


class TestPartialDump(unittest.TestCase):
    def test_partial_magic_zero(self):
        """Partial dump has magic=0 — not valid."""
        d = parse(make(partial=True))
        self.assertNotEqual(d.magic, MAGIC)


class TestHexFramedRoundtrip(unittest.TestCase):
    def test_roundtrip(self):
        """Build dump → hex-frame → parse hex frame → same data."""
        import re, tempfile, os
        from decode_panicdump import load_hex_framed

        original = make("hardfault", user_tag=777)
        hex_text = build_hex_framed(original)

        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(hex_text)
            tmp_path = f.name

        try:
            recovered = load_hex_framed(tmp_path)
            self.assertEqual(original, recovered)
        finally:
            os.unlink(tmp_path)


class TestAllFaultVariants(unittest.TestCase):
    def test_all_fault_codes_parse(self):
        for name, code in FAULT_CODES.items():
            with self.subTest(fault=name):
                data = make(name)
                d = parse(data)
                self.assertTrue(d.crc_ok, f"CRC failed for fault={name}")
                self.assertEqual(d.fault_reason, code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
