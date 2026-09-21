# SPDX-License-Identifier: Apache-2.0 OR MIT
"""Application state, process supervision, and portable operation reports."""

from collections import deque
from dataclasses import dataclass
from datetime import datetime, timezone
import html
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]
LIMITATION = (
    "This record describes writes to the exposed logical address range. "
    "It does not certify sanitization of hidden, remapped, or overprovisioned storage."
)


def readable_size(value):
    value = float(value)
    for unit in ("B", "KiB", "MiB", "GiB", "TiB", "PiB"):
        if value < 1024 or unit == "PiB":
            return f"{value:.1f} {unit}"
        value /= 1024


def blocked_reason(device):
    for key, message in (
        ("read_only", "Read-only"),
        ("mounted", "Mounted or mount status unavailable"),
        ("swap_active", "Swap active or status unavailable"),
        ("has_holders", "In use or dependency status unavailable"),
    ):
        if device.get(key):
            return message
    if device["size_bytes"] <= 0:
        return "Empty device"
    return ""


@dataclass(frozen=True)
class Settings:
    method: str = "zero"
    passes: int = 1
    verify: bool = True

    def validate(self):
        if self.method not in ("zero", "ones", "random"):
            raise ValueError("Select a supported write pattern.")
        if type(self.passes) is not int or not 1 <= self.passes <= 16:
            raise ValueError("Pass count must be an integer from 1 to 16.")
        if self.method == "random" and self.verify:
            raise ValueError("Read-back verification is available for zero and ones patterns.")


class Backend:
    def __init__(self, demo=False, binary=None):
        self.binary = Path(binary or ROOT / "build" / "oms").resolve()
        self.demo = demo
        self._temporary = tempfile.TemporaryDirectory(prefix="oms-demo-") if demo else None
        self.demo_paths = []
        if demo:
            for index, size in enumerate((32, 64), start=1):
                path = Path(self._temporary.name) / f"sample-{index}.img"
                with path.open("wb") as stream:
                    stream.write(b"Disposable demonstration data\n")
                    stream.truncate(size * 1024 * 1024)
                self.demo_paths.append(str(path.resolve()))

    def close(self):
        if self._temporary is not None:
            self._temporary.cleanup()

    def _json(self, arguments):
        result = subprocess.run(
            [str(self.binary), *arguments], stdin=subprocess.DEVNULL,
            capture_output=True, text=True, errors="surrogateescape", timeout=30,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or "Device inspection failed.")
        return json.loads(result.stdout), result.stderr.strip()

    def inventory(self):
        if not self.demo:
            return self._json(["list", "--json"])
        devices = []
        for index, path in enumerate(self.demo_paths, start=1):
            device, _ = self._json(["inspect", path, "--json", "--allow-file"])
            device["model"] = f"Demo medium {index}"
            devices.append(device)
        return devices, ""

    def command(self, device, settings, confirmation):
        settings.validate()
        if confirmation != device["path"]:
            raise ValueError("Type the complete target path exactly as displayed.")
        reason = blocked_reason(device)
        if reason:
            raise ValueError(reason)
        if self.demo:
            if device["kind"] != "file" or device["path"] not in self.demo_paths:
                raise ValueError("Demo mode can only write its own disposable files.")
        elif device["kind"] != "block":
            raise ValueError("Live mode only accepts block devices.")
        arguments = [
            str(self.binary), "erase", device["path"], "--method", settings.method,
            "--passes", str(settings.passes), "--execute", "--confirm", confirmation,
            "--expect-id", device["identity"], "--progress",
        ]
        if settings.verify:
            arguments.append("--verify")
        if self.demo:
            arguments.append("--allow-file")
        return arguments


class Job:
    """One subprocess, one immutable target, and a queue consumed by the UI thread."""

    def __init__(self, backend, device, settings, confirmation):
        self.command = backend.command(device, settings, confirmation)
        self.device = dict(device)
        self.settings = settings
        self.demo = backend.demo
        self.events = queue.Queue()
        self._lock = threading.Lock()
        self._cancelled = threading.Event()
        self._process = None
        self.thread = threading.Thread(target=self._run, daemon=False)

    def start(self):
        self.thread.start()

    def cancel(self):
        self._cancelled.set()
        with self._lock:
            if self._process is not None and self._process.poll() is None:
                try:
                    self._process.terminate()
                except ProcessLookupError:
                    pass

    def _run(self):
        started = datetime.now(timezone.utc).isoformat()
        clock = time.monotonic()
        log = deque(maxlen=300)
        status = "failed"
        code = None
        try:
            with self._lock:
                if self._cancelled.is_set():
                    status = "cancelled"
                else:
                    self._process = subprocess.Popen(
                        self.command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1,
                    )
            if self._process is not None:
                with self._process as process:
                    for line in process.stdout:
                        if line.startswith("OMS_PROGRESS "):
                            parts = line.split()
                            if len(parts) == 6:
                                phase, current, passes, completed, total = parts[1:]
                                self.events.put(("progress", {
                                    "phase": phase, "pass": int(current), "passes": int(passes),
                                    "completed": int(completed), "total": int(total),
                                }))
                        else:
                            log.append(line.rstrip())
                            self.events.put(("log", line.rstrip()))
                    code = process.wait()
                status = "completed" if code == 0 else (
                    "cancelled" if self._cancelled.is_set() else "failed"
                )
        except (OSError, ValueError) as error:
            log.append(str(error))
            with self._lock:
                if self._process is not None and self._process.poll() is None:
                    self._process.terminate()
                    self._process.wait()
        finally:
            record = {
                "schema_version": 1, "id": str(uuid.uuid4()), "application": "Open Media Sanitizer",
                "mode": "demo" if self.demo else "live", "started_at": started,
                "finished_at": datetime.now(timezone.utc).isoformat(),
                "elapsed_seconds": round(time.monotonic() - clock, 3),
                "target": self.device, "method": self.settings.method,
                "passes": self.settings.passes, "verification_requested": self.settings.verify,
                "verification_completed": status == "completed" and self.settings.verify,
                "outcome": status, "exit_code": code, "log": list(log), "scope": LIMITATION,
            }
            self.events.put(("finished", record))


def report_html(record):
    """A standalone, script-free report. All device-controlled text is escaped."""
    escape = lambda value: html.escape(str(value), quote=True)
    rows = {
        "Record": record["id"], "Mode": record["mode"], "Outcome": record["outcome"],
        "Target": record["target"]["path"], "Model": record["target"]["model"],
        "Size": readable_size(record["target"]["size_bytes"]), "Pattern": record["method"],
        "Passes": record["passes"], "Started (UTC)": record["started_at"],
        "Finished (UTC)": record["finished_at"], "Duration (seconds)": record["elapsed_seconds"],
        "Read-back verified": "Yes" if record["verification_completed"] else "No",
    }
    body = "".join(f"<tr><th>{escape(k)}</th><td>{escape(v)}</td></tr>" for k, v in rows.items())
    log = escape("\n".join(record["log"]))
    return (
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        '<title>Media operation record</title><style>'
        'body{font:16px system-ui,sans-serif;background:#edf2f6;color:#142a38;margin:0;padding:4vw}'
        'main{max-width:900px;margin:auto;background:white;padding:40px;border-radius:16px}'
        'h1{font-size:32px}h2{font-size:18px;color:#087f75}table{width:100%;border-collapse:collapse}'
        'th,td{text-align:left;padding:12px;border-bottom:1px solid #dce5eb;overflow-wrap:anywhere}'
        'th{width:32%}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#edf2f6;padding:20px}'
        '@media print{body{background:white;padding:0}main{padding:0}}'
        '</style><main><h2>OPEN MEDIA SANITIZER / OPERATION RECORD</h2>'
        f'<h1>{escape(record["outcome"].capitalize())}</h1><table>{body}</table>'
        f'<p>{escape(record["scope"])}</p><h2>Operation log</h2><pre>{log}</pre></main></html>\n'
    )


def save_report(record, path, format_name):
    if format_name not in ("json", "html"):
        raise ValueError("Unsupported report format.")
    path = Path(path)
    target = Path(record["target"]["path"])
    if path.resolve() == target.resolve() or (
        path.exists() and target.exists() and os.path.samefile(path, target)
    ):
        raise ValueError("Choose a report destination separate from the erase target.")
    if path.exists() and not path.is_file():
        raise ValueError("Reports must be saved to a regular file.")
    content = json.dumps(record, indent=2, ensure_ascii=True) + "\n" if format_name == "json" else report_html(record)
    # Write beside the destination and replace atomically; an interrupted save
    # leaves any existing report intact. New reports are private to this user.
    descriptor, temporary = tempfile.mkstemp(prefix=".oms-report-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
