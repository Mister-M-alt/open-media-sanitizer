# Image and release maintenance

[Documentation index](README.md) · [Boot image guide](boot-images.md) · [Developer guide](development.md)

This guide is for people building system images, adding hardware support, or
preparing downloads. Use the operator guide for day-to-day operation. The current
repository and release are private; publishing a release within the repository
does not change its visibility.

## Build inputs and outputs

The [image guide](boot-images.md#build) lists host requirements and all target
commands. The host is x86-64 Linux. Allow at least 30 GiB per architecture plus
source-bundle space and preferably 8 GiB RAM. Choose `JOBS` for the host's memory
and CPU capacity.

| Input | Where it is defined |
| --- | --- |
| Buildroot version, archive hash, source snapshot preparation | `images/build.sh` |
| Build container and host packages | `images/Dockerfile` |
| Shared runtime packages and root filesystem | `images/configs/common.config` |
| CPU, toolchain, kernel image, and GRUB firmware target | `images/configs/TARGET.config` |
| Kernel drivers | `images/linux.config` |
| Desktop startup and display configuration | `images/overlay/` |
| Boot entries and read-only root arguments | `images/grub.cfg` |
| Runtime cleanup and disk layout | `images/post-build.sh`, `images/post-image.sh` |
| Application package and license metadata | `images/package/oms/` |
| External toolchain source hashes | `images/runtime-sources.list` |

Buildroot 2025.02.18 is pinned and hash-checked. Dependency versions and hashes
come from that release. Container-base and host-tool updates mean bit-for-bit
reproducibility is not asserted. Record the source commit and resolved
`buildroot.config` with each distributed image.

The build wrapper snapshots the local application source, including local
changes; it does not restrict itself to committed files. Start release builds
from a reviewed, clean checkout. Target configuration is regenerated from the
checked-in fragments on each invocation; edit the fragments rather than relying
on a manual change to `image-output/TARGET/.config`.

```sh
JOBS=4 ./images/build-container.sh x86_64
```

This creates `image-output/x86_64/images/oms.img` and a compressed download under
`image-output/artifacts/x86_64/`. The output directory and `.image-cache/` are
ignored by Git. Do not place generated disks in source commits.

## Collect corresponding sources

In the same build environment, run `./images/package-sources.sh TARGET` after
the image build. For a Docker build, the wrapper has already created the required
builder image; from the repository root, for example:

```sh
docker run --rm --user "$(id -u):$(id -g)" \
    -v "$PWD:$PWD" -w "$PWD" -e JOBS=4 \
    oms-builder:2025.02.18 ./images/package-sources.sh x86_64
```

The resulting architecture directory contains:

| Artifact | Purpose |
| --- | --- |
| `oms-TARGET.img.xz` | Compressed boot disk |
| `buildroot.config` | Resolved system configuration |
| `oms-source.tar.xz` | Local application, tests, image recipes, documentation, and project licenses |
| `dependency-sources.tar.xz` | Buildroot-collected dependency source and license material |
| `toolchain-runtime-sources.tar.xz` | Runtime library sources, licenses, and exact Bootlin toolchain build recipes/configuration |
| `buildroot-2025.02.18.tar.xz` | Pinned Buildroot source |
| `dependency-manifest.csv` | Configured target packages and license metadata |
| `dependency-source-notes.txt` | Collection limitations and packaging supplements |
| `SHA256SUMS` | Checksums of the compressed artifacts in this directory |

The source collector supplements Buildroot's known gaps: the local application,
Buildroot itself, and the external toolchain's runtime sources/licenses. It
excludes the prebuilt SDK binary archive that Buildroot otherwise lists as
source. Review the notes after every dependency or toolchain change; a previous
supplement may no longer match a new runtime.

The project's own code is MIT OR Apache-2.0. Linux, GRUB, BusyBox, GTK, fonts, and
other dependencies retain their own licenses. Keep sources, configuration,
notices, and build recipes with image distributions. A generated manifest is
not a blanket licensing clearance for a modified distribution.

## Validate the image

Run the native checks from [development](development.md#validation-by-change-type),
then install QEMU, the matching firmware, `socat`, and `jq` on the host. Test
each affected profile:

```sh
./images/smoke-vm.sh x86_32
./images/smoke-vm.sh x86_64
./images/smoke-vm.sh x86_64 bios
./images/smoke-vm.sh arm32
./images/smoke-vm.sh arm64
```

The helper uses QEMU TCG, 2 GiB RAM, a temporary disk-write overlay, no networking,
and no host storage passthrough. It checks the GUI screenshot, verifies that the
boot disk is marked mounted, and writes/verifies a regular file in guest RAM.
It never tests erasure of the virtual boot device or a physical disk. A successful
run prints **Graphical boot, boot-medium protection, and native write/read-back
passed** and shuts its VM down.

Keep `screen.ppm`, `serial.log`, and `boot-device.json` from
`image-output/vm-TARGET-FIRMWARE/` with test results and the exact source commit.
`run-vm.sh` is a headless diagnostic helper, not an interactive desktop viewer.
Use the smoke helper for its automatic checks and cleanup.

The 0.3.0 preview was tested on x86 BIOS, x64 UEFI, ARM32 UEFI, and AArch64 UEFI.
IA32 EFI is built but has not been tested. The post-build check rejects runtime
Python interpreters, source, bytecode, and Python executable scripts. Retain that
check when updating runtime packages; upstream build helpers can otherwise leak
into the target filesystem.

## Add physical hardware support

A generic image passing QEMU is not proof of board support. Record the exact
board/model, CPU baseline, firmware architecture/version, boot method, storage
controller, and display/input hardware. For ARM, determine who supplies the
device tree and whether the firmware can load the selected EFI architecture.

Add the required kernel configuration and runtime firmware only when needed,
with their source/license information. Preserve the read-only mounted root,
boot-medium usage protection, and local-only workspace startup. Do not claim
that one ARM image supports every board or both EFI architectures.

Start physical validation with boot, display, keyboard, inventory, and demo-file
operations. Confirm the boot drive is blocked. Any subsequent storage-write
test needs specifically identified expendable hardware and an operator who
authorizes its destruction; automated tests remain regular-file-only. Record
hardware results separately from QEMU results, including failures and untested
controllers.

## Prepare a release

1. Choose a reviewed source commit. Keep the version in `include/oms.h`, the
   application package version, and version-specific documentation consistent.
2. Build and test the affected targets. Native CI runs on pushes/PRs; the
   **Bootable images** workflow is manually dispatched with one target or `all`.
   Its image and diagnostic artifacts expire after the configured retention
   period, currently 14 days; they are not permanent release storage.
3. Collect corresponding sources for every image from the same checkout and
   configuration. Verify the checksums and inspect collection notes.
4. Give release assets unique names. The preview uses `oms-TARGET.img.xz` and
   `oms-TARGET-sources.tar`; each source tar contains an architecture directory
   with the source artifacts and available validation evidence. Do not include
   another copy of the disk image inside the source tar.
5. Generate a release-level `SHA256SUMS` covering the four image files and the
   four source tar files. This is separate from each architecture's internal
   checksums. Verify uploaded asset hashes against the local files.
6. Create a draft prerelease tied to the full reviewed commit SHA. Describe
   firmware combinations actually tested, physical-hardware limits, source
   contents, and changes users need to know. Publish when the complete asset
   set is uploaded and verified; retain private repository visibility until a
   separate decision to make it public.

A documentation update on `main` does not change an existing release's image,
source bundle, or tag. Keep published assets associated with the source that
produced them; do not silently substitute a later build under the same version.

## Work with a downloaded source bundle

The preview's `oms-arm64-sources.tar`, for example, extracts an `arm64/`
directory. Place the matching image in that directory to verify all entries in
its internal `SHA256SUMS`, or use the release-level checksums before extraction.

Extract `oms-source.tar.xz` into a new working directory to inspect the application
and image recipes. The other archives supply dependency source, licenses, and
toolchain context. The normal build scripts use their configured download URLs;
the bundles are not a preassembled, offline Buildroot download cache. An offline
rebuild requires preparing that cache and the required host tools separately.
