#!/usr/bin/env python3
"""
CLI integration tests for decode_panicdump.py.
"""

import subprocess
import sys
import tempfile
import os
import json
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).parent.parent.parent / "tools"
DECODER = str(TOOLS_DIR / "decode_panicdump.py")

sys.path.insert(0, str(TOOLS_DIR))
from make_mock_dump import build_dump, build_hex_framed


def run(args, input_file=None):
    cmd = [sys.executable, DECODER] + args
    if input_file:
        cmd.append(input_file)
    return subprocess.run(cmd, capture_output=True, text=True)


class TempDump:
    def __init__(self, data: bytes, suffix=".bin"):
        self.data = data
        self.suffix = suffix
        self._f = None

    def __enter__(self):
        self._f = tempfile.NamedTemporaryFile(suffix=self.suffix, delete=False)
        self._f.write(self.data)
        self._f.close()
        return self._f.name

    def __exit__(self, *_):
        os.unlink(self._f.name)


class TestCLIValidDump(unittest.TestCase):
    def setUp(self):
        self.data = build_dump(fault_name="hardfault", user_tag=42)

    def test_exit_code_zero_valid(self):
        with TempDump(self.data) as path:
            self.assertEqual(run([], path).returncode, 0)

    def test_output_contains_arch(self):
        with TempDump(self.data) as path:
            self.assertIn("Cortex-M4", run([], path).stdout)

    def test_output_contains_fault_type(self):
        with TempDump(self.data) as path:
            self.assertIn("HardFault", run([], path).stdout)

    def test_output_contains_pc(self):
        with TempDump(self.data) as path:
            self.assertIn("0x08001234", run([], path).stdout)

    def test_output_crc_ok(self):
        with TempDump(self.data) as path:
            self.assertIn("OK", run([], path).stdout)

    def test_output_user_tag(self):
        with TempDump(self.data) as path:
            self.assertIn("42", run([], path).stdout)


class TestCLIBadCRC(unittest.TestCase):
    def test_exit_code_nonzero(self):
        with TempDump(build_dump(fault_name="hardfault", corrupt_crc=True)) as path:
            self.assertEqual(run([], path).returncode, 1)

    def test_output_contains_fail(self):
        with TempDump(build_dump(fault_name="hardfault", corrupt_crc=True)) as path:
            self.assertIn("FAIL", run([], path).stdout)


class TestCLIVerifyMode(unittest.TestCase):
    def test_verify_ok_exit_0(self):
        with TempDump(build_dump()) as path:
            r = run(["--verify"], path)
        self.assertEqual(r.returncode, 0)
        self.assertIn("OK", r.stdout)

    def test_verify_bad_exit_1(self):
        with TempDump(build_dump(corrupt_crc=True)) as path:
            r = run(["--verify"], path)
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL", r.stdout)


class TestCLIJsonOutput(unittest.TestCase):
    def _get_json(self, fault="hardfault", expect_exit=0, **kw):
        with TempDump(build_dump(fault_name=fault, **kw)) as path:
            r = run(["--json"], path)
        self.assertEqual(r.returncode, expect_exit)
        return json.loads(r.stdout)

    def test_json_parseable(self):
        self._get_json()

    def test_json_arch_name(self):
        self.assertEqual(self._get_json()["arch_name"], "Cortex-M4")

    def test_json_fault_name(self):
        self.assertEqual(self._get_json("memmanage")["fault_name"], "MemManage")

    def test_json_crc_ok(self):
        self.assertTrue(self._get_json()["crc_ok"])

    def test_json_crc_fail(self):
        with TempDump(build_dump(corrupt_crc=True)) as path:
            r = run(["--json"], path)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(r.stdout.strip(), "")

    def test_json_cfsr_decoded(self):
        decoded = self._get_json()["cfsr_decoded"]
        self.assertTrue(any("BFARVALID" in s for s in decoded))
        self.assertTrue(any("PRECISERR" in s for s in decoded))

    def test_json_usagefault_divbyzero(self):
        self.assertTrue(any("DIVBYZERO" in s for s in self._get_json("usagefault")["cfsr_decoded"]))

    def test_json_user_tag(self):
        self.assertEqual(self._get_json(user_tag=0xABCD1234)["user_tag"], 0xABCD1234)

    def test_json_stack_data_hex(self):
        self.assertEqual(len(self._get_json()["stack_data"]), 128)

    def test_json_regs_present(self):
        j = self._get_json()
        for reg in ["pc", "lr", "msp", "cfsr", "hfsr"]:
            self.assertIn(reg, j["regs"])

    def test_json_refuses_crc_fail_without_forensic(self):
        with TempDump(build_dump(corrupt_crc=True)) as path:
            r = run(["--json"], path)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(r.stdout.strip(), "")

    def test_json_forensic_override(self):
        with TempDump(build_dump(corrupt_crc=True)) as path:
            r = run(["--json", "--forensic"], path)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('"crc_ok": false', r.stdout.lower())


class TestCLIHexFramed(unittest.TestCase):
    def test_hex_framed_round_trip(self):
        data = build_dump(fault_name="busfault", user_tag=99)
        hex_text = build_hex_framed(data)

        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(hex_text)
            path = f.name

        try:
            r = subprocess.run([sys.executable, DECODER, "--hex", path],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0)
            self.assertIn("BusFault", r.stdout)
            self.assertIn("OK", r.stdout)
        finally:
            os.unlink(path)

    def test_hex_framed_missing_markers_error(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write("some random text without markers\n")
            path = f.name
        try:
            r = subprocess.run([sys.executable, DECODER, "--hex", path],
                               capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
        finally:
            os.unlink(path)

    def test_hex_framed_trailing_data_error(self):
        data = build_hex_framed(build_dump())
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(data + "trailing")
            path = f.name
        try:
            r = subprocess.run([sys.executable, DECODER, "--hex", path],
                               capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
        finally:
            os.unlink(path)

    def test_hex_framed_malformed_hex_error(self):
        data = build_hex_framed(build_dump())
        data = data.replace("\r\n", "\r\nZ", 1)
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(data)
            path = f.name
        try:
            r = subprocess.run([sys.executable, DECODER, "--hex", path],
                               capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
        finally:
            os.unlink(path)


class TestCLIAllFaultTypes(unittest.TestCase):
    def test_all_fault_types_decode_ok(self):
        fault_display = {
            "hardfault": "HardFault",
            "memmanage": "MemManage",
            "busfault": "BusFault",
            "usagefault": "UsageFault",
            "sw_trigger": "SW_Trigger",
        }
        for name in ["hardfault", "memmanage", "busfault", "usagefault", "sw_trigger"]:
            with self.subTest(fault=name):
                with TempDump(build_dump(fault_name=name)) as path:
                    r = run([], path)
                self.assertEqual(r.returncode, 0)
                self.assertIn(fault_display[name], r.stdout)


class TestCLIEdgeCases(unittest.TestCase):
    def test_file_not_found(self):
        r = run([], "/nonexistent/path/dump.bin")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("error", r.stderr.lower())

    def test_truncated_file(self):
        with TempDump(build_dump()[:50]) as path:
            r = run([], path)
        self.assertNotEqual(r.returncode, 0)

    def test_empty_file(self):
        with TempDump(b"") as path:
            r = run([], path)
        self.assertNotEqual(r.returncode, 0)

    def test_partial_dump_bad_magic(self):
        with TempDump(build_dump(partial=True)) as path:
            r = run([], path)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("magic", r.stderr.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
