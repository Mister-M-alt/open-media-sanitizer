# Troubleshooting

[Documentation index](README.md) · [Operator guide](operator-guide.md) · [CLI reference](cli-reference.md)

Start by distinguishing a desktop launch problem, a boot/hardware problem, and
a refused or failed operation. A refusal can be the expected result of a
protective check. Capture the exact message before changing the environment.

## Download and startup

| Symptom | What to check |
| --- | --- |
| GitHub shows 404 or no release assets | Sign in with an account that has access to the private repository; check the [release page](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1) |
| Checksum verification fails | Download the image and `SHA256SUMS` from the same release again; do not use a file with a mismatched checksum |
| Missing C compiler, Make, GTK, or JSON-GLib | Install the [desktop dependencies](../README.md#run-on-an-existing-linux-desktop); development packages are needed to compile |
| Root launcher asks you to build first | Run `make` as your regular user before launching the already-built binaries with elevated privileges |
| `No graphical display is available` | Start from a graphical Linux session or use the CLI; the desktop cannot open over a plain terminal connection |
| Root desktop cannot connect to the display | Use the bootable edition or the CLI for real operations; the app does not change desktop display authorization |
| Backend cannot be started | Build/install `oms` and `oms-gui` together in the same directory; inspect the error in Activity |
| Desktop closes then opens again on boot media | The boot session restarts the workspace; use the recovery shell's `poweroff` command to shut down |

## Boot media and ARM

| Symptom | What to check |
| --- | --- |
| USB drive has files but does not boot | Write the decompressed `.img` as a disk image; copying the file onto a formatted USB drive is not the same operation |
| Firmware rejects or ignores the image | Match CPU and firmware architecture; Secure Boot signing is not included; IA32 UEFI has not been tested |
| ARM machine does not boot | Confirm its precise board model, firmware, device tree, and drivers against the [profile](boot-images.md); a 64-bit CPU alone does not prove AArch64 UEFI support |
| Black screen after Linux starts | Try Ctrl+Alt+F2 and inspect `/tmp/oms-display.log`; the hardware may need a different kernel/display configuration |
| Target storage is missing | Check connections and kernel/controller support; read `oms list` diagnostics in the recovery console |
| QEMU reports missing firmware | Install the matching firmware or set `OMS_EFI_CODE` and `OMS_EFI_VARS` to compatible files as described in the image guide |
| QEMU reports an unknown virtio display device | Install your distribution's QEMU display-device modules as well as the system emulator |

On boot media, Ctrl+Alt+F2 opens the local recovery console; press Enter if
prompted. Ctrl+Alt+F1 returns to the desktop. This console has root privileges.
Read-only diagnostics include:

```sh
oms --version
uname -a
oms list
cat /tmp/oms-display.log
```

The image has no remote login service. A firmware failure before Linux starts
will not create the display log; record the firmware message instead.

## Target selection and confirmation

| Message or state | Meaning and next step |
| --- | --- |
| No supported devices visible | Inventory omits partitions, loop, RAM, and zram devices; failed inspections may be reported as skipped on CLI standard error |
| Mounted or mount status unavailable | A mounted filesystem overlaps the target, or usage information could not be trusted; inspect its role and run on the host |
| Swap active or status unavailable | Swap usage blocks the target, or required status could not be read reliably |
| In use or status unavailable | Another block device depends on the target, or a usage check was inconclusive |
| Read-only | The kernel or file permissions report the target as non-writable; changing confirmation text cannot override this |
| Empty device | The reported size is zero; inspect the connection and device state |
| Physical erasure requires root | Live device operations require root; demo operation does not |
| Start button remains disabled | Type the entire canonical path from Review exactly, with no added spaces; select and review an available target first |
| Target identity changed / target became active | Refresh, inspect again, and re-evaluate the target before a new confirmation |
| Inspection timed out | The desktop stops an inspection after 30 seconds; check the device and connection before retrying |
| Test file must have exactly one hard link / file locked | Create a fresh temporary sample that no other process is using |

Do not blindly unmount, disable swap, or remove storage mappings to get past a
refusal. Those resources may support the running system. If the intended target
contains the current OS, use separate boot media. There is no force/bypass flag.

## Operation and report problems

| Symptom | Meaning and next step |
| --- | --- |
| Verification is disabled for Random | This implementation only verifies zero/ones; choose one of those patterns if read-back is required |
| Progress returns to zero | A new pass or verification phase started; wait for the final outcome |
| Progress reached 100% but the run failed | Flush, close, or later verification can fail after writing; inspect the final log and exit status |
| Verification mismatch | Returned data did not match the pattern; save diagnostics and investigate the storage path |
| Stop takes time | Blocking I/O or flushing can delay cancellation; powering off cannot restore already overwritten data |
| Report export fails | Use a writable regular-file destination with space available, separate from the target; a symlink or device is rejected |
| Reports are gone after restart | History is session-local; boot session storage is RAM-backed unless explicitly exported elsewhere |
| HTML says read-back was not verified | Check `verification_requested`, `verification_completed`, method, and outcome in JSON; it may have been disabled or the run failed |

## Include useful information in a bug report

Provide the version/release tag or commit, CPU architecture, OS/kernel, firmware
mode, and whether this is demo, live desktop, or boot media. Include the exact
message, the steps that caused it, expected versus observed behavior, and whether
any writes had started. For ARM, include the board model and firmware version.

Attach relevant excerpts from Activity or the exported record, and the display
log for graphical boot issues. For a local QEMU smoke test, diagnostics are in
`image-output/vm-TARGET-FIRMWARE/`. A small reproduction using a fresh regular
file is preferable to instructions that require physical erasure.

Review diagnostics before sharing them: paths and hardware details may identify
your environment. Do not upload device contents or recovered data. Use the
[security reporting route](../SECURITY.md) for a vulnerability, including a case
where a protective check appears to permit an unintended write.
