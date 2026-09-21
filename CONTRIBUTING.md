# Contributing

Thank you for helping improve Open Media Sanitizer.

## Development workflow

1. Create a focused branch from `main`.
2. Build with `make`.
3. Run `make test` before submitting a pull request.
4. Explain any change that affects target selection, confirmation, write behavior, or verification.

The desktop app and report generation use C, GTK 3, and JSON-GLib. Run `make test-gui`
with the development packages, Xvfb, and xauth installed to exercise selection, confirmation,
navigation, and a real write-and-verify operation on disposable demo files.

`make test` includes fixture-based device checks and injected I/O failures.
The fault-injection executable and fixture probe are test-only binaries; they
are never installed or used by the desktop app.

Changes to destructive behavior should include a test that uses a temporary regular file. Automated tests must never write to block devices.

Keep the core dependency-free where practical. New dependencies should have a clear maintenance or safety benefit and a license compatible with this project's dual-license terms.

## Licensing contributions

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in this project is licensed under either Apache-2.0 or MIT, at the recipient's option, without additional terms or conditions.
