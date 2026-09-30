# Changelog

Release entries describe shipped application behavior. Documentation on `main`
can be newer than the source snapshot bundled with an image.

## Unreleased

- Document public downloads and source access, USB preparation, and report export
  to a second drive.
- Add media-suitability guidance, the preview validation record, and report
  examples with a machine-readable schema.
- List free and paid alternatives, including ShredOS/nwipe, RedKey USB, Parted
  Magic, and Blancco Drive Eraser.

## 0.3.0-preview.1 — 2026-09-21

[Release downloads](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1)
· [Source commit](https://github.com/Mister-M-alt/open-media-sanitizer/tree/171adee0096065fde3ecd9d894e777d0bc051b16)

- Provide a native C CLI and GTK desktop for inventory, review, one-target
  operations, progress, cancellation, and report export.
- Support zero, ones, and random writes, with read-back for zero and ones.
- Require explicit execution and target confirmation, with usage and identity
  checks before writing.
- Export JSON and printable HTML records, including failed and cancelled
  attempts; provide a demo using disposable files.
- Build standalone x86-32, x86-64, ARM32, and ARM64 images with matching source
  bundles and dependency notices. The runtime contains no Python interpreter.
- Validate native behavior with automated tests and boot profiles in QEMU, as
  detailed in the [validation record](docs/validation.md).

This is a preview with no validated physical-hardware matrix, firmware sanitize
commands, or certification claim. See [scope and media support](docs/scope.md).
