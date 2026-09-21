# Developer guide

[Documentation index](README.md) · [Contribution guidelines](../CONTRIBUTING.md)

This guide is for C developers, testers, and documentation contributors. The
application and regression test logic use C11. Make and shell scripts coordinate
builds, fixtures, and boot sessions. The CLI uses libc and Linux interfaces;
the desktop additionally uses GTK 3, GLib/GIO, and JSON-GLib.

## Get a checkout

Repository access is required while the project is private. With Git and GitHub
SSH authentication configured:

```sh
git clone git@github.com:Mister-M-alt/open-media-sanitizer.git
cd open-media-sanitizer
git switch -c my-change
```

Install the [Linux development dependencies](../README.md#run-on-an-existing-linux-desktop).
For graphical tests, also install Xvfb and xauth. On Debian/Ubuntu:

```sh
sudo apt install xvfb xauth
```

Build and test as your normal user. Neither demo mode nor the automated test
suite requires access to a physical disk.

## Build and run

```sh
make
./start.sh --demo
make test
make test-gui
```

`make` creates `build/oms` and `build/oms-gui`. `make cli` builds just the backend;
`make demo` and `make run` build and launch the demo or live desktop. The launcher
builds as a regular user but only launches existing binaries when run as root.
GUI and backend must remain in the same directory: the GUI resolves `oms` beside
its own executable, rather than searching `PATH`.

Install after building with `sudo make install`; the default destination is
`/usr/local/bin`. A package builder can stage without installing on the host:

```sh
make DESTDIR="$PWD/build/staging" PREFIX=/usr install
```

The Makefile supports `CC`, `CFLAGS`, `CPPFLAGS`, `LDFLAGS`, `PKG_CONFIG`, `PREFIX`,
and `DESTDIR`. It enables strict warnings and 64-bit file offsets, including on
32-bit targets. Use `make clean` when changing compiler or sanitizer flags;
Make does not track changed flag values as file dependencies.

## Source map

| Path | Responsibility |
| --- | --- |
| `src/main.c` | CLI dispatch, argument validation, usage, and exit codes |
| `src/device.c` | Device/file inspection, usage checks, identity, inventory, and JSON output |
| `src/erase.c` | Confirmation, revalidation, exclusive open, writes, read-back, cancellation, and cleanup |
| `src/workspace.c` | Inventory subprocess deadlines, job creation, demo files, report rendering/export |
| `src/gui.c` | GTK pages, confirmation, asynchronous output, job completion, and session history |
| `include/oms.h` | Backend types and functions |
| `include/workspace.h` | Desktop/workspace types and functions |
| `tests/` | Native tests, syscall wrappers, shell fixture orchestration, and boot-screen checker |
| `images/` | Buildroot external tree, target profiles, runtime overlay, image/source packaging, and QEMU checks |
| `.github/workflows/` | Native CI and manually triggered image builds |

The desktop asks the backend for JSON inventory, then launches one erase
subprocess with an explicit path and expected identity. It drains output,
waits for process exit, and creates a report. Inventory queries have a 30-second
deadline; this is separate from the lifetime of an erase operation.

The backend owns the final decision to write. UI validation alone is not a
substitute for checks against the opened target. Keep inspection and execution
bound to the same identity, size, and device instance when the kernel exposes
that information.

## Validation by change type

| Change | Relevant validation |
| --- | --- |
| CLI options, usage checks, or write behavior | `make test`; add a regression using regular files or fixtures for changed behavior |
| Desktop selection, confirmation, job lifecycle, or reports | `make test` and `make test-gui` |
| Kernel, boot overlay, runtime packages, or image scripts | Build affected profiles and run their QEMU smoke tests |
| Documentation only | Check links, commands, defaults, and UI labels against source/help; compilation is unnecessary |

`make test` covers CLI behavior, native reports/workspace logic, inspection
timeouts, simulated device relationships, file guards, patterns, and injected
I/O failures. `make test-gui` runs a headless Xvfb desktop test, including a real
write/read-back on demo files and cancellation. Test-only probes and fault
injection binaries are excluded from installation.

For memory/undefined-behavior checks on the native suite:

```sh
make clean
make CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' test
make clean
make
```

Automated tests must never write to block devices. Exercise destructive paths
with newly created, disposable regular files; model system usage with fixtures.
The normal application provides no runtime switch to redirect inspection to
fixture paths. See the [review notes](review.md) for the corrected issues and
remaining test limits.

## Images and architecture work

Cross-build through the pinned Buildroot setup rather than guessing compiler
flags. See [boot image builds](boot-images.md#build). Its upstream host tooling
requires Python, but the application's source and the boot runtime do not.
Dependency implementations include languages other than C; the project does
not claim that the complete Linux distribution is exclusively C.

The [maintainer guide](maintenance.md) explains source collection, firmware
profiles, and validation evidence. A passing compile alone does not demonstrate
boot or driver support. ARM32 and ARM64 are separate profiles and binaries.

## Submit a change

Keep changes focused and describe the user-visible behavior. Include the
commands you ran, their results, and anything not tested. Flag any change to
target selection, confirmation, usage checks, writes, or verification in the
pull request description. Do not commit `build/`, `.image-cache/`, image outputs,
reports from real devices, or credentials.

For documentation, put the shortest starting path in the root README and detail
in an audience-specific guide. Define unfamiliar terms, distinguish demo from
live commands, state whether an example writes data, and link back to the index.
Test executable examples only on temporary files. Keep UI labels, CLI defaults,
and reported test coverage consistent with the implementation.

Follow [CONTRIBUTING.md](../CONTRIBUTING.md) for contribution licensing and
[SECURITY.md](../SECURITY.md) for private vulnerability reports.
