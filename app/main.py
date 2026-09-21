#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 OR MIT
"""Native desktop workspace. No web service or third-party Python packages."""

import argparse
import os
import queue
import signal
import sys
import threading

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except ImportError as error:
    print(f"The desktop app needs Python Tk: {error}\n"
          "Debian/Ubuntu: sudo apt install python3-tk\nArch: sudo pacman -S tk\n"
          "Fedora: sudo dnf install python3-tkinter", file=sys.stderr)
    raise SystemExit(1)

from workspace import Backend, Job, ROOT, Settings, blocked_reason, readable_size, save_report


BG = "#eef3f7"
INK = "#172f41"
MUTED = "#526877"
ACCENT = "#087f75"
NAV = "#102d3e"
WHITE = "#ffffff"


class Workspace:
    def __init__(self, root, backend):
        self.root = root
        self.backend = backend
        self.devices = []
        self.selected = None
        self.records = []
        self.job = None
        self.active = False
        self.refreshing = False
        self.close_pending = False
        self.messages = queue.Queue()
        self.logs = []
        self.phase = "Waiting"
        self.percent = 0.0
        self.page = "Storage"
        self.method = tk.StringVar(value="zero")
        self.passes = tk.StringVar(value="1")
        self.verify = tk.BooleanVar(value=True)
        self.status = tk.StringVar(value="Ready")
        self.root.title("Open Media Sanitizer · Media Workspace")
        self.root.geometry("1160x760")
        self.root.minsize(940, 640)
        self.root.configure(bg=BG)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self._theme()
        self._shell()
        self.root.after(50, self.poll)
        self.refresh()

    def _theme(self):
        self.root.option_add("*Font", "{DejaVu Sans} 10")
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("Treeview", background=WHITE, foreground=INK, fieldbackground=WHITE,
                        rowheight=48, borderwidth=0)
        style.configure("Treeview.Heading", background="#e4edf2", foreground=INK,
                        font=("DejaVu Sans", 10, "bold"), padding=10)
        style.map("Treeview", background=[("selected", "#d1eee8")],
                  foreground=[("selected", INK)])
        style.configure("TProgressbar", background=ACCENT, troughcolor="#dce7ed", borderwidth=0)
        style.configure("TCombobox", padding=7)

    def label(self, parent, text="", size=11, color=INK, bold=False, **kwargs):
        return tk.Label(parent, text=text, bg=parent.cget("bg"), fg=color,
                        font=("DejaVu Sans", size, "bold" if bold else "normal"), **kwargs)

    def button(self, parent, text, command, primary=False, danger=False):
        color = "#b33d40" if danger else ACCENT
        button = tk.Button(parent, text=text, command=command, bg=color if primary else WHITE,
                         fg=WHITE if primary else INK, activebackground="#d1eee8",
                         activeforeground=INK, disabledforeground="#8c9ca6", relief="flat",
                         borderwidth=0, padx=16, pady=11, cursor="hand2", takefocus=True,
                         highlightthickness=2, highlightbackground=parent.cget("bg"),
                         highlightcolor=ACCENT)
        button.bind("<Return>", lambda _event: button.invoke())
        return button

    def _shell(self):
        self.root.columnconfigure(1, weight=1)
        self.root.rowconfigure(0, weight=1)
        rail = tk.Frame(self.root, bg=NAV, width=205, padx=18, pady=28)
        rail.grid(row=0, column=0, sticky="nsew")
        rail.grid_propagate(False)
        self.label(rail, "OMS", 27, "#73dcc4", True).pack(anchor="w")
        self.label(rail, "MEDIA WORKSPACE", 9, "#a5c2d0").pack(anchor="w", pady=(0, 38))
        self.navigation = {}
        for name in ("Storage", "Activity", "Reports", "About"):
            button = tk.Button(rail, text=name, command=lambda p=name: self.show(p),
                               bg=NAV, fg="#d7e7ed", activebackground="#244658",
                               activeforeground=WHITE, relief="flat", anchor="w",
                               padx=15, pady=14, cursor="hand2", takefocus=True)
            button.pack(fill="x", pady=4)
            button.bind("<Return>", lambda _event, b=button: b.invoke())
            self.navigation[name] = button
        self.label(rail, "APACHE-2.0 OR MIT", 8, "#a5c2d0").pack(side="bottom", anchor="w")
        self.label(rail, "Local. Explicit. Reviewable.", 9, "#a5c2d0").pack(side="bottom", anchor="w", pady=12)
        area = tk.Frame(self.root, bg=BG, padx=28, pady=24)
        area.grid(row=0, column=1, sticky="nsew")
        area.columnconfigure(0, weight=1)
        area.rowconfigure(2, weight=1)
        header = tk.Frame(area, bg=BG)
        header.grid(row=0, column=0, sticky="ew")
        self.heading = self.label(header, "Storage", 25, bold=True)
        self.heading.pack(side="left")
        mode = "DEMO · DISPOSABLE FILES" if self.backend.demo else "LIVE · LOCAL DEVICES"
        self.label(header, mode, 9, ACCENT, True).pack(side="right")
        self.subtitle = self.label(area, "", 10, MUTED, anchor="w", wraplength=760, justify="left")
        self.subtitle.grid(row=1, column=0, sticky="ew", pady=(10, 20))
        self.content = tk.Frame(area, bg=BG)
        self.content.grid(row=2, column=0, sticky="nsew")
        self.label(area, textvariable=self.status, size=9, color=MUTED, anchor="w").grid(
            row=3, column=0, sticky="ew", pady=(18, 0))

    def show(self, page):
        if self.active and page in ("Storage", "Review"):
            page = "Activity"
        self.page = page
        for widget in self.content.winfo_children():
            widget.destroy()
        for name, button in self.navigation.items():
            button.configure(bg="#244658" if name == page else NAV)
        self.heading.configure(text=page)
        self.subtitle.configure(text={
            "Storage": "Choose one medium, set the write pattern, then review before anything is changed.",
            "Review": "Check the target and type its full path to authorize this operation.",
            "Activity": "Follow the current operation. Cancellation stops further writes; it does not restore data.",
            "Reports": "Session records you can export as portable JSON or a standalone HTML document.",
            "About": "An open source workspace for explicit storage-media overwrites.",
        }[page])
        {"Storage": self.storage, "Review": self.review, "Activity": self.activity,
         "Reports": self.reports, "About": self.about}[page]()

    def refresh(self):
        if self.active or self.refreshing:
            return
        self.refreshing = True
        self.selected = None
        self.status.set("Reading local device inventory…")
        self.show("Storage")

        def collect():
            try:
                self.messages.put(("inventory", self.backend.inventory()))
            except (OSError, RuntimeError, ValueError, TimeoutError) as error:
                self.messages.put(("error", str(error)))
            except Exception as error:
                self.messages.put(("error", f"Inventory failed: {error}"))

        threading.Thread(target=collect, daemon=True).start()

    def storage(self):
        top = tk.Frame(self.content, bg=BG)
        top.pack(fill="x", pady=(0, 12))
        self.label(top, f"{len(self.devices)} available media", 13, bold=True).pack(side="left")
        refresh = self.button(top, "Refresh inventory", self.refresh)
        refresh.pack(side="right")
        if self.refreshing:
            refresh.configure(state="disabled")
        table_area = tk.Frame(self.content, bg=WHITE)
        table_area.pack(fill="both", expand=True)
        columns = ("model", "size", "state")
        self.inventory_tree = ttk.Treeview(table_area, columns=columns, show="tree headings",
                                          selectmode="browse", height=5)
        for column, title, width in (("#0", "Target", 160), ("model", "Medium", 210),
                                     ("size", "Capacity", 100), ("state", "Availability", 190)):
            self.inventory_tree.heading(column, text=title)
            self.inventory_tree.column(column, width=width, minwidth=80, stretch=True)
        bar = ttk.Scrollbar(table_area, orient="vertical", command=self.inventory_tree.yview)
        self.inventory_tree.configure(yscrollcommand=bar.set)
        bar.pack(side="right", fill="y")
        self.inventory_tree.pack(fill="both", expand=True)
        for index, device in enumerate(self.devices):
            state = blocked_reason(device) or "Available for review"
            display_path = os.path.basename(device["path"]) if self.backend.demo else device["path"]
            self.inventory_tree.insert("", "end", iid=str(index), text=display_path,
                                       values=(device["model"] or "Unknown model",
                                               readable_size(device["size_bytes"]), state))
        self.inventory_tree.bind("<<TreeviewSelect>>", self.select_device)
        if not self.devices:
            text = "Scanning…" if self.refreshing else "No media found. Try Refresh, or open the demo with ./start.sh --demo."
            self.label(table_area, text, 10, MUTED, wraplength=680).pack(pady=15)
        settings = tk.Frame(self.content, bg=WHITE, padx=18, pady=18)
        settings.pack(fill="x", pady=16)
        self.label(settings, "Operation settings", 13, bold=True).grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 14))
        self.label(settings, "Pattern", 10, MUTED).grid(row=1, column=0, sticky="w")
        method = ttk.Combobox(settings, textvariable=self.method, values=("zero", "ones", "random"),
                              state="readonly", width=13)
        method.grid(row=2, column=0, sticky="w", padx=(0, 22))
        method.bind("<<ComboboxSelected>>", self.update_method)
        self.label(settings, "Passes", 10, MUTED).grid(row=1, column=1, sticky="w")
        ttk.Combobox(settings, textvariable=self.passes, values=tuple(range(1, 17)),
                     width=5, state="readonly").grid(row=2, column=1, sticky="w", padx=(0, 22))
        self.verify_control = tk.Checkbutton(settings, text="Read-back verification", variable=self.verify,
                                             bg=WHITE, fg=INK, activebackground=WHITE, selectcolor=WHITE)
        self.verify_control.grid(row=2, column=2, sticky="w")
        self.update_method()
        bottom = tk.Frame(self.content, bg=BG)
        bottom.pack(fill="x")
        self.selection_text = self.label(bottom, "Select a medium above to continue.", 10, MUTED,
                                         wraplength=470, justify="left")
        self.selection_text.pack(side="left")
        self.review_button = self.button(bottom, "Review operation →", self.prepare_review, primary=True)
        self.review_button.pack(side="right")
        self.review_button.configure(state="disabled")
        if self.selected is not None and not self.refreshing:
            for index, device in enumerate(self.devices):
                if device["identity"] == self.selected["identity"] and device["path"] == self.selected["path"]:
                    self.inventory_tree.selection_set(str(index))
                    self.select_device()
                    break

    def select_device(self, _event=None):
        choices = self.inventory_tree.selection()
        if not choices or self.refreshing:
            return
        self.selected = dict(self.devices[int(choices[0])])
        reason = blocked_reason(self.selected)
        self.selection_text.configure(text=reason or f"Selected: {self.selected['path']}")
        self.review_button.configure(state="disabled" if reason else "normal")

    def update_method(self, _event=None):
        random = self.method.get() == "random"
        if random:
            self.verify.set(False)
        self.verify_control.configure(state="disabled" if random else "normal")

    def prepare_review(self):
        if self.selected is None or self.refreshing or blocked_reason(self.selected):
            return
        try:
            self.review_settings = Settings(self.method.get(), int(self.passes.get()), self.verify.get())
            self.review_settings.validate()
        except ValueError as error:
            messagebox.showerror("Check settings", str(error), parent=self.root)
            return
        self.review_device = dict(self.selected)
        self.show("Review")

    def review(self):
        card = tk.Frame(self.content, bg=WHITE, padx=26, pady=24)
        card.pack(fill="both", expand=True)
        self.label(card, "01 / TARGET", 10, ACCENT, True).pack(anchor="w")
        self.label(card, self.review_device["path"], 16, bold=True, wraplength=730,
                   justify="left").pack(anchor="w", pady=(10, 4))
        self.label(card, f"{self.review_device['model'] or 'Unknown model'}  ·  "
                        f"{readable_size(self.review_device['size_bytes'])}", 11, MUTED).pack(anchor="w")
        self.label(card, "02 / OPERATION", 10, ACCENT, True).pack(anchor="w", pady=(26, 10))
        settings = self.review_settings
        self.label(card, f"{settings.method.capitalize()} pattern · {settings.passes} pass(es) · "
                        f"Verification {'on' if settings.verify else 'off'}", 12).pack(anchor="w")
        text = ("Demo: only this disposable sample file will be overwritten." if self.backend.demo else
                "All accessible data on this target will be overwritten. This cannot be undone.")
        self.label(card, text, 11, "#9a3a2f", wraplength=680, justify="left").pack(anchor="w", pady=24)
        self.label(card, "03 / TYPE THE COMPLETE TARGET PATH", 10, ACCENT, True).pack(anchor="w")
        self.confirmation = tk.StringVar()
        entry = tk.Entry(card, textvariable=self.confirmation, relief="solid", borderwidth=1,
                         bg=WHITE, fg=INK, font=("DejaVu Sans", 12))
        entry.pack(fill="x", ipady=9, pady=12)
        row = tk.Frame(card, bg=WHITE)
        row.pack(fill="x", pady=(16, 0))
        self.button(row, "← Back to storage", lambda: self.show("Storage")).pack(side="left")
        self.execute_button = self.button(row, "Run demo" if self.backend.demo else "Erase selected media",
                                          self.start_job, primary=True, danger=not self.backend.demo)
        self.execute_button.pack(side="right")
        self.execute_button.configure(state="disabled")
        self.confirmation.trace_add("write", lambda *_: self.execute_button.configure(
            state="normal" if self.confirmation.get() == self.review_device["path"] else "disabled"))
        entry.focus_set()

    def start_job(self):
        if self.active:
            return
        if not self.backend.demo and os.geteuid() != 0:
            messagebox.showerror("Administrator access required",
                                 "Device inspection works without root. To erase a physical device, "
                                 "close this window and launch the built app with sudo -E ./start.sh. "
                                 "Demo mode works without administrator access.", parent=self.root)
            return
        try:
            self.job = Job(self.backend, self.review_device, self.review_settings, self.confirmation.get())
        except ValueError as error:
            messagebox.showerror("Cannot start operation", str(error), parent=self.root)
            return
        self.active = True
        self.logs = []
        self.percent = 0
        self.phase = "Starting"
        self.show("Activity")
        self.job.start()

    def activity(self):
        card = tk.Frame(self.content, bg=WHITE, padx=24, pady=22)
        card.pack(fill="both", expand=True)
        self.activity_phase = self.label(card, self.phase, 19, bold=True)
        self.activity_phase.pack(anchor="w")
        target = self.job.device["path"] if self.job else "No operation has been started."
        self.label(card, target, 10, MUTED, wraplength=700, justify="left").pack(anchor="w", pady=10)
        self.progressbar = ttk.Progressbar(card, maximum=100, value=self.percent)
        self.progressbar.pack(fill="x", pady=(5, 18))
        self.log_widget = tk.Text(card, height=12, bg="#f3f7fa", fg=INK, relief="flat", wrap="word",
                                  font=("DejaVu Sans Mono", 10), padx=12, pady=12)
        self.log_widget.pack(fill="both", expand=True)
        self.log_widget.insert("end", "\n".join(self.logs[-300:]))
        self.log_widget.configure(state="disabled")
        self.cancel_button = self.button(card, "Cancel operation" if self.active else "View reports",
                                          self.cancel_job if self.active else lambda: self.show("Reports"))
        self.cancel_button.pack(anchor="e", pady=(18, 0))

    def cancel_job(self):
        if self.active and self.job:
            self.job.cancel()
            self.status.set("Cancellation requested. Waiting for the backend to flush and close the target…")
            if self.page == "Activity":
                self.cancel_button.configure(state="disabled")

    def reports(self):
        card = tk.Frame(self.content, bg=WHITE, padx=24, pady=22)
        card.pack(fill="both", expand=True)
        self.label(card, "Operation records", 17, bold=True).pack(anchor="w", pady=(0, 14))
        if not self.records:
            self.label(card, "Completed, failed, and cancelled operations appear here.\n"
                             "Run a demo to create your first record.", 12, MUTED, justify="left").pack(anchor="w", pady=30)
            return
        tree = ttk.Treeview(card, columns=("outcome", "mode", "path"), show="headings", selectmode="browse")
        for name, width in (("outcome", 130), ("mode", 75), ("path", 440)):
            tree.heading(name, text=name.capitalize())
            tree.column(name, width=width, minwidth=70)
        for index, record in enumerate(self.records):
            tree.insert("", "end", iid=str(index), values=(record["outcome"].capitalize(),
                                                           record["mode"], record["target"]["path"]))
        tree.pack(fill="both", expand=True)
        tree.selection_set(str(len(self.records) - 1))
        row = tk.Frame(card, bg=WHITE)
        row.pack(fill="x", pady=(18, 0))
        for format_name in ("html", "json"):
            self.button(row, f"Export {format_name.upper()}",
                        lambda f=format_name: self.export_selected(tree, f)).pack(side="right", padx=5)
        self.label(card, "Records stay in memory until you export them. HTML reports can be printed to PDF.",
                   9, MUTED, wraplength=700, justify="left").pack(anchor="w", pady=(15, 0))

    def export_selected(self, tree, format_name):
        selection = tree.selection()
        if not selection:
            self.status.set("Select an operation record before exporting.")
            return
        self.export(self.records[int(selection[0])], format_name)

    def export(self, record, format_name):
        path = filedialog.asksaveasfilename(
            parent=self.root, title="Export operation record", defaultextension=f".{format_name}",
            initialfile=f"media-operation-{record['id'][:8]}.{format_name}",
            filetypes=[(format_name.upper(), f"*.{format_name}")], confirmoverwrite=True,
        )
        if path:
            try:
                save_report(record, path, format_name)
                self.status.set(f"Report saved to {path}")
            except (OSError, ValueError) as error:
                messagebox.showerror("Report could not be saved", str(error), parent=self.root)

    def about(self):
        card = tk.Frame(self.content, bg=WHITE, padx=25, pady=25)
        card.pack(fill="both", expand=True)
        self.label(card, "Open Media Sanitizer", 22, bold=True).pack(anchor="w")
        self.label(card, "Version 0.2.0", 11, MUTED).pack(anchor="w", pady=10)
        text = ("A desktop workflow backed by a small Linux C utility. Device selection, review, "
                "execution, and reporting are separate steps.\n\n"
                "Writes cover the logical address range exposed by the device. Firmware sanitize, "
                "cryptographic erase, and access to hidden or remapped sectors are not implemented.\n\n"
                "Demo mode runs the same write and verification code on disposable files.\n\n"
                "The project code is available under Apache-2.0 OR MIT, at your option. "
                "Python and Tk are system-provided dependencies under their own licenses. "
                "All interface elements are drawn with native widgets; no image or font assets are bundled.")
        self.label(card, text, 12, MUTED, wraplength=680, justify="left").pack(anchor="w", pady=15)
        self.button(card, "Read license texts", self.licenses).pack(anchor="w", pady=15)

    def licenses(self):
        window = tk.Toplevel(self.root)
        window.title("Licenses · Open Media Sanitizer")
        window.geometry("760x540")
        text = tk.Text(window, wrap="word", padx=20, pady=20)
        scroll = ttk.Scrollbar(window, command=text.yview)
        text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        text.pack(fill="both", expand=True)
        for name in ("LICENSE", "LICENSE-MIT", "LICENSE-APACHE"):
            text.insert("end", (ROOT / name).read_text(encoding="utf-8") + "\n\n")
        text.configure(state="disabled")

    def poll(self):
        try:
            while True:
                kind, payload = self.messages.get_nowait()
                self.refreshing = False
                if kind == "inventory":
                    self.devices, notice = payload
                    self.status.set(notice or "Inventory refreshed. No data has been changed.")
                else:
                    self.devices = []
                    self.status.set(payload)
                if self.page == "Storage":
                    self.show("Storage")
        except queue.Empty:
            pass
        if self.job:
            try:
                while True:
                    kind, payload = self.job.events.get_nowait()
                    if kind == "progress":
                        self.percent = 100 * payload["completed"] / max(payload["total"], 1)
                        self.phase = f"{payload['phase'].capitalize()} · pass {payload['pass']} of {payload['passes']} · {self.percent:.0f}%"
                        if self.page == "Activity":
                            self.activity_phase.configure(text=self.phase)
                            self.progressbar.configure(value=self.percent)
                    elif kind == "log":
                        self.logs.append(payload)
                        self.logs = self.logs[-300:]
                        if self.page == "Activity":
                            self.log_widget.configure(state="normal")
                            self.log_widget.insert("end", payload + "\n")
                            self.log_widget.see("end")
                            self.log_widget.configure(state="disabled")
                    elif kind == "finished":
                        self.active = False
                        self.records.append(payload)
                        self.phase = payload["outcome"].capitalize()
                        self.status.set(f"Operation {payload['outcome']}. Open Reports to export the record.")
                        if self.close_pending:
                            self.shutdown()
                            return
                        self.show("Activity")
            except queue.Empty:
                pass
        self.root.after(75, self.poll)

    def close(self):
        if self.active:
            if messagebox.askyesno("Operation in progress", "Cancel this operation and close after it stops?", parent=self.root):
                self.close_pending = True
                self.cancel_job()
            return
        self.shutdown()

    def shutdown(self):
        self.backend.close()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Open the Media Workspace desktop application.")
    parser.add_argument("--demo", action="store_true", help="use disposable sample files instead of physical devices")
    arguments = parser.parse_args()
    backend = Backend(demo=arguments.demo)
    try:
        root = tk.Tk()
    except tk.TclError as error:
        backend.close()
        print(f"Cannot open the desktop window: {error}\nRun from a graphical desktop, "
              "or use ./build/oms --help for the CLI.", file=sys.stderr)
        return 1
    app = None
    def interrupt(_number, _frame):
        raise KeyboardInterrupt

    previous_terminate = signal.signal(signal.SIGTERM, interrupt)
    try:
        app = Workspace(root, backend)
        root.mainloop()
    except KeyboardInterrupt:
        pass
    finally:
        if app is not None and app.job is not None and app.job.thread.is_alive():
            app.job.cancel()
            app.job.thread.join()
        backend.close()
        signal.signal(signal.SIGTERM, previous_terminate)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
