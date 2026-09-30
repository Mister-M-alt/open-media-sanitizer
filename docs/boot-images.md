# Standalone boot media

[Documentation index](README.md) · [Operator guide](operator-guide.md) · [Maintainer guide](maintenance.md)

The image build produces a complete Linux system with the native C desktop and
erase backend. It starts locally from USB or a virtual disk without an installed
operating system, an account, a browser, or a network service.

Download the images, checksums, and matching source bundles from the
[0.3.0 preview release](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1).
Downloads are public; using the application requires no account or activation.

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

## Prepare a USB drive

Use a USB drive of at least 2 GB whose contents can be replaced. The current raw
images are about 833 MiB; the remaining capacity is not configured as persistent
report storage. Have a second drive with an existing FAT32 or ext4 partition for
reports. The preview was tested with 2 GiB RAM in QEMU; a physical-machine minimum
has not been measured.

1. Download the image matching the **machine you will boot**, plus `SHA256SUMS`,
   from the release above. The computer used to prepare the USB can run Linux,
   Windows, or macOS. Preparing media on a Mac does not establish that the image
   supports that Mac's hardware.
2. Check the compressed download before writing it. In the download directory,
   use the appropriate command below, substituting your chosen filename:

   | System | Command |
   | --- | --- |
   | Linux | `sha256sum --ignore-missing -c SHA256SUMS` |
   | macOS | `shasum -a 256 oms-x86_64.img.xz` |
   | Windows PowerShell | `Get-FileHash .\oms-x86_64.img.xz -Algorithm SHA256` |

   Linux must report `OK` for the chosen image. On macOS or Windows, compare the
   complete result with the same filename's line in `SHA256SUMS`; uppercase and
   lowercase hexadecimal letters are equivalent. Stop if the values differ.
   Checksums detect a changed download; the preview does not publish a separate
   signed checksum manifest.
3. Use an image-writing utility such as
   [balenaEtcher](https://etcher.balena.io/), available for Linux, Windows, and
   macOS. Etcher accepts the `.img.xz` directly. For another utility that requires
   a raw image, first extract the `.img`; on Linux with XZ Utils installed, use
   `xz -dk oms-x86_64.img.xz`.
4. In Etcher, choose **Flash from file**, select the image, then **Select target**.
   Check the USB drive's identity and capacity before choosing **Flash**. This
   replaces the entire selected drive, including its partition table. Wait for
   writing and validation to finish, then eject the drive through the host OS.
5. Boot the intended machine using its firmware's one-time USB boot menu. Secure
   Boot signing is not supplied; use a compatible firmware configuration. Choose
   the demonstration entry first to check the display, input, and workflow.

If Windows or macOS offers to format an unfamiliar partition after flashing,
cancel that prompt. The Linux filesystem is already part of the boot image.
See [troubleshooting](troubleshooting.md#boot-media-and-arm) if booting fails.

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
as a workflow artifact with limited retention. Use release assets for published
downloads. The workflow runs manually so ordinary code pushes do not trigger
four large system builds.

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
test physical disk erasure or hardware-specific storage behavior. The
[validation record](validation.md) links the exact source commit, CI run, and
evidence retained in the release source bundles.

## Use

Follow [USB preparation](#prepare-a-usb-drive), then select that drive in the
machine's firmware boot menu.

The default entry opens the device workspace. Starting the application does not
erase anything. Select one device, review its identity and capacity, and type its
complete path to authorize the operation. A separate demonstration entry creates
temporary files and never offers physical devices for erasure.

The root filesystem stays mounted read-only. The backend therefore recognizes
the boot drive as in use. Writable session state lives in RAM and disappears on
shutdown. Reports must be exported to separately mounted storage to survive a
reboot. No storage is automatically mounted.

Follow [the second-drive procedure](#save-reports-to-a-second-drive) before
starting an operation whose record you need to retain. The recovery shell has
local root access; there is no remote login service.

Display logs are in `/tmp/oms-display.log`. Hardware without a supported DRM
display driver may require a board-specific kernel or display configuration.
The CLI remains available from the recovery console.

See [troubleshooting](troubleshooting.md#boot-media-and-arm) for download,
firmware, display, and missing-device problems. The
[report guide](reports.md) explains export formats, retention, and outcome fields.

## Save reports to a second drive

Prepare an existing FAT32 or ext4 partition on a separate drive before booting.
Both filesystems are supported across the four preview images. exFAT and NTFS
support is not consistent across these images. This procedure mounts an existing
filesystem; it does not create or format one.

1. Press Ctrl+Alt+F2 to open the local recovery shell. Inspect the current device
   list, connect the report drive, and inspect again:

   ```sh
   oms list
   cat /proc/partitions
   blkid
   ```

   Identify the new drive by its size and identity, then its existing report
   partition by the filesystem label/UUID. `/dev/sdX1` below is a placeholder for
   that **partition**, not the erase target or either boot-image partition. Names
   can change between boots; do not assume that a particular drive letter is safe.
2. For FAT32, replace the placeholder and run:

   ```sh
   mount -t vfat -o rw,nosuid,nodev,noexec,umask=077 /dev/sdX1 /media/reports
   ```

   For ext4, use this command instead:

   ```sh
   mount -t ext4 -o rw,nosuid,nodev,noexec /dev/sdX1 /media/reports
   ```

   Check that mounting succeeded and that free space is on the intended drive:

   ```sh
   mount
   df -h /media/reports
   ```

   If mounting fails, resolve it before exporting. Saving to an unmounted
   `/media/reports` is not a successful export to the second drive.
3. Return with Ctrl+Alt+F1. If you booted the demonstration entry, try exporting
   a demo record to `/media/reports/oms-demo.json` to confirm the destination is
   writable. Repeat the mount procedure after rebooting into a live session.
   In that session, refresh Storage after mounting; mounted report storage is
   blocked from erasure.
4. After the desired operation, export its JSON and/or HTML record there with
   a distinct filename. Wait for the export to complete. In the recovery shell,
   inspect the saved files and flush and unmount the drive:

   ```sh
   ls -l /media/reports
   sync
   umount /media/reports
   ```

   Wait for `umount` to succeed before unplugging the drive. If it is busy, close
   any save dialog, leave that directory in the shell, and retry.
5. Read the files on another computer to confirm retention. When all work is
   complete, run `poweroff` in the recovery shell. Closing the workspace alone
   restarts it; it does not shut down the machine.

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
