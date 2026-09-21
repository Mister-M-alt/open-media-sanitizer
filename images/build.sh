#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=${1:-}
case "$arch" in x86_32|x86_64|arm32|arm64) ;; *) echo "Usage: $0 x86_32|x86_64|arm32|arm64 [make-target]" >&2; exit 2 ;; esac
version=2025.02.18
digest=e38ad1df6ea0479fff6419a87a64535d02131674d463be154a763ef725b55321
cache="$project/.image-cache"
output="$project/image-output/$arch"
mkdir -p "$cache" "$output"
# Lock only shared preparation; independent architectures compile concurrently.
(
    flock 9
    archive="$cache/buildroot-$version.tar.xz"
    if [ ! -f "$archive" ]; then
        curl --fail --location --retry 3 "https://buildroot.org/downloads/buildroot-$version.tar.xz" -o "$archive.part"
        mv "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$digest" "$archive" | sha256sum -c -
    if [ ! -d "$cache/buildroot-$version" ]; then tar -xJf "$archive" -C "$cache"; fi
    mkdir -p "$cache/source"
    rsync -a --delete --include=/src/ --include=/src/*** --include=/include/ --include=/include/*** \
        --include=/Makefile --include='/LICENSE*' --exclude='*' "$project/" "$cache/source/"
) 9>"$cache/prepare.lock"
export BR2_DL_DIR="$cache/downloads"
export SOURCE_DATE_EPOCH=1789689600
cat "$project/images/configs/common.config" "$project/images/configs/$arch.config" > "$output/oms_defconfig"
make -C "$cache/buildroot-$version" O="$output" BR2_EXTERNAL="$project/images" \
    BR2_DEFCONFIG="$output/oms_defconfig" defconfig
if [ "${2:-all}" = all ]; then make -C "$output" oms-dirclean; fi
make -C "$output" BR2_JLEVEL="${JOBS:-16}" "${2:-all}"
if [ "${2:-all}" = all ]; then
    mkdir -p "$project/image-output/artifacts/$arch"
    xz -T"${JOBS:-16}" -3 -c "$output/images/oms.img" > "$project/image-output/artifacts/$arch/oms-$arch.img.xz"
    cp "$output/.config" "$project/image-output/artifacts/$arch/buildroot.config"
    if [ -f "$output/images/u-boot.bin" ]; then cp "$output/images/u-boot.bin" "$project/image-output/artifacts/$arch/"; fi
    (cd "$project/image-output/artifacts/$arch" && sha256sum ./*.img.xz > SHA256SUMS)
fi
