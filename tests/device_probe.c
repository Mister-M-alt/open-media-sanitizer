// SPDX-License-Identifier: Apache-2.0 OR MIT
/* A fixture-only executable. It never opens block devices. */
#define OMS_PROC_ROOT "proc"
#define OMS_SYS_ROOT "sys"
#include "../src/device.c"

int main(int argc, char **argv)
{
    if (argc != 3) {
        return 2;
    }
    bool busy;
    if (strcmp(argv[1], "mounted") == 0) {
        busy = target_is_mounted(argv[2]);
    } else if (strcmp(argv[1], "holders") == 0) {
        busy = target_has_holders(argv[2]);
    } else if (strcmp(argv[1], "swap") == 0 || strcmp(argv[1], "loop") == 0) {
        struct stat status;
        if (stat(argv[2], &status) != 0) {
            return 2;
        }
        struct oms_target target = {.kind = OMS_TARGET_REGULAR,
            .filesystem_id = status.st_dev, .inode = status.st_ino};
        (void)snprintf(target.path, sizeof(target.path), "%s", argv[2]);
        busy = strcmp(argv[1], "swap") == 0 ? target_is_swap(&target) : file_has_loop_holder(&target);
    } else {
        return 2;
    }
    puts(busy ? "busy" : "idle");
    return 0;
}
