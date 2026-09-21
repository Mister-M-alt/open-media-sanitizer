# Code review and validation

This review covers version 0.3.0 of the C backend and the C desktop app.

## Corrected behavior

| Area | Correction | Regression coverage |
| --- | --- | --- |
| Device usage | Missing, unreadable, or malformed system information blocks erasure. Parent/partition overlap, mapper dependencies, and holders are checked. | Simulated mount, sysfs, and dependency fixtures |
| File targets | Active swap files, loop backing files, multiple hard links, and conflicting locks are rejected. | Escaped-path fixtures and temporary files |
| Confirmation | GUI confirmation includes an expected identity. Identity and size are checked again before writing, including the opened descriptor. Device sequence is checked when the kernel provides it. | Stale identity and injected descriptor-size change |
| Writes | Short writes and EINTR are handled; zero progress, read EOF, verification mismatch, flush failure, close failure, and cancellation cannot report success. | Injected syscall outcomes using a test-only executable |
| Progress | Percentage calculations avoid integer multiplication overflow. Explicit progress events feed the desktop app. | Multi-pass writes across a partial final buffer |
| Inventory | Device details come from sysfs, so unprivileged users can see devices without opening them for I/O. Skipped entries produce a diagnostic. | Read-only manual inventory check and desktop demo |
| Application | A single supervised subprocess performs writes. Confirmation is required; demo mode only permits its own files. GLib asynchronously drains progress and waits for exit before reporting completion. | C application tests and Xvfb desktop tests |
| Reports | JSON and script-free HTML include outcomes and verification status. Untrusted text is escaped and exports use atomic replacement. | Report parsing, escaping, file permissions, and destination validation |

## Remaining limits

- The tests never erase a physical device. Hardware, driver, firmware, and storage-controller behavior require separate validation on expendable hardware.
- Usage inspection sees the current mount namespace. The program is intended to run on the host.
- Regular-file locks are advisory. File mode is for controlled tests.
- Firmware sanitization, cryptographic erasure, hidden areas, and remapped sectors are outside this implementation.
- Read-back checks operate through the OS and device caches; they are not proof of physical sanitization.
- The native desktop requires Linux, GTK 3, JSON-GLib, and a graphical session. Report history is session-local unless exported.

## Commands

```sh
make test
make test-gui
make clean
make CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Tests and syscall wrappers are excluded from the installed CLI. The normal
application offers no runtime switch to override the system-data paths used by
the fixture tests.
