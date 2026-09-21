# Open Media Sanitizer

Open Media Sanitizer is a native C11 application for inspecting storage, reviewing
an erase operation, following its progress, and exporting a record. Run it on a
Linux desktop or boot a complete standalone system from USB without an installed
operating system or a network service.

The GTK desktop (`oms-gui`), command-line backend (`oms`), report generator, and
regression tests are written in C. The boot images contain no Python interpreter,
Python scripts, or bytecode.

The project is at an early stage. Use it only after reviewing the source and testing the workflow in an environment where data loss is acceptable.

## Start here

| What you want to do | Where to start |
| --- | --- |
| Understand the project and its terminology | [Documentation index and glossary](docs/README.md) |
| Try the interface for the first time | [Demo walkthrough](docs/operator-guide.md#try-the-demo-first) |
| Boot a computer from USB | [Download and boot](#download-and-boot), then [the operator guide](docs/operator-guide.md) |
| Perform and record an operation | [Operator guide](docs/operator-guide.md) and [reports](docs/reports.md) |
| Use a terminal or integrate the backend | [CLI reference](docs/cli-reference.md) |
| Diagnose a problem | [Troubleshooting](docs/troubleshooting.md) |
| Build, test, or contribute C code | [Developer guide](docs/development.md) and [contributing](CONTRIBUTING.md) |
| Build images, validate hardware, or distribute a release | [Boot images](docs/boot-images.md) and [maintainer guide](docs/maintenance.md) |

![Media Workspace showing disposable demo media](docs/workspace.png)

## Download and boot

Download the images, `SHA256SUMS`, and matching source bundles from the
[0.3.0 preview release](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1).
The repository and release are currently **private**; GitHub access is required.

Choose the image for your CPU and firmware:

| Image | CPU target | Firmware tested in QEMU |
| --- | --- | --- |
| `oms-x86_32.img.xz` | i686, 32-bit x86 | Legacy BIOS |
| `oms-x86_64.img.xz` | 64-bit x86 | Legacy BIOS and x64 UEFI |
| `oms-arm32.img.xz` | ARMv7-A Cortex-A15, hard-float | ARM32 UEFI, QEMU `virt` |
| `oms-arm64.img.xz` | ARMv8-A Cortex-A53 | AArch64 UEFI, QEMU `virt` |

Each image passed graphical startup, mounted boot-drive detection, and a native
write/read-back test on a temporary file in RAM. Physical hardware and IA32 UEFI
have not been validated. ARM boards require matching firmware, device trees, and
drivers; these generic UEFI images do not support every ARM board. Secure Boot
signing is not included.

1. Download the chosen `.img.xz` and `SHA256SUMS` into the same directory.
2. Verify the downloaded files, then decompress the image. For example, on Linux:

   ```sh
   sha256sum --ignore-missing -c SHA256SUMS
   xz -dk oms-x86_64.img.xz
   ```

3. Use an image-writing utility to write the `.img` to a disposable USB drive,
   replacing that drive's contents. Select the USB drive in the firmware boot menu.

Starting the workspace does not erase a device. A separate demonstration boot
entry uses disposable files. Session state and reports live in RAM; export
reports to separately mounted storage before shutting down.

See the [boot image guide](docs/boot-images.md) for firmware requirements, report
export, recovery access, image builds, and dependency source bundles.

## Run on an existing Linux desktop

Install the system dependencies once:

```sh
# Debian / Ubuntu
sudo apt install build-essential pkg-config libgtk-3-dev libjson-glib-dev librsvg2-common shared-mime-info adwaita-icon-theme

# Arch Linux
sudo pacman -S --needed base-devel pkgconf gtk3 json-glib

# Fedora
sudo dnf install gcc make pkgconf-pkg-config gtk3-devel json-glib-devel
```

From this checkout, open a working demonstration:

```sh
./start.sh --demo
```

The launcher builds both C executables and opens the desktop window. No pip, npm,
account, network service, or application installation is needed. Demo mode
creates two disposable files and runs the real write and read-back verification
code only on those files. They are removed when the app closes.

To inspect real devices, open `./start.sh`. Choose **Storage → select a medium →
Review operation**, then type its complete path. Physical erasure requires root:
after building as your regular user, launch `sudo -E ./start.sh` from a trusted
graphical desktop session. The launcher never elevates itself or builds as root.
If your desktop does not permit root windows, use the CLI below for execution.

**Activity** shows write and verification progress and supports cancellation.
**Reports** exports completed, failed, and cancelled operation records as JSON
or a standalone HTML page that can also be printed to PDF. Records include the
target, timing, settings, outcome, and verification result. Demo records are
explicitly marked. Records are kept in memory until exported.

Keyboard users can navigate with Tab and activate controls with Space/Enter.
If no graphical display is available, the launcher prints an explanation and
the CLI remains available. The current interface is in English.

See the [review notes](docs/review.md) for corrected issues, test coverage, and limits.

## Safety model

- There is no command that erases every detected device.
- `oms erase` is a dry run unless `--execute` is present.
- Execution requires the canonical target path to be typed interactively or supplied with `--confirm`.
- Mounted devices, active swap devices, read-only devices, and devices with active holders are rejected.
- The target is inspected again after confirmation and checked again after opening.
- The desktop binds confirmation to the inspected device identity and size.
- Missing or malformed usage information blocks execution.
- Block-device writes require an exclusive open; regular-file tests use an advisory lock.
- Test files with multiple hard links, active swap, or loop-device attachments are rejected.
- Regular files are rejected unless `--allow-file` is supplied for controlled testing.
- Block-device execution requires root privileges.

These checks reduce operator error, but they cannot make a destructive command risk-free. Confirm the device identity, size, model, and mount state independently before executing an erase.

## Build and test

The backend requires a C11 compiler, GNU Make, and Linux kernel headers. The
desktop and C tests also require GTK 3 and JSON-GLib development packages.
Use `make cli` to build only the backend without graphical dependencies.

Make and shell scripts handle building and launching. Building the application
does not require Python. Building the complete Linux boot images uses upstream
tools that require Python **on the build host only**; it is excluded from the
runtime images. Follow the [image build instructions](docs/boot-images.md#build)
to build any of the four targets locally or through the manual GitHub
**Bootable images** workflow.

```sh
make
make test
```

For graphical tests, install `xvfb` and `xauth`, then run `make test-gui`.
The suite uses disposable files and simulated sysfs/proc data. It never writes
to a physical block device.

Install both executables as `/usr/local/bin/oms` and `/usr/local/bin/oms-gui`:

```sh
sudo make install
```

## Usage

The examples below use `oms` after installation. From a source checkout, use
`./build/oms` instead. The [CLI reference](docs/cli-reference.md) covers every
option, defaults, JSON inventory, exit codes, and progress events.

List visible whole block devices:

```sh
oms list
```

Inspect one target without writing to it:

```sh
oms inspect /dev/sdX
```

Review an erase plan. This command does not write data:

```sh
sudo oms erase /dev/sdX --method zero --verify
```

Execute the reviewed plan with interactive confirmation:

```sh
sudo oms erase /dev/sdX --method zero --verify --execute
```

For a non-interactive environment, `--confirm` must exactly match the canonical path printed by `inspect`:

```sh
sudo oms erase /dev/sdX --method zero --verify --execute --confirm /dev/sdX
```

Regular-file operation exists so the complete write path can be tested without a block device:

```sh
truncate -s 64M sample.img
oms erase sample.img --allow-file --method zero
oms erase sample.img --allow-file --method zero --verify --execute \
  --confirm "$(readlink -f sample.img)"
```

Run `oms --help` for all options.

## Methods

| Method | Written bytes | Read-back verification |
| --- | --- | --- |
| `zero` | `0x00` | Available |
| `ones` | `0xff` | Available |
| `random` | Bytes from `/dev/urandom` | Not available |

One pass is the default. Up to 16 passes can be requested, although additional host writes are not automatically more effective for every storage technology.

## Scope and limitations

`oms` performs sequential writes through the operating system to the logical address range exposed by a block device. It does not issue ATA Secure Erase, NVMe Sanitize, SCSI sanitize, cryptographic key-destruction, or vendor-specific firmware commands. It cannot directly overwrite remapped sectors, reserved capacity, overprovisioned flash, or inaccessible regions.

Read-back verification checks that the requested byte pattern can be read from the exposed logical range after the final pass. It is an integrity check for that operation, not a certification of sanitization or regulatory compliance.

Usage checks inspect the current Linux mount namespace. Run physical erasure on
the host, not inside a container with a partial device view. File locks are
advisory; `--allow-file` is for controlled tests, not concurrent file shredding.
Verification requests cache eviction but cannot guarantee that every read comes
from physical media. Cancelling an operation leaves a partially overwritten
target. Physical hardware behavior has not been certified by the test suite.

The current implementation supports Linux. Device inventory intentionally omits loop, RAM, and zram devices, while an explicitly named regular file can be used with `--allow-file`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security-sensitive reports should follow [SECURITY.md](SECURITY.md).

Documentation improvements are welcome. Start with the
[developer guide](docs/development.md) for the source layout and validation
workflow; no code changes are needed to contribute a clearer explanation.

## License

Licensed under either of the following, at your option:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT License ([LICENSE-MIT](LICENSE-MIT))

Unless you explicitly state otherwise, contributions intentionally submitted for inclusion in this project are licensed under the same terms.

System components such as GTK and Linux retain their own licenses. Distributed
boot images include those components; keep their matching source bundles and
notices alongside the images, as described in [the image guide](docs/boot-images.md).
