# SPDX-License-Identifier: Apache-2.0 OR MIT
import os
from pathlib import Path
import sys
import time
import signal
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "app"))


@unittest.skipUnless(os.environ.get("DISPLAY"), "Run make test-gui for the graphical tests")
class DesktopTests(unittest.TestCase):
    def setUp(self):
        import tkinter as tk
        from main import Workspace
        from workspace import Backend
        self.backend = Backend(demo=True)
        self.root = tk.Tk()
        self.app = Workspace(self.root, self.backend)
        self.addCleanup(self.cleanup)
        self.until(lambda: not self.app.refreshing)

    def cleanup(self):
        if self.app.active:
            self.app.cancel_job()
            self.until(lambda: not self.app.active)
        self.root.destroy()
        self.backend.close()

    def until(self, condition, timeout=15):
        deadline = time.monotonic() + timeout
        while not condition():
            self.root.update()
            if time.monotonic() > deadline:
                self.fail("Desktop operation timed out")
            time.sleep(0.01)
        self.root.update()

    def choose(self):
        self.app.inventory_tree.selection_set("0")
        self.app.select_device()
        self.app.prepare_review()
        self.root.update()

    def test_review_requires_confirmation_and_completes_demo(self):
        self.assertEqual(self.app.page, "Storage")
        self.assertEqual(len(self.app.devices), 2)
        self.choose()
        self.assertEqual(self.app.page, "Review")
        self.assertEqual(self.app.execute_button.cget("state"), "disabled")
        self.app.confirmation.set("wrong")
        self.assertEqual(self.app.execute_button.cget("state"), "disabled")
        self.app.confirmation.set(self.app.review_device["path"])
        self.assertEqual(self.app.execute_button.cget("state"), "normal")
        self.app.start_job()
        self.until(lambda: not self.app.active)
        self.assertEqual(self.app.records[-1]["outcome"], "completed")
        self.assertTrue(self.app.records[-1]["verification_completed"])
        self.app.show("Reports")
        self.root.update()
        self.assertEqual(self.app.page, "Reports")

    def test_random_disables_verification(self):
        self.app.method.set("random")
        self.app.update_method()
        self.assertFalse(self.app.verify.get())
        self.assertEqual(self.app.verify_control.cget("state"), "disabled")

    def test_navigation_and_resize(self):
        for page in ("Activity", "Reports", "About", "Storage"):
            self.app.show(page)
            self.root.geometry("960x660")
            self.root.update()
            self.assertEqual(self.app.page, page)

    def test_launcher_opens_from_another_directory_and_closes_cleanly(self):
        self.check_launcher_exit(signal.SIGINT)

    def test_launcher_cleans_up_on_termination(self):
        self.check_launcher_exit(signal.SIGTERM)

    def check_launcher_exit(self, requested_signal):
        launcher = Path(__file__).resolve().parents[1] / "start.sh"
        with tempfile.TemporaryDirectory() as directory:
            environment = dict(os.environ, TMPDIR=directory)
            process = subprocess.Popen([str(launcher), "--demo"], cwd=directory,
                                       env=environment, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            try:
                self.until(lambda: bool(list(Path(directory).glob("oms-demo-*"))))
                time.sleep(0.2)
                process.send_signal(requested_signal)
                output, errors = process.communicate(timeout=10)
                self.assertEqual(process.returncode, 0, output + errors)
                self.assertFalse(list(Path(directory).glob("oms-demo-*")))
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.communicate(timeout=10)


if __name__ == "__main__":
    unittest.main()
