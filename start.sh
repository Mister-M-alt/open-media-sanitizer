#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(uname -s)" != Linux ]; then
    echo "The desktop app and erase backend currently require Linux." >&2
    exit 1
fi
if [ "$(id -u)" -eq 0 ]; then
    if [ ! -x "$project_dir/build/oms" ] || [ ! -x "$project_dir/build/oms-gui" ]; then
        echo "Build as your regular user first: make -C \"$project_dir\"" >&2
        exit 1
    fi
else
    if ! command -v make >/dev/null 2>&1 || ! command -v "${CC:-cc}" >/dev/null 2>&1; then
        echo "Install a C compiler and Make (Debian/Ubuntu: sudo apt install build-essential)." >&2
        exit 1
    fi
    make --no-print-directory -C "$project_dir" all
fi
exec "$project_dir/build/oms-gui" "$@"
