# Scope and media support

[Documentation index](README.md) · [Operator guide](operator-guide.md) · [Alternatives](../README.md#alternatives)

Open Media Sanitizer is a free, open-source Linux application for reviewing and
performing a host overwrite of one target at a time. It provides a native desktop,
a CLI, and standalone boot images. The current release is a preview; the
[validation record](validation.md) distinguishes tested behavior from hardware
support that has not been established.

## Current capabilities

| Area | Available in this preview |
| --- | --- |
| Inventory | Whole Linux block devices, identity and size, and current usage flags |
| Operation | One selected target, `zero`, `ones`, or `random`, with 1–16 write passes |
| Verification | Optional read-back after the final zero or ones pass |
| Review | Dry-run CLI plans, desktop review, exact-path confirmation, identity rechecks |
| Progress | Writing and verification progress, elapsed time, and cancellation |
| Records | Desktop records for completed, failed, and cancelled attempts; JSON and printable HTML export |
| Demonstration | Disposable regular files using the same write and read-back code |
| Boot media | Separate x86-32, x86-64, ARM32, and ARM64 images with the firmware limits in the [boot guide](boot-images.md) |

The CLI defaults to a dry run and does not enable verification unless `--verify`
is supplied. The desktop enables verification for its default zero-pattern
operation. Neither interface supports read-back verification of random writes.

## Media suitability

Device visibility means Linux exposes an addressable target. It does not establish
that a host overwrite reaches every place where that device stores data.

| Medium or environment | What this implementation can do | Limit to consider |
| --- | --- | --- |
| Magnetic hard disk exposed as a Linux block device | Write the exposed logical range and optionally read back a fixed pattern | Remapped sectors and inaccessible regions are outside that range |
| SATA or NVMe SSD | Write logical blocks exposed by the controller | Wear levelling, remapping, and overprovisioning can retain data outside the written range; no firmware sanitize or cryptographic erase command is issued |
| USB flash drive, SD card, or eMMC | Write the logical device when its Linux driver exposes it and usage checks pass | Flash translation and reserved capacity have the same limits; repeated passes do not establish full flash coverage |
| USB bridge or RAID logical volume | Operate on the device presented by the bridge or controller | The application does not dismantle arrays, identify every underlying member, or establish coverage behind the controller |
| Damaged, read-only, or intermittently connected device | Inspect what is accessible and reject or report failures | An incomplete operation is not successful erasure; unreadable or unwritable areas remain unresolved |
| Regular file | Exercise the write path with `--allow-file`, or through demo mode | This is a controlled test, not file shredding; it does not cover snapshots, copies, filesystem history, or the enclosing device |
| Container or virtual machine | Run demonstrations on isolated files | A partial host device view can make usage checks incomplete; use the host or standalone boot system for physical-device work |

Physical-device behavior has not been validated for this preview. Select an
erasure method appropriate to the particular device and required assurance;
the [alternative tools](../README.md#alternatives) include different methods
and reporting workflows.

## Features outside the current release

There is no ATA Secure Erase, NVMe Sanitize, SCSI sanitize, cryptographic erase,
hidden-area handling, or vendor-specific firmware erasure. There is also no
multi-device batch scheduler, remote management service, automatic report export,
persistent report database, report import, native PDF generation, or digitally
signed certificate. HTML can be printed to PDF in a separate browser.

The project does not claim certification or compliance with a sanitization
standard. A completed operation records the result of the implemented logical
overwrite; [report interpretation](reports.md#what-a-report-establishes) explains
what its verification result establishes.

These are present limitations, not promises of future features. Suggestions and
hardware-validation contributions can be discussed through
[the contribution process](../CONTRIBUTING.md).

## Use and licensing

The application requires no account, activation, subscription, or per-erasure
payment. It runs locally without an application network service. Downloading
releases or building dependencies requires network access unless the required
files have already been obtained.

Project code is available under MIT OR Apache-2.0. The boot images include other
software under its own licenses; their matching source bundles and notices are
part of the [release distribution](maintenance.md#collect-corresponding-sources).
