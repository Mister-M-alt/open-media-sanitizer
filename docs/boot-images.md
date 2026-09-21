# Standalone boot media

[Documentation index](README.md) · [Operator guide](operator-guide.md) · [Maintainer guide](maintenance.md)

The image build produces a complete Linux system with the native C desktop and
erase backend. It starts locally from USB or a virtual disk without an installed
operating system, an account, a browser, or a network service.

Download the images, checksums, and matching source bundles from the
[private 0.3.0 preview release](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1).
Repository access is required while the project remains private.

There are four separate images, not a universal image:

| Target | CPU baseline | Firmware profile |
| --- | --- | --- |
| `x86_32` | i686, 32-bit x86 | Legacy BIOS or IA32 UEFI |
| `x86_64` | Baseline x86-64 | Legacy BIOS or x64 UEFI |
| `arm32` | ARMv7-A Cortex-A15, hard-float | 32-bit ARM UEFI, QEMU `virt` reference platform |
| `arm64` | ARMv8-A Cortex-A53 | AArch64 UEFI, QEMU `virt` reference platform |

ARM boards need matching firmware, device trees, and drivers. These generic UEFI
profiles do not imply support for every Raspberry Pi, phone, or development
board. Supply the physical board model before treating an ARM image as validated
for that machine. Secure Boot signing is not included. A graphical display and
keyboard are required; a mouse is optional.

## Build

The build host is x86-64 Linux. Use a local filesystem with at least 30 GiB free
per architecture, additional space for source bundles, and preferably 8 GiB RAM.
The first build downloads toolchains and source archives and can take a long time.

```sh
./images/build-container.sh x86_64
./images/build-container.sh x86_32
./images/build-container.sh arm32
./images/build-container.sh arm64
```

The wrapper requires Docker access. It builds in a container as your current
user. Set `JOBS=4` on smaller hosts. For an Ubuntu 24.04 host with the packages
listed in [the Dockerfile](../images/Dockerfile), use `./images/build.sh TARGET`
directly. Buildroot 2025.02.18 is pinned by SHA-256; package versions and source
hashes come from that release. The container base and Ubuntu build-tool packages
receive updates, so bit-for-bit reproducibility is not asserted.

Application code and regression tests are C. Make and shell scripts orchestrate
the builds and boot session. Some upstream build tools require host Python;
Python is not installed in the generated system and the application has no
Python dependency. The post-build check rejects a Python interpreter, source,
bytecode, or Python executable script in the runtime image. Third-party native
libraries retain their upstream implementations and licenses.

Outputs are under `image-output/artifacts/TARGET/`:

- `oms-TARGET.img.xz`: compressed raw disk image.
- `SHA256SUMS`: checksums.
- `buildroot.config`: resolved configuration.

After a build, produce dependency notices and corresponding source archives:

```sh
# Inside the same build environment:
./images/package-sources.sh x86_64
```

The GitHub **Bootable images** workflow performs both steps and stores the result
as a private workflow artifact. It runs manually so ordinary code pushes do not
trigger four large system builds.

For a boot smoke test, install QEMU, the matching UEFI firmware (except for x86
BIOS), `socat`, and `jq`, then run `./images/smoke-vm.sh TARGET`. This boots an isolated
VM with no networking or host disks, checks a screenshot for the workspace,
confirms the boot drive is marked mounted, and writes/verifies a temporary file in RAM.
It uses a temporary write overlay and leaves the built image unchanged. Screenshots
and serial logs remain under `image-output/vm-TARGET-FIRMWARE/`. Override firmware
paths with `OMS_EFI_CODE` and `OMS_EFI_VARS` if your distribution uses other paths.
The default x86 32-bit test uses BIOS; testing IA32 UEFI needs separate IA32 firmware.

The 0.3.0 preview images passed graphical boot, mounted boot-medium detection,
and a native write/read-back test on a temporary file in QEMU with 2 GiB RAM:

| Target | Tested firmware |
| --- | --- |
| `x86_32` | BIOS |
| `x86_64` | BIOS and x64 UEFI |
| `arm32` | ARM32 UEFI |
| `arm64` | AArch64 UEFI |

IA32 UEFI and physical hardware have not been validated. These checks do not
test physical disk erasure or hardware-specific storage behavior.

## Use

Check `SHA256SUMS`, decompress the `.img.xz`, and use an image-writing utility to
write the `.img` to a disposable USB drive. The image replaces that drive's
contents. Select that USB drive in the machine's firmware boot menu.

The default entry opens the device workspace. Starting the application does not
erase anything. Select one device, review its identity and capacity, and type its
complete path to authorize the operation. A separate demonstration entry creates
temporary files and never offers physical devices for erasure.

The root filesystem stays mounted read-only. The backend therefore recognizes
the boot drive as in use. Writable session state lives in RAM and disappears on
shutdown. Reports must be exported to separately mounted storage to survive a
reboot. No storage is automatically mounted.

For report storage, press Ctrl+Alt+F2 for the local recovery shell, inspect device
names, and mount an existing report partition at `/media/reports`. Return with
Ctrl+Alt+F1 and export there. Unmount it after export before unplugging it. Mounted
report storage is also blocked from erasure. The recovery shell has local root
access; there is no remote login service. Use `poweroff` there to shut down.

Display logs are in `/tmp/oms-display.log`. Hardware without a supported DRM
display driver may require a board-specific kernel or display configuration.
The CLI remains available from the recovery console.

See [troubleshooting](troubleshooting.md#boot-media-and-arm) for download,
firmware, display, and missing-device problems. The
[report guide](reports.md) explains export formats, retention, and outcome fields.

## Licenses

The project's C code and build recipes are MIT OR Apache-2.0. The complete boot
image also contains Linux, GRUB, BusyBox, libraries, and fonts under their own
licenses. It is not an MIT-only or Apache-only operating system.

Keep the dependency manifest, source bundles, Buildroot archive, build recipes,
and dependency-source notes with any image distribution. Review Buildroot's
`legal-info/README` for components it could not automatically collect. A generated
manifest is not a declaration that all redistribution obligations are satisfied.

The [maintainer guide](maintenance.md) provides a container command for source
collection, an artifact inventory, and release validation steps.
