# SPDX-License-Identifier: Apache-2.0 OR MIT
import fcntl
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "oms"
FAULTS = ROOT / "build" / "oms-faults"
PROBE = ROOT / "build" / "device-probe"


class EraseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.target = Path(self.temp.name) / 'sample space-ąć-".img'
        self.data = b"\xa5" * (1024 * 1024 + 73)
        self.target.write_bytes(self.data)

    def run_erase(self, *extra, fault=None, method="zero"):
        env = os.environ.copy()
        if fault:
            env["OMS_TEST_FAULT"] = fault
        return subprocess.run([
            str(FAULTS if fault else BIN), "erase", str(self.target), "--allow-file",
            "--execute", "--confirm", str(self.target), "--method", method, *extra,
        ], capture_output=True, text=True, env=env, timeout=15)

    def test_unicode_json_and_identity(self):
        result = subprocess.run([str(BIN), "inspect", str(self.target), "--allow-file", "--json"],
                                capture_output=True, text=True, check=True)
        device = json.loads(result.stdout)
        self.assertEqual(device["path"], str(self.target))
        self.assertEqual(self.run_erase("--expect-id", device["identity"], "--verify").returncode, 0)

    def test_changed_identity_does_not_write(self):
        result = self.run_erase("--expect-id", "outdated")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("identity changed", result.stderr)
        self.assertEqual(self.target.read_bytes(), self.data)

    def test_hardlinks_are_rejected(self):
        os.link(self.target, self.target.parent / "alias.img")
        result = self.run_erase()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hard link", result.stderr)
        self.assertEqual(self.target.read_bytes(), self.data)

    def test_concurrent_lock_rejected(self):
        with self.target.open("r+b") as stream:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.run_erase()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("locked", result.stderr)
        self.assertEqual(self.target.read_bytes(), self.data)

    def test_multipass_partial_buffer_and_progress(self):
        result = self.run_erase("--verify", "--passes", "2", "--progress", method="ones")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.target.read_bytes(), b"\xff" * len(self.data))
        self.assertIn("OMS_PROGRESS verifying 2 2", result.stderr)

    def test_random_changes_content_without_size_change(self):
        result = self.run_erase(method="random")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.target.stat().st_size, len(self.data))
        self.assertNotEqual(self.target.read_bytes(), self.data)

    def test_short_and_interrupted_writes_are_retried(self):
        for fault in ("short-write", "write-eintr"):
            with self.subTest(fault=fault):
                self.target.write_bytes(self.data)
                result = self.run_erase("--verify", fault=fault)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.target.read_bytes(), bytes(len(self.data)))

    def test_io_errors_never_report_success(self):
        for fault, message in (
            ("write-zero", "no write progress"), ("write-error", "write failed"),
            ("read-eof", "unexpected end of target"), ("verify-mismatch", "verification mismatch"),
            ("flush-error", "cannot flush"), ("close-error", "cannot close"),
            ("cancel", "interrupted"), ("changed-size", "does not match"),
        ):
            with self.subTest(fault=fault):
                self.target.write_bytes(self.data)
                result = self.run_erase("--verify", fault=fault)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertNotIn("Erase completed", result.stdout)
                if fault == "changed-size":
                    self.assertEqual(self.target.read_bytes(), self.data)

    def test_invalid_options_leave_data_unchanged(self):
        for arguments in (("--passes", "0"), ("--passes", "17"), ("--passes", "-1"),
                          ("--passes", " 1"), ("--passes", "+1"), ("--method", "oops"),
                          ("--method", "random", "--verify")):
            with self.subTest(arguments=arguments):
                self.assertNotEqual(self.run_erase(*arguments).returncode, 0)
                self.assertEqual(self.target.read_bytes(), self.data)


class DeviceUsageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        for directory in ("proc/self", "sys/dev/block", "sys/class/block"):
            (self.base / directory).mkdir(parents=True)
        self.mountinfo = self.base / "proc/self/mountinfo"
        self.mountinfo.write_text("1 0 0:1 / / rw - tmpfs none rw\n")
        self.swaps = self.base / "proc/swaps"
        self.swaps.write_text("Filename Type Size Used Priority\n")
        self.disk = self.disk_fixture("disk-a", "8:0")
        self.partition = self.disk_fixture("disk-a/disk-a1", "8:1", partition=True)
        self.other = self.disk_fixture("disk-b", "8:16")

    def disk_fixture(self, name, number, partition=False):
        path = self.base / "sys/devices" / name
        path.mkdir(parents=True, exist_ok=True)
        (path / "holders").mkdir()
        if partition:
            (path / "partition").write_text("1\n")
        else:
            (path / "slaves").mkdir()
        (self.base / "sys/class/block" / path.name).symlink_to(path)
        (self.base / "sys/dev/block" / number).symlink_to(path)
        return path

    def probe(self, mode, target):
        result = subprocess.run([str(PROBE), mode, str(target)], cwd=self.base,
                                capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def mount(self, number):
        self.mountinfo.write_text(f"10 1 {number} / /media rw - ext4 /dev/example rw\n")

    def test_unused_target_is_idle(self):
        self.assertEqual(self.probe("mounted", self.disk), "idle")
        self.assertEqual(self.probe("holders", self.disk), "idle")

    def test_mounted_descendant_and_mounted_parent_are_blocked(self):
        self.mount("8:1")
        self.assertEqual(self.probe("mounted", self.disk), "busy")
        self.mount("8:0")
        self.assertEqual(self.probe("mounted", self.partition), "busy")

    def test_unrelated_mounted_device_does_not_block_target(self):
        self.mount("8:16")
        self.assertEqual(self.probe("mounted", self.disk), "idle")

    def test_mapper_dependency_is_blocked(self):
        mapper = self.disk_fixture("mapper", "253:0")
        (mapper / "slaves/member").symlink_to(self.partition)
        self.mount("253:0")
        self.assertEqual(self.probe("mounted", self.disk), "busy")

    def test_missing_malformed_or_empty_mount_data_is_blocked(self):
        for content in ("", "corrupt\n", "1 0 8:99 / / rw - ext4 /dev/gone rw\n"):
            self.mountinfo.write_text(content)
            self.assertEqual(self.probe("mounted", self.disk), "busy")
        self.mountinfo.unlink()
        self.assertEqual(self.probe("mounted", self.disk), "busy")

    def test_child_holder_is_blocked(self):
        (self.partition / "holders/mapper").write_text("")
        self.assertEqual(self.probe("holders", self.disk), "busy")
        self.assertEqual(self.probe("holders", self.other), "idle")

    def test_missing_holders_are_blocked(self):
        (self.disk / "holders").rmdir()
        self.assertEqual(self.probe("holders", self.disk), "busy")

    def test_swap_file_with_escaped_space_is_blocked(self):
        target = self.base / "test image"
        target.write_bytes(b"sample")
        self.assertEqual(self.probe("swap", target), "idle")
        escaped = str(target).replace(" ", "\\040")
        self.swaps.write_text(f"Filename Type Size Used Priority\n{escaped} file 4 0 -2\n")
        self.assertEqual(self.probe("swap", target), "busy")

    def test_missing_swap_status_is_blocked(self):
        target = self.base / "image"
        target.write_bytes(b"sample")
        self.swaps.unlink()
        self.assertEqual(self.probe("swap", target), "busy")

    def test_attached_loop_file_is_blocked(self):
        target = self.base / "test image"
        target.write_bytes(b"sample")
        loop = self.disk_fixture("loop0", "7:0")
        (loop / "loop").mkdir()
        (loop / "loop/backing_file").write_text(str(target).replace(" ", "\\040") + "\n")
        self.assertEqual(self.probe("loop", target), "busy")

    def test_unresolvable_loop_backing_file_is_blocked(self):
        target = self.base / "test image"
        target.write_bytes(b"sample")
        loop = self.disk_fixture("loop0", "7:0")
        (loop / "loop").mkdir()
        (loop / "loop/backing_file").write_text(str(self.base / "renamed-image") + "\n")
        self.assertEqual(self.probe("loop", target), "busy")


if __name__ == "__main__":
    unittest.main()
