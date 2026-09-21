#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
# Type ASCII test commands through the keyboard of an isolated QEMU guest.
set -eu
monitor=${1:?monitor socket required}
text=${2:?text required}
commands=$(mktemp)
trap 'rm -f "$commands"' EXIT HUP INT TERM
printf '%s\n' "$text" | LC_ALL=C awk '
{
    for (i = 1; i <= length($0); ++i) {
        c = substr($0, i, 1)
        if (c ~ /[a-z0-9]/) key = c
        else if (c ~ /[A-Z]/) key = "shift-" tolower(c)
        else if (c == " ") key = "spc"
        else if (c == "/") key = "slash"
        else if (c == "-") key = "minus"
        else if (c == "_") key = "shift-minus"
        else if (c == ".") key = "dot"
        else if (c == "=") key = "equal"
        else if (c == ">") key = "shift-dot"
        else if (c == "&") key = "shift-7"
        else if (c == ";") key = "semicolon"
        else if (c == "\047") key = "apostrophe"
        else exit 2
        print "sendkey " key " 50"
    }
    print "sendkey ret 50"
}' > "$commands"
while IFS= read -r command; do
    printf '%s\n' "$command"
    sleep 0.10
done < "$commands" | socat - UNIX-CONNECT:"$monitor" >/dev/null
