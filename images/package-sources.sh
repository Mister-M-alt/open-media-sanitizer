#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=${1:?architecture required}
case "$arch" in x86_32|x86_64|arm32|arm64) ;; *) exit 2 ;; esac
output="$project/image-output/$arch"
artifacts="$project/image-output/artifacts/$arch"
"$project/images/build.sh" "$arch" legal-info
mkdir -p "$artifacts"
"$project/images/toolchain-sources.sh" "$arch"
# Buildroot local packages have no upstream tarball. Include their exact source
# and the image build recipes alongside Buildroot's dependency source archive.
tar -cJf "$artifacts/oms-source.tar.xz" -C "$project" src include tests Makefile start.sh README.md CONTRIBUTING.md SECURITY.md LICENSE LICENSE-APACHE LICENSE-MIT images docs .github .dockerignore .gitignore
XZ_OPT='-T4 -1' tar --exclude='legal-info/sources/toolchain-external-bootlin-*' -cJf "$artifacts/dependency-sources.tar.xz" -C "$output" legal-info
cp "$project/.image-cache/buildroot-2025.02.18.tar.xz" "$artifacts/"
cp "$output/legal-info/manifest.csv" "$artifacts/dependency-manifest.csv"
cp "$output/legal-info/README" "$artifacts/dependency-source-notes.txt"
cat >> "$artifacts/dependency-source-notes.txt" <<'EOF'

Open Media Sanitizer packaging supplements:
- Buildroot sources are included as buildroot-2025.02.18.tar.xz.
- The local application and image recipes are included as oms-source.tar.xz.
- External toolchain runtime sources, licenses, original toolchain build
  recipes/configuration, and collection notes are in toolchain-runtime-sources.tar.xz.
- The external prebuilt SDK archive is omitted from dependency-sources.tar.xz.
EOF
(cd "$artifacts" && sha256sum ./*.xz > SHA256SUMS)
