#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(uname -s)" != Linux ]; then
    echo "The desktop app and erase backend currently require Linux." >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "Install Python 3 and its Tk package to open the desktop app." >&2
    exit 1
fi
if ! python3 -c 'import tkinter' >/dev/null 2>&1; then
    echo "Install Tk: Debian/Ubuntu: sudo apt install python3-tk" >&2
    echo "Arch: sudo pacman -S tk | Fedora: sudo dnf install python3-tkinter" >&2
    exit 1
fi
if [ "$(id -u)" -eq 0 ]; then
    if [ ! -x "$project_dir/build/oms" ]; then
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
exec python3 "$project_dir/app/main.py" "$@"
