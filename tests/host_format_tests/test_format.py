#!/usr/bin/env python3
"""
Host-side unit tests for panicdump format and decoder.
"""

import sys
import unittest
import tempfile
import os
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent / "tools"))

from make_mock_dump import (
    build_dump,
    build_hex_framed,
    DUMP_SIZE,
    FAULT_CODES,
    FLAG_FRAME_VALID,
    FLAG_STACK_VALID,
    EXC_RETURN_MSP,
)
from decode_panicdump import parse, verify_crc, MAGIC, VERSION


def make(fault="hardfault", **kw):
    return build_dump(fault_name=fault, **kw)


class TestDumpSize(unittest.TestCase):
    def test_dump_size_192(self):
        self.assertEqual(DUMP_SIZE, 192)

    def test_generated_size(self):
        self.assertEqual(len(make()), 192)


class TestParsing(unittest.TestCase):
    def test_magic(self):
        self.assertEqual(parse(make()).magic, MAGIC)

    def test_version(self):
        self.assertEqual(parse(make()).version, VERSION)

    def test_total_size(self):
        self.assertEqual(parse(make()).total_size, 192)

    def test_exc_return(self):
        self.assertEqual(parse(make()).sequence, EXC_RETURN_MSP)

    def test_flags(self):
        d = parse(make())
        self.assertTrue(d.flags & FLAG_FRAME_VALID)
        self.assertTrue(d.flags & FLAG_STACK_VALID)

    def test_arch_cortexm4(self):
        self.assertEqual(parse(make()).arch_id, 0x0004)

    def test_fault_hardfault(self):
        self.assertEqual(parse(make("hardfault")).fault_reason, 1)

    def test_fault_memmanage(self):
        self.assertEqual(parse(make("memmanage")).fault_reason, 2)

    def test_fault_busfault(self):
        self.assertEqual(parse(make("busfault")).fault_reason, 3)

    def test_fault_usagefault(self):
        self.assertEqual(parse(make("usagefault")).fault_reason, 4)

    def test_fault_sw_trigger(self):
        self.assertEqual(parse(make("sw_trigger")).fault_reason, 5)

    def test_user_tag(self):
        self.assertEqual(parse(make(user_tag=0xDEADBEEF)).user_tag, 0xDEADBEEF)

    def test_pc_hardfault(self):
        self.assertEqual(parse(make("hardfault")).regs["pc"], 0x08001234)

    def test_bfar_hardfault(self):
        self.assertEqual(parse(make("hardfault")).regs["bfar"], 0x20010004)

    def test_cfsr_usagefault_divbyzero(self):
        self.assertTrue(parse(make("usagefault")).regs["cfsr"] & (1 << 25))

    def test_stack_slice_size(self):
        d = parse(make())
        self.assertEqual(d.stack_bytes, 64)
        self.assertEqual(len(d.stack_data), 64)


class TestCRC(unittest.TestCase):
    def test_valid_crc(self):
        self.assertTrue(parse(make()).crc_ok)

    def test_corrupt_crc_detected(self):
        self.assertFalse(parse(make(corrupt_crc=True)).crc_ok)

    def test_crc_field_zero_during_compute(self):
        raw = make()
        ok, stored, computed = verify_crc(raw)
        self.assertTrue(ok)
        self.assertEqual(stored, computed)

    def test_flip_one_byte_detected(self):
        raw = bytearray(make())
        raw[40] ^= 0xFF
        self.assertFalse(parse(bytes(raw)).crc_ok)

    def test_one_bit_corruption_header(self):
        raw = bytearray(make())
        raw[12] ^= 0x01
        self.assertFalse(parse(bytes(raw)).crc_ok)

    def test_one_bit_corruption_stack(self):
        raw = bytearray(make())
        raw[128] ^= 0x01
        self.assertFalse(parse(bytes(raw)).crc_ok)


class TestPartialDump(unittest.TestCase):
    def test_partial_magic_zero(self):
        with self.assertRaises(ValueError):
            parse(make(partial=True))

    def test_crc_missing_before_magic(self):
        raw = bytearray(make())
        raw[32:36] = b"\x00\x00\x00\x00"
        self.assertFalse(parse(bytes(raw)).crc_ok)


class TestHexFramedRoundtrip(unittest.TestCase):
    def test_roundtrip(self):
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
                d = parse(make(name))
                self.assertTrue(d.crc_ok, f"CRC failed for fault={name}")
                self.assertEqual(d.fault_reason, code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
