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
test "$firmware" = uefi || { case "$arch" in x86_*) ;; *) exit 2 ;; esac; }
disk="$project/image-output/$arch/images/oms.img"
test -f "$disk"
run_dir="$project/image-output/vm-$arch-$firmware"
mkdir -p "$run_dir"
if [ -f "$run_dir/pid" ] && kill -0 "$(cat "$run_dir/pid")" 2>/dev/null; then
    echo "A VM is already running in $run_dir" >&2; exit 1
fi
set -- -m 2048 -smp 2 -accel tcg,thread=multi -snapshot -nic none \
    -drive "file=$disk,if=virtio,format=raw" -display none \
    -monitor "unix:$run_dir/monitor,server=on,wait=off" \
    -serial "file:$run_dir/serial.log" -pidfile "$run_dir/pid" -daemonize
case "$arch" in
    x86_32)
        emulator=qemu-system-i386
        set -- -machine pc -cpu max -vga virtio "$@"
        if [ "$firmware" = uefi ]; then
            code=${OMS_EFI_CODE:-/usr/share/qemu/edk2-i386-code.fd}
            vars=${OMS_EFI_VARS:-/usr/share/qemu/edk2-i386-vars.fd}
        fi
        ;;
    x86_64)
        emulator=qemu-system-x86_64
        set -- -machine pc -cpu max -vga virtio "$@"
        if [ "$firmware" = uefi ]; then
            code=${OMS_EFI_CODE:-/usr/share/edk2/x64/OVMF_CODE.4m.fd}
            vars=${OMS_EFI_VARS:-/usr/share/edk2/x64/OVMF_VARS.4m.fd}
            if [ ! -f "$code" ] && [ -z "${OMS_EFI_CODE:-}" ]; then
                code=/usr/share/OVMF/OVMF_CODE_4M.fd
                vars=/usr/share/OVMF/OVMF_VARS_4M.fd
            fi
        fi
        ;;
    arm32)
        emulator=qemu-system-arm
        set -- -machine virt -cpu cortex-a15 -device virtio-gpu-pci -device qemu-xhci -device usb-kbd -device usb-tablet "$@"
        code=${OMS_EFI_CODE:-/usr/share/qemu/edk2-arm-code.fd}
        vars=${OMS_EFI_VARS:-/usr/share/qemu/edk2-arm-vars.fd}
        if [ ! -f "$code" ] && [ -z "${OMS_EFI_CODE:-}" ]; then
            code=/usr/share/AAVMF/AAVMF32_CODE.fd
            vars=/usr/share/AAVMF/AAVMF32_VARS.fd
        fi
        ;;
    arm64)
        emulator=qemu-system-aarch64
        set -- -machine virt -cpu cortex-a53 -device virtio-gpu-pci -device qemu-xhci -device usb-kbd -device usb-tablet "$@"
        code=${OMS_EFI_CODE:-/usr/share/edk2/aarch64/QEMU_EFI.fd}
        vars=${OMS_EFI_VARS:-/usr/share/edk2/aarch64/QEMU_VARS.fd}
        if [ ! -f "$code" ] && [ -z "${OMS_EFI_CODE:-}" ]; then
            code=/usr/share/AAVMF/AAVMF_CODE.fd
            vars=/usr/share/AAVMF/AAVMF_VARS.fd
        fi
        ;;
esac
if [ "$firmware" = uefi ]; then
    test -f "$code" && test -f "$vars"
    cp "$vars" "$run_dir/vars.fd"
    set -- "$@" -drive "if=pflash,format=raw,readonly=on,file=$code" -drive "if=pflash,format=raw,file=$run_dir/vars.fd"
fi
"$emulator" "$@"
printf 'VM PID: %s\nMonitor: %s/monitor\n' "$(cat "$run_dir/pid")" "$run_dir"
