# Preview validation record

[Documentation index](README.md) · [Boot image guide](boot-images.md) · [Maintainer guide](maintenance.md)

This record identifies the evidence distributed with `v0.3.0-preview.1`, published
on 21 September 2026. It describes software and virtual-machine checks; it is not
a physical-hardware compatibility list or a sanitization certification.

## Release identity

| Item | Reference |
| --- | --- |
| Release and downloads | [v0.3.0-preview.1](https://github.com/Mister-M-alt/open-media-sanitizer/releases/tag/v0.3.0-preview.1) |
| Image source commit | [`171adee0096065fde3ecd9d894e777d0bc051b16`](https://github.com/Mister-M-alt/open-media-sanitizer/tree/171adee0096065fde3ecd9d894e777d0bc051b16) |
| Build system | Buildroot 2025.02.18, with resolved configuration in each source bundle |
| Download checksums | [`SHA256SUMS`](https://github.com/Mister-M-alt/open-media-sanitizer/releases/download/v0.3.0-preview.1/SHA256SUMS) |
| Native CI at the release commit | [Successful run, 21 September 2026](https://github.com/Mister-M-alt/open-media-sanitizer/actions/runs/35592990935) |

The native CI workflow builds the application and runs the CLI, simulated-device,
native workspace/report, and headless GUI tests. Inspect the linked run and
[workflow](../.github/workflows/ci.yml) for the exact commands. Those tests use
disposable files and simulated device information, not physical disk writes.

## Boot checks

The release's `validation.txt` files record successful graphical startup,
mounted boot-medium detection, and native write/read-back on a regular file in
guest RAM for these profiles:

| Image | Firmware checked | VM memory |
| --- | --- | --- |
| `oms-x86_32.img.xz` | Legacy BIOS | 2 GiB |
| `oms-x86_64.img.xz` | Legacy BIOS and x64 UEFI | 2 GiB |
| `oms-arm32.img.xz` | ARM32 UEFI on QEMU `virt` | 2 GiB |
| `oms-arm64.img.xz` | AArch64 UEFI on QEMU `virt` | 2 GiB |

The [smoke helper](../images/smoke-vm.sh) uses software emulation, no networking,
no host-disk passthrough, and a temporary write overlay. It leaves the built image
unchanged. Its regular-file write check does not erase the boot disk.

## Inspect the bundled evidence

Download `oms-TARGET-sources.tar` from the same release as the image. Its top-level
`TARGET/` directory includes:

| File | Contents |
| --- | --- |
| `validation.txt` | Source commit, target, firmware checks, and their limits |
| `boot-check.log` | Smoke-check command output |
| `boot-workspace.png` | Screenshot of the running workspace |
| `boot-device.json` | Inventory snapshot showing the boot medium's mounted state |
| `buildroot.config` | Resolved image configuration |
| `oms-source.tar.xz` | Application source and build recipes packaged for the release |
| Dependency archives and notices | Corresponding sources and collection notes described in [maintenance](maintenance.md#collect-corresponding-sources) |

For example, after checking the download checksum, inspect the small evidence
files without extracting the complete dependency archive:

```sh
tar -xOf oms-x86_64-sources.tar x86_64/validation.txt
tar -xOf oms-x86_64-sources.tar x86_64/boot-check.log
```

The x86-64 summary records both firmware runs; its bundled screenshot and
`boot-check.log` are from the UEFI run. The source bundles preserve the original
release snapshot, so consult the current repository for documentation updates.

## Limits and new hardware results

IA32 UEFI boot and physical hardware have not been validated. The existing checks
do not establish SSD firmware behavior, inaccessible-sector coverage, behavior
after power loss, or compatibility with a particular ARM board. Two GiB is the
tested VM memory allocation, not a measured minimum requirement.

New hardware results should identify the source commit or release, image checksum,
machine and firmware, storage controller/connection, device model, exact procedure,
and observed outcome. Remove personal data before sharing logs or reports. Follow
[image validation](maintenance.md#validate-the-image) when reproducing the VM checks.
