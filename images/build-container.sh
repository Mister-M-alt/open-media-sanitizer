#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
docker build -t oms-builder:2025.02.18 -f "$project/images/Dockerfile" "$project"
exec docker run --rm --user "$(id -u):$(id -g)" -v "$project:$project" -w "$project" \
    -e JOBS="${JOBS:-16}" oms-builder:2025.02.18 ./images/build.sh "$@"
