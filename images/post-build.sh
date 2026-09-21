#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
board=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Keep the root partition mounted read-only so usage checks protect boot media.
mkdir -p "$TARGET_DIR/boot/grub" "$TARGET_DIR/media/reports"
cp "$board/grub.cfg" "$TARGET_DIR/boot/grub/grub.cfg"
if [ ! -f "$BINARIES_DIR/grub.img" ]; then
    sed -i 's/6f6d7301-02/6f6d7301-0000-4000-8000-000000000002/g' "$TARGET_DIR/boot/grub/grub.cfg"
fi
for kernel in bzImage zImage Image; do
    if [ -f "$BINARIES_DIR/$kernel" ]; then
        cp "$BINARIES_DIR/$kernel" "$TARGET_DIR/boot/oms-kernel"
        break
    fi
done
test -f "$TARGET_DIR/boot/oms-kernel"
# Xorg's package installs a server-only startup service. Our session owns the
# display server, so disable the competing instance before producing the image.
rm -f "$TARGET_DIR/etc/init.d/S40xorg"
mkdir -p "$BINARIES_DIR/efi-part/EFI/BOOT"
cp "$TARGET_DIR/boot/grub/grub.cfg" "$BINARIES_DIR/efi-part/EFI/BOOT/grub.cfg"
# A local recovery console, with no network login service.
sed -i '/GENERIC_SERIAL/d; /getty/d' "$TARGET_DIR/etc/inittab"
printf '\ntty2::askfirst:-/bin/sh\n' >> "$TARGET_DIR/etc/inittab"
test ! -e "$TARGET_DIR/usr/bin/python3"
test ! -e "$TARGET_DIR/usr/bin/python"
# XCB's protocol generator and libstdc++'s debugger printers are development
# helpers. They are unused by the native runtime and need no place on boot media.
for generator in "$TARGET_DIR"/usr/lib/python*/site-packages/xcbgen; do
    rm -rf "$generator"
done
rm -f "$TARGET_DIR"/usr/lib/libstdc++.so.*-gdb.py
rm -f "$TARGET_DIR/usr/libexec/libinput/libinput-analyze-buttons" \
    "$TARGET_DIR/usr/libexec/libinput/libinput-list-kernel-devices" \
    "$TARGET_DIR/usr/libexec/openbox-xdg-autostart"
for library in "$TARGET_DIR"/usr/lib/python*; do
    rmdir "$library/site-packages" "$library" 2>/dev/null || true
done
python_file=$(find "$TARGET_DIR" -type f \( -name '*.py' -o -name '*.pyc' -o -name '*.pyo' \) -print -quit)
if [ -n "$python_file" ]; then
    echo "Unexpected Python file in runtime image: $python_file" >&2
    exit 1
fi
python_script=$(grep -rIl '^#!.*python' "$TARGET_DIR/usr" || true)
if [ -n "$python_script" ]; then
    echo "Unexpected Python executable in runtime image: $python_script" >&2
    exit 1
fi
chmod 0755 "$TARGET_DIR/etc/init.d/S99workspace" "$TARGET_DIR/usr/bin/oms-session"
