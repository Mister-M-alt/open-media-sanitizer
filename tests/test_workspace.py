# SPDX-License-Identifier: Apache-2.0 OR MIT
import json
from pathlib import Path
import queue
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "app"))
from workspace import Backend, Job, Settings, report_html, save_report


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.backend = Backend(demo=True)
        self.addCleanup(self.backend.close)
        self.devices, _ = self.backend.inventory()
        self.device = self.devices[0]

    def collect(self, job):
        job.start()
        job.thread.join(timeout=15)
        if job.thread.is_alive():
            job.cancel()
            job.thread.join(timeout=15)
            self.fail("Job did not finish in time")
        events = []
        while True:
            try:
                events.append(job.events.get_nowait())
            except queue.Empty:
                break
        self.assertEqual(events[-1][0], "finished")
        return events, events[-1][1]

    def test_real_demo_job_and_reports(self):
        job = Job(self.backend, self.device, Settings(), self.device["path"])
        events, record = self.collect(job)
        self.assertEqual(record["outcome"], "completed", record["log"])
        self.assertEqual(record["mode"], "demo")
        self.assertTrue(record["verification_completed"])
        self.assertTrue(any(kind == "progress" for kind, _ in events))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "operation.json"
            save_report(record, path, "json")
            self.assertEqual(json.loads(path.read_text())["id"], record["id"])
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            save_report(record, Path(directory) / "operation.html", "html")

    def test_wrong_confirmation_never_starts(self):
        with self.assertRaises(ValueError):
            Job(self.backend, self.device, Settings(), "/incorrect")

    def test_demo_rejects_arbitrary_files_and_devices(self):
        for kind, path in (("block", "/dev/example"), ("file", "/tmp/not-owned.img")):
            device = dict(self.device, kind=kind, path=path)
            with self.assertRaises(ValueError):
                self.backend.command(device, Settings(), path)

    def test_busy_targets_are_rejected(self):
        for flag in ("read_only", "mounted", "swap_active", "has_holders"):
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                self.backend.command(dict(self.device, **{flag: True}), Settings(), self.device["path"])

    def test_cancel_before_spawn_does_not_write(self):
        before = Path(self.device["path"]).read_bytes()
        job = Job(self.backend, self.device, Settings(), self.device["path"])
        job.cancel()
        _, record = self.collect(job)
        self.assertEqual(record["outcome"], "cancelled")
        self.assertFalse(record["verification_completed"])
        self.assertEqual(Path(self.device["path"]).read_bytes(), before)

    def test_spawn_failure_is_a_failed_record(self):
        self.backend.binary = Path("/does/not/exist")
        _, record = self.collect(Job(self.backend, self.device, Settings(), self.device["path"]))
        self.assertEqual(record["outcome"], "failed")

    def test_report_escapes_device_text_and_logs(self):
        _, record = self.collect(Job(self.backend, self.device, Settings(), self.device["path"]))
        record["target"]["model"] = '<script>alert("example")</script>'
        record["log"].append("<img src=x onerror=alert(1)>")
        rendered = report_html(record)
        self.assertNotIn("<script>", rendered)
        self.assertNotIn("<img ", rendered)
        self.assertIn("&lt;script&gt;", rendered)
        with self.assertRaises(ValueError):
            save_report(record, self.device["path"], "json")

    def test_settings_reject_incompatible_verification(self):
        with self.assertRaises(ValueError):
            Settings(method="random", verify=True).validate()
        with self.assertRaises(ValueError):
            Settings(passes=0).validate()


if __name__ == "__main__":
    unittest.main()
