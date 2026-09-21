#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 OR MIT
set -eu
config="$BINARIES_DIR/genimage-oms.cfg"
cat > "$config" <<'EOF'
image efi.vfat {
    vfat {
        extraargs = "-F 32"
        file EFI { image = "efi-part/EFI" }
    }
    size = 64M
}
image oms.img {
EOF
if [ -f "$BINARIES_DIR/grub.img" ]; then
    cp "$BUILD_DIR/grub2-2.12/build-i386-pc/grub-core/boot.img" "$BINARIES_DIR/boot.img"
    cat >> "$config" <<'EOF'
    hdimage { partition-table-type = "mbr" disk-signature = 0x6f6d7301 }
    partition bootcode {
        in-partition-table = "no"
        image = "boot.img"
        offset = 0
        size = 512
        holes = {"(440; 512)"}
    }
    partition grub {
        in-partition-table = "no"
        image = "grub.img"
        offset = 512
    }
    partition esp {
        partition-type = 0xef
        bootable = true
        offset = 1M
        image = "efi.vfat"
    }
    partition root {
        partition-type = 0x83
        image = "rootfs.ext4"
    }
EOF
else
    cat >> "$config" <<'EOF'
    hdimage { partition-table-type = "gpt" }
    partition esp {
        partition-type-uuid = U
        offset = 1M
        image = "efi.vfat"
    }
    partition root {
        partition-type-uuid = L
        partition-uuid = "6f6d7301-0000-4000-8000-000000000002"
        image = "rootfs.ext4"
    }
EOF
fi
printf '}\n' >> "$config"
support/scripts/genimage.sh -c "$config"
