#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT

set -eu

binary=${1:-./build/oms}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

target="$temporary/target.img"
expected="$temporary/expected.img"

"$binary" --version | grep -q '^oms '
"$binary" --help | grep -q '^Open Media Sanitizer'

dd if=/dev/urandom of="$target" bs=131072 count=1 status=none
canonical=$(readlink -f "$target")
before=$(cksum "$target")

"$binary" inspect "$target" --allow-file | grep -q "Path:       $canonical"

if "$binary" inspect "$target" >/dev/null 2>&1; then
    echo "regular files must require --allow-file" >&2
    exit 1
fi

"$binary" erase "$target" --allow-file --method zero >/dev/null
after=$(cksum "$target")
test "$before" = "$after"

if "$binary" erase "$target" --allow-file --method zero --execute </dev/null >/dev/null 2>&1; then
    echo "non-interactive execution must require --confirm" >&2
    exit 1
fi

if "$binary" erase "$target" --allow-file --method zero --execute \
    --confirm "$temporary/wrong.img" >/dev/null 2>&1; then
    echo "an incorrect confirmation must fail" >&2
    exit 1
fi

"$binary" erase "$target" --allow-file --method zero --execute \
    --confirm "$canonical" --verify >/dev/null
dd if=/dev/zero of="$expected" bs=131072 count=1 status=none
cmp "$target" "$expected"

"$binary" erase "$target" --allow-file --method ones --execute \
    --confirm "$canonical" --verify >/dev/null
tr '\000' '\377' <"$expected" >"$temporary/ones.img"
cmp "$target" "$temporary/ones.img"

if "$binary" erase "$target" --allow-file --method random --verify >/dev/null 2>&1; then
    echo "random read-back verification must be rejected" >&2
    exit 1
fi

echo "CLI safety tests passed"
