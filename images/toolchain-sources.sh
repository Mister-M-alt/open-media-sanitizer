#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=${1:?architecture required}
case "$arch" in x86_32|x86_64|arm32|arm64) ;; *) exit 2 ;; esac
cache="$project/.image-cache/runtime-sources"
stage="$project/.image-cache/runtime-package-$arch"
sdk="$project/image-output/$arch/host/opt/ext-toolchain"
mkdir -p "$cache" "$stage/licenses/gcc" "$stage/licenses/glibc"
(
    flock 9
    while read -r hash filename url; do
        case "$hash" in ''|'#'*) continue ;; esac
        if [ ! -f "$cache/$filename" ]; then
            curl --fail --location --retry 3 "$url" -o "$cache/$filename.part"
            mv "$cache/$filename.part" "$cache/$filename"
        fi
        printf '%s  %s\n' "$hash" "$cache/$filename" | sha256sum -c -
    done < "$project/images/runtime-sources.list"
) 9>"$project/.image-cache/runtime-sources.lock"
cp "$cache/"*.tar.* "$stage/"
cp "$project/images/runtime-sources.list" "$stage/"
cp "$sdk/buildroot.config" "$stage/toolchain-buildroot.config"
cp "$sdk/summary.csv" "$stage/toolchain-manifest.csv"
cp "$sdk/README.txt" "$stage/toolchain-upstream-readme.txt"
tar -xJf "$cache/gcc-13.3.0.tar.xz" -C "$stage/licenses/gcc" --strip-components=1 \
    gcc-13.3.0/COPYING gcc-13.3.0/COPYING3 gcc-13.3.0/COPYING.LIB gcc-13.3.0/COPYING.RUNTIME
glibc=glibc-2.39-74-g198632a05f6c7b9ab67d3331d8caace9ceabb685
glibc_root=glibc-198632a05f6c7b9ab67d3331d8caace9ceabb685
tar -xzf "$cache/$glibc.tar.gz" -C "$stage/licenses/glibc" --strip-components=1 \
    "$glibc_root/COPYING" "$glibc_root/COPYING.LIB" "$glibc_root/LICENSES"
cat > "$stage/README.txt" <<'EOF'
Toolchain runtime source supplement

The boot image uses the glibc, libgcc and libstdc++ runtime libraries from
Bootlin's stable 2024.05-1 toolchain. This supplement contains their upstream
sources, the Linux headers used to build them, the exact Bootlin Buildroot fork
with its patches/build recipes, and the architecture's toolchain configuration.
The Bootlin Buildroot commit is 1bef6133191aec7b923f5ff80af077c5cdab4e20.
Its package/gcc/13.3.0 and package/linux-headers directories contain the patches
applied to the included source archives. Follow toolchain-upstream-readme.txt
using the included toolchain-buildroot.config to rebuild that toolchain.

Buildroot's generic legal-info mechanism saves the external SDK binary archive
as a "source" and does not collect its licenses. That SDK binary archive is
excluded from our dependency-source bundle; the actual runtime sources and
license texts are supplied here instead. The full SDK is a build-time download,
not part of the boot image. Its manifest also lists build tools not shipped in
the boot image. The current image's own kernel sources and build recipes are in
the dependency bundle and buildroot-2025.02.18.tar.xz, respectively.
EOF
XZ_OPT='-T4 -1' tar -cJf "$project/image-output/artifacts/$arch/toolchain-runtime-sources.tar.xz" -C "$stage" .
