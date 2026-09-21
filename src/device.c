// SPDX-License-Identifier: Apache-2.0 OR MIT

#include "oms.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static void set_error(char *error, size_t error_size, const char *format, ...)
{
    va_list arguments;

    if (error == NULL || error_size == 0U) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static int copy_text(char *destination,
                     size_t destination_size,
                     const char *source,
                     char *error,
                     size_t error_size)
{
    int length = snprintf(destination, destination_size, "%s", source);

    if (length < 0 || (size_t)length >= destination_size) {
        set_error(error, error_size, "path or label is too long");
        return -1;
    }
    return 0;
}
static bool same_or_descendant(const char *candidate, const char *parent)
{
    size_t parent_length = strlen(parent);

    return strncmp(candidate, parent, parent_length) == 0 &&
           (candidate[parent_length] == '\0' || candidate[parent_length] == '/');
}

static int sysfs_for_device(unsigned int device_major,
                            unsigned int device_minor,
                            char *output,
                            size_t output_size)
{
    char link_path[128];
    char *resolved;

    if (snprintf(link_path,
                 sizeof(link_path),
                 "/sys/dev/block/%u:%u",
                 device_major,
                 device_minor) >= (int)sizeof(link_path)) {
        return -1;
    }
    resolved = realpath(link_path, NULL);
    if (resolved == NULL) {
        return -1;
    }
    if (snprintf(output, output_size, "%s", resolved) >= (int)output_size) {
        free(resolved);
        return -1;
    }
    free(resolved);
    return 0;
}

static bool device_depends_on_target(const char *device_sysfs,
                                     const char *target_sysfs,
                                     unsigned int depth)
{
    char slaves_path[OMS_PATH_CAP];
    DIR *directory;
    struct dirent *entry;

    if (same_or_descendant(device_sysfs, target_sysfs)) {
        return true;
    }
    if (depth >= 16U ||
        snprintf(slaves_path, sizeof(slaves_path), "%s/slaves", device_sysfs) >=
            (int)sizeof(slaves_path)) {
        return false;
    }
    directory = opendir(slaves_path);
    if (directory == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char entry_path[OMS_PATH_CAP];
        char *resolved;
        bool depends;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (snprintf(entry_path,
                     sizeof(entry_path),
                     "%s/%s",
                     slaves_path,
                     entry->d_name) >= (int)sizeof(entry_path)) {
            continue;
        }
        resolved = realpath(entry_path, NULL);
        if (resolved == NULL) {
            continue;
        }
        depends = device_depends_on_target(resolved, target_sysfs, depth + 1U);
        free(resolved);
        if (depends) {
            (void)closedir(directory);
            return true;
        }
    }
    (void)closedir(directory);
    return false;
}

static bool target_is_mounted(const char *target_sysfs)
{
    FILE *mounts = fopen("/proc/self/mountinfo", "r");
    char *line = NULL;
    size_t capacity = 0U;
    bool mounted = false;

    if (mounts == NULL) {
        return true;
    }
    while (getline(&line, &capacity, mounts) >= 0) {
        unsigned int device_major;
        unsigned int device_minor;
        char mounted_sysfs[OMS_PATH_CAP];

        if (sscanf(line, "%*u %*u %u:%u", &device_major, &device_minor) != 2) {
            continue;
        }
        if (sysfs_for_device(device_major,
                             device_minor,
                             mounted_sysfs,
                             sizeof(mounted_sysfs)) == 0 &&
            device_depends_on_target(mounted_sysfs, target_sysfs, 0U)) {
            mounted = true;
            break;
        }
    }
    free(line);
    (void)fclose(mounts);
    return mounted;
}

static bool target_is_swap(const char *target_sysfs)
{
    FILE *swaps = fopen("/proc/swaps", "r");
    char *line = NULL;
    size_t capacity = 0U;
    bool active = false;
    bool first_line = true;

    if (swaps == NULL) {
        return true;
    }
    while (getline(&line, &capacity, swaps) >= 0) {
        char path[OMS_PATH_CAP];
        struct stat status;
        char swap_sysfs[OMS_PATH_CAP];

        if (first_line) {
            first_line = false;
            continue;
        }
        if (sscanf(line, "%4095s", path) != 1 || stat(path, &status) != 0 ||
            !S_ISBLK(status.st_mode)) {
            continue;
        }
        if (sysfs_for_device(major(status.st_rdev),
                             minor(status.st_rdev),
                             swap_sysfs,
                             sizeof(swap_sysfs)) == 0 &&
            device_depends_on_target(swap_sysfs, target_sysfs, 0U)) {
            active = true;
            break;
        }
    }
    free(line);
    (void)fclose(swaps);
    return active;
}

static bool directory_has_entries(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;

    if (directory == NULL) {
        return true;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] != '.') {
            (void)closedir(directory);
            return true;
        }
    }
    (void)closedir(directory);
    return false;
}

static bool target_has_holders(const char *target_sysfs)
{
    DIR *directory = opendir("/sys/class/block");
    struct dirent *entry;

    if (directory == NULL) {
        return true;
    }
    while ((entry = readdir(directory)) != NULL) {
        char class_path[OMS_PATH_CAP];
        char holders_path[OMS_PATH_CAP];
        char *resolved;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (snprintf(class_path,
                     sizeof(class_path),
                     "/sys/class/block/%s",
                     entry->d_name) >= (int)sizeof(class_path)) {
            continue;
        }
        resolved = realpath(class_path, NULL);
        if (resolved == NULL) {
            continue;
        }
        if (!same_or_descendant(resolved, target_sysfs)) {
            free(resolved);
            continue;
        }
        if (snprintf(holders_path,
                     sizeof(holders_path),
                     "%s/holders",
                     resolved) >= (int)sizeof(holders_path)) {
            free(resolved);
            continue;
        }
        free(resolved);
        if (directory_has_entries(holders_path)) {
            (void)closedir(directory);
            return true;
        }
    }
    (void)closedir(directory);
    return false;
}

static bool read_boolean_file(const char *path)
{
    FILE *file = fopen(path, "r");
    int value = 0;

    if (file == NULL) {
        return false;
    }
    if (fscanf(file, "%d", &value) != 1) {
        value = 0;
    }
    (void)fclose(file);
    return value != 0;
}

static void read_model(const char *sysfs_path, char *model, size_t model_size)
{
    char path[OMS_PATH_CAP];
    FILE *file;
    size_t length;

    model[0] = '\0';
    if (snprintf(path, sizeof(path), "%s/device/model", sysfs_path) >= (int)sizeof(path)) {
        return;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    if (fgets(model, (int)model_size, file) == NULL) {
        model[0] = '\0';
    }
    (void)fclose(file);
    length = strlen(model);
    while (length > 0U && (model[length - 1U] == '\n' || model[length - 1U] == ' ' ||
                            model[length - 1U] == '\t')) {
        model[--length] = '\0';
    }
}

static int inspect_block(const struct stat *status,
                         struct oms_target *target,
                         char *error,
                         size_t error_size)
{
    int descriptor;
    int read_only = 0;
    unsigned long long size = 0ULL;
    char flag_path[OMS_PATH_CAP];

    if (sysfs_for_device(major(status->st_rdev),
                         minor(status->st_rdev),
                         target->sysfs_path,
                         sizeof(target->sysfs_path)) != 0) {
        set_error(error, error_size, "cannot resolve the device in sysfs");
        return -1;
    }
    descriptor = open(target->path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        set_error(error, error_size, "cannot open %s: %s", target->path, strerror(errno));
        return -1;
    }
    if (ioctl(descriptor, BLKGETSIZE64, &size) != 0) {
        set_error(error, error_size, "cannot read device size: %s", strerror(errno));
        (void)close(descriptor);
        return -1;
    }
    if (ioctl(descriptor, BLKROGET, &read_only) != 0) {
        read_only = 1;
    }
    (void)close(descriptor);

    target->kind = OMS_TARGET_BLOCK;
    target->size_bytes = (uint64_t)size;
    target->device_id = status->st_rdev;
    target->read_only = read_only != 0;
    target->mounted = target_is_mounted(target->sysfs_path);
    target->swap_active = target_is_swap(target->sysfs_path);
    target->has_holders = target_has_holders(target->sysfs_path);
    if (snprintf(flag_path, sizeof(flag_path), "%s/removable", target->sysfs_path) <
        (int)sizeof(flag_path)) {
        target->removable = read_boolean_file(flag_path);
    }
    read_model(target->sysfs_path, target->model, sizeof(target->model));
    return 0;
}

int oms_inspect_target(const char *path,
                       bool allow_regular,
                       struct oms_target *target,
                       char *error,
                       size_t error_size)
{
    struct stat status;
    char *resolved;
    const char *base_name;

    if (path == NULL || path[0] == '\0' || target == NULL) {
        set_error(error, error_size, "a target path is required");
        return -1;
    }
    memset(target, 0, sizeof(*target));
    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        set_error(error, error_size, "cannot resolve %s: %s", path, strerror(errno));
        return -1;
    }
    if (copy_text(target->path,
                  sizeof(target->path),
                  resolved,
                  error,
                  error_size) != 0) {
        free(resolved);
        return -1;
    }
    free(resolved);

    if (stat(target->path, &status) != 0) {
        set_error(error, error_size, "cannot inspect %s: %s", target->path, strerror(errno));
        return -1;
    }
    base_name = strrchr(target->path, '/');
    base_name = base_name == NULL ? target->path : base_name + 1;
    if (copy_text(target->name,
                  sizeof(target->name),
                  base_name,
                  error,
                  error_size) != 0) {
        return -1;
    }

    target->filesystem_id = status.st_dev;
    target->inode = status.st_ino;
    if (S_ISBLK(status.st_mode)) {
        return inspect_block(&status, target, error, error_size);
    }
    if (!S_ISREG(status.st_mode)) {
        set_error(error, error_size, "%s is neither a block device nor a regular file", target->path);
        return -1;
    }
    if (!allow_regular) {
        set_error(error,
                  error_size,
                  "%s is a regular file; use --allow-file only for controlled testing",
                  target->path);
        return -1;
    }
    if (status.st_size < 0) {
        set_error(error, error_size, "the file reports an invalid size");
        return -1;
    }
    target->kind = OMS_TARGET_REGULAR;
    target->size_bytes = (uint64_t)status.st_size;
    target->read_only = access(target->path, W_OK) != 0;
    return 0;
}

void oms_format_size(uint64_t bytes, char *output, size_t output_size)
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = (double)bytes;
    size_t unit = 0U;

    while (value >= 1024.0 && unit + 1U < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0U) {
        (void)snprintf(output, output_size, "%llu %s", (unsigned long long)bytes, units[unit]);
    } else {
        (void)snprintf(output, output_size, "%.2f %s", value, units[unit]);
    }
}

void oms_print_target(FILE *output, const struct oms_target *target)
{
    char size[64];
    const char *state = "ready";

    oms_format_size(target->size_bytes, size, sizeof(size));
    if (target->read_only) {
        state = "read-only";
    } else if (target->mounted) {
        state = "mounted";
    } else if (target->swap_active) {
        state = "active swap";
    } else if (target->has_holders) {
        state = "used by another block device";
    }

    fprintf(output, "Target\n");
    fprintf(output, "  Path:       %s\n", target->path);
    fprintf(output,
            "  Type:       %s\n",
            target->kind == OMS_TARGET_BLOCK ? "block device" : "regular file");
    fprintf(output, "  Size:       %s (%llu bytes)\n", size, (unsigned long long)target->size_bytes);
    fprintf(output, "  State:      %s\n", state);
    if (target->kind == OMS_TARGET_BLOCK) {
        fprintf(output, "  Removable:  %s\n", target->removable ? "yes" : "no");
        fprintf(output, "  Model:      %s\n", target->model[0] == '\0' ? "unknown" : target->model);
    }
}

static bool is_virtual_name(const char *name)
{
    return strncmp(name, "loop", 4U) == 0 || strncmp(name, "ram", 3U) == 0 ||
           strncmp(name, "zram", 4U) == 0;
}

int oms_list_targets(FILE *output)
{
    DIR *directory = opendir("/sys/class/block");
    struct dirent *entry;
    unsigned int found = 0U;

    if (directory == NULL) {
        fprintf(stderr, "Cannot enumerate block devices: %s\n", strerror(errno));
        return -1;
    }
    fprintf(output, "%-16s %-12s %-10s %-12s %s\n", "PATH", "SIZE", "REMOVABLE", "STATE", "MODEL");
    while ((entry = readdir(directory)) != NULL) {
        char partition_path[OMS_PATH_CAP];
        char device_path[OMS_PATH_CAP];
        struct stat partition_status;
        struct oms_target target;
        char error[256];
        char size[64];
        const char *state;

        if (entry->d_name[0] == '.' || is_virtual_name(entry->d_name)) {
            continue;
        }
        if (snprintf(partition_path,
                     sizeof(partition_path),
                     "/sys/class/block/%s/partition",
                     entry->d_name) >= (int)sizeof(partition_path) ||
            stat(partition_path, &partition_status) == 0) {
            continue;
        }
        if (snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name) >=
            (int)sizeof(device_path)) {
            continue;
        }
        if (oms_inspect_target(device_path, false, &target, error, sizeof(error)) != 0) {
            continue;
        }
        oms_format_size(target.size_bytes, size, sizeof(size));
        if (target.read_only) {
            state = "read-only";
        } else if (target.mounted || target.swap_active || target.has_holders) {
            state = "in use";
        } else {
            state = "ready";
        }
        fprintf(output,
                "%-16s %-12s %-10s %-12s %s\n",
                target.path,
                size,
                target.removable ? "yes" : "no",
                state,
                target.model[0] == '\0' ? "unknown" : target.model);
        ++found;
    }
    (void)closedir(directory);
    if (found == 0U) {
        fprintf(output, "No supported block devices were visible.\n");
    }
    return 0;
}
