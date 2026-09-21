#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=${1:?architecture required}
default_firmware=uefi
if [ "$arch" = x86_32 ]; then default_firmware=bios; fi
firmware=${2:-$default_firmware}
case "$arch" in x86_32|x86_64|arm32|arm64) ;; *) exit 2 ;; esac
case "$firmware" in bios|uefi) ;; *) exit 2 ;; esac
run_dir="$project/image-output/vm-$arch-$firmware"
mkdir -p "$project/build"
${CC:-cc} -std=c11 -Wall -Wextra -Werror "$project/tests/check_boot.c" -o "$project/build/check-boot"
"$project/images/run-vm.sh" "$arch" "$firmware"
trap 'printf "quit\n" | socat - UNIX-CONNECT:"$run_dir/monitor" >/dev/null 2>&1 || true' EXIT HUP INT TERM
attempt=0
ready=false
while [ "$attempt" -lt 60 ]; do
    sleep 5
    printf 'screendump %s/screen.ppm\n' "$run_dir" | socat - UNIX-CONNECT:"$run_dir/monitor" >/dev/null
    if "$project/build/check-boot" "$run_dir/screen.ppm"; then
        ready=true
        break
    fi
    attempt=$((attempt + 1))
done
if [ "$ready" != true ]; then
    echo "Graphical boot timed out; inspect $run_dir/screen.ppm and serial.log" >&2
    exit 1
fi
# The VM contains only the boot image under a temporary write overlay. All write
# tests below target a disposable regular file in its RAM-backed /tmp directory.
send_text() {
    "$project/images/vm-type.sh" "$run_dir/monitor" "$1"
}
printf 'sendkey ctrl-alt-f2\n' | socat - UNIX-CONNECT:"$run_dir/monitor" >/dev/null
sleep 1
printf 'sendkey ret\n' | socat - UNIX-CONNECT:"$run_dir/monitor" >/dev/null
sleep 1
case "$arch" in arm*) serial=ttyAMA0 ;; *) serial=ttyS0 ;; esac
send_text "exec >/dev/$serial 2>&1; PS1=''"
send_text 'oms inspect /dev/vda --json'
send_text 'dd if=/dev/zero of=/tmp/oms-smoke.img bs=1048576 count=2 && oms erase /tmp/oms-smoke.img --allow-file --execute --confirm /tmp/oms-smoke.img --method ones --verify && printf OMS_SMOKE_ && echo OK'
attempt=0
while [ "$attempt" -lt 30 ]; do
    if tr -d '\r' < "$run_dir/serial.log" | grep -qx 'OMS_SMOKE_OK'; then
        tr -d '\r' < "$run_dir/serial.log" | sed -n 's/^[^{]*\({"path":.*}\)$/\1/p' | tail -1 > "$run_dir/boot-device.json"
        jq -e '.kind == "block" and .mounted == true' "$run_dir/boot-device.json" >/dev/null
        printf 'Graphical boot, boot-medium protection, and native write/read-back passed: %s %s\n' "$arch" "$firmware"
        exit 0
    fi
    sleep 1
    attempt=$((attempt + 1))
done
echo "Native VM test failed; inspect $run_dir/serial.log" >&2
exit 1
