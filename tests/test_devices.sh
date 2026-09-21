#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
probe=$(readlink -f build/device-probe)
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
cd "$fixture"
mkdir -p proc/self sys/dev/block sys/class/block
printf '1 0 0:1 / / rw - tmpfs none rw\n' > proc/self/mountinfo
printf 'Filename Type Size Used Priority\n' > proc/swaps
disk() {
    mkdir -p "sys/devices/$1/holders" "sys/devices/$1/slaves"
    ln -s "$fixture/sys/devices/$1" "sys/dev/block/$2"
    ln -s "$fixture/sys/devices/$1" "sys/class/block/${1##*/}"
}
disk disk-a 8:0
disk disk-a/disk-a1 8:1
printf '1\n' > sys/devices/disk-a/disk-a1/partition
disk disk-b 8:16
check() { test "$("$probe" "$1" "$fixture/$2")" = "$3"; }
check mounted sys/devices/disk-a idle
check holders sys/devices/disk-a idle
printf '10 1 8:1 / /media rw - ext4 /dev/example rw\n' > proc/self/mountinfo
check mounted sys/devices/disk-a busy
printf '10 1 8:0 / /media rw - ext4 /dev/example rw\n' > proc/self/mountinfo
check mounted sys/devices/disk-a/disk-a1 busy
printf '10 1 8:16 / /media rw - ext4 /dev/example rw\n' > proc/self/mountinfo
check mounted sys/devices/disk-a idle
disk mapper 253:0
ln -s "$fixture/sys/devices/disk-a/disk-a1" sys/devices/mapper/slaves/member
printf '10 1 253:0 / /media rw - ext4 /dev/example rw\n' > proc/self/mountinfo
check mounted sys/devices/disk-a busy
for invalid in '' corrupt '10 1 8:99 / / rw - ext4 /dev/gone rw'; do
    printf '%s\n' "$invalid" > proc/self/mountinfo
    check mounted sys/devices/disk-a busy
done
rm proc/self/mountinfo
check mounted sys/devices/disk-a busy
touch sys/devices/disk-a/disk-a1/holders/mapper
check holders sys/devices/disk-a busy
check holders sys/devices/disk-b idle
rmdir sys/devices/disk-b/holders
check holders sys/devices/disk-b busy
printf sample > 'test image'
check swap 'test image' idle
printf 'Filename Type Size Used Priority\n%s/test\\040image file 4 0 -2\n' "$fixture" > proc/swaps
check swap 'test image' busy
rm proc/swaps
check swap 'test image' busy
disk loop0 7:0
mkdir sys/devices/loop0/loop
printf '%s/test\\040image\n' "$fixture" > sys/devices/loop0/loop/backing_file
check loop 'test image' busy
printf '%s/missing\n' "$fixture" > sys/devices/loop0/loop/backing_file
check loop 'test image' busy
echo 'Device usage fixtures passed'
