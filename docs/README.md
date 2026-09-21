# Documentation

[Project overview](../README.md) · [Download preview](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1)

Open Media Sanitizer writes a selected pattern across the storage that Linux
exposes for one target. It has a graphical workspace and a command-line backend.
The bootable edition includes Linux, so it can run without an installed OS.

These guides describe version 0.3.0 and the current `main` branch. For a released
image, use the documentation at its release tag and retain its matching source
bundle. Access to this repository and its downloads currently requires a GitHub
account with permission to the private repository.

## Choose a starting point

| Reader | Suggested path | What you will learn |
| --- | --- | --- |
| First-time user | [Operator guide](operator-guide.md) → demo | How to select a sample, confirm a write, and save a record without choosing real hardware |
| Technician or operator | [Operator guide](operator-guide.md) → [boot images](boot-images.md) → [reports](reports.md) | How to prepare, operate, cancel, export, and shut down |
| System administrator or integrator | [CLI reference](cli-reference.md) → [troubleshooting](troubleshooting.md) | Exact options, output, status codes, and diagnostic steps |
| Reviewer or person receiving reports | [Reports](reports.md) → [review notes](review.md) | What a result means, what was tested, and what the record cannot prove |
| C developer or contributor | [Developer guide](development.md) → [contributing](../CONTRIBUTING.md) | Dependencies, source structure, tests, and change expectations |
| Image builder or release maintainer | [Boot images](boot-images.md) → [maintainer guide](maintenance.md) | Target profiles, build artifacts, validation, sources, and release preparation |

## What is included

- A native C11 GTK desktop, C backend, C report generator, and C regression tests.
- Separate boot images for x86 32-bit, x86 64-bit, ARM32, and ARM64.
- Zero, ones, and random write patterns; zero/ones can be read back for verification.
- Explicit target confirmation, usage checks, cancellation, and JSON/HTML export.

The application runs on Linux. Windows and macOS do not have native application
builds. Booting a compatible machine from the standalone image is independent
of its installed OS. ARM hardware requires the firmware and drivers described
in the [platform guide](boot-images.md); CPU bitness alone is not sufficient.

The application has no Python dependency. Make and shell scripts handle build
and boot orchestration. Some upstream image-building tools use Python on the
build host; the resulting runtime image excludes it. Third-party native
libraries retain their upstream implementations.

## Results and limits

A completed operation means the requested logical writes succeeded and, when
requested, the final zero/ones pattern passed read-back verification. It does
not establish that hidden or remapped storage was sanitized. There are no ATA
Secure Erase, NVMe Sanitize, cryptographic erase, or certification features.

Automated tests use disposable files and simulated system information. The four
image profiles have been tested in QEMU, with the firmware combinations listed
in the [boot image guide](boot-images.md). Physical erasure behavior has not been
validated by those tests. Cancellation cannot undo writes already made.

## Terms used in these guides

| Term | Meaning here |
| --- | --- |
| Target / medium | The one device or test file selected for an operation |
| Block device | A Linux storage device, such as `/dev/sdb` or `/dev/nvme0n1` |
| Partition | A region of a disk; a whole-disk operation also overwrites its partition table |
| Canonical path | The resolved path shown by inspection, which confirmation must match exactly |
| Mounted | A filesystem is attached to the running OS and may be in use |
| Holder | Another Linux block device depends on this one, for example through a storage mapping |
| Root | The Linux administrator account required for real block-device writes |
| Dry run | Inspection and a proposed plan, without writing the erase pattern |
| Pass | One complete write across the target's exposed logical range |
| Read-back verification | Reading the final zero/ones pattern and checking every returned byte |
| Image | A complete disk layout stored in a file; `.img.xz` is its compressed form |
| Firmware / UEFI / BIOS | The startup software that loads the boot image before Linux starts |
| Source bundle | Application and dependency source, notices, configuration, and build recipes accompanying an image |
| MiB / GiB | Binary size units: 1 MiB is 1,048,576 bytes; 1 GiB is 1,024 MiB |

## Getting help and contributing

Start with [troubleshooting](troubleshooting.md) and include the requested
diagnostics when reporting a problem. Use [the security policy](../SECURITY.md)
for vulnerabilities. There is no promised support response time during the
preview stage.

The interface and documentation are currently in English. Keyboard operation
is described in the operator guide; assistive-technology compatibility has not
been formally audited. Contributions to documentation and accessibility are
welcome under the [contribution guidelines](../CONTRIBUTING.md).
