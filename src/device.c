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

#ifndef OMS_PROC_ROOT
#define OMS_PROC_ROOT "/proc"
#endif
#ifndef OMS_SYS_ROOT
#define OMS_SYS_ROOT "/sys"
#endif

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
                 OMS_SYS_ROOT "/dev/block/%u:%u",
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

    if (same_or_descendant(device_sysfs, target_sysfs) ||
        same_or_descendant(target_sysfs, device_sysfs)) {
        return true;
    }
    if (depth >= 16U ||
        snprintf(slaves_path, sizeof(slaves_path), "%s/slaves", device_sysfs) >=
            (int)sizeof(slaves_path)) {
        return true;
    }
    directory = opendir(slaves_path);
    if (directory == NULL) {
        /* A partition has no slaves directory; its parent disk does. */
        char partition_path[OMS_PATH_CAP];
        struct stat partition;
        if (errno == ENOENT &&
            snprintf(partition_path, sizeof(partition_path), "%s/partition", device_sysfs) <
                (int)sizeof(partition_path) && stat(partition_path, &partition) == 0) {
            return false;
        }
        return true;
    }
    for (;;) {
        char entry_path[OMS_PATH_CAP];
        char *resolved;
        bool depends;

        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            bool failed = errno != 0;
            return closedir(directory) != 0 || failed;
        }
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (snprintf(entry_path,
                     sizeof(entry_path),
                     "%s/%s",
                     slaves_path,
                     entry->d_name) >= (int)sizeof(entry_path)) {
            (void)closedir(directory);
            return true;
        }
        resolved = realpath(entry_path, NULL);
        if (resolved == NULL) {
            (void)closedir(directory);
            return true;
        }
        depends = device_depends_on_target(resolved, target_sysfs, depth + 1U);
        free(resolved);
        if (depends) {
            (void)closedir(directory);
            return true;
        }
    }
}

static bool target_is_mounted(const char *target_sysfs)
{
    FILE *mounts = fopen(OMS_PROC_ROOT "/self/mountinfo", "r");
    char *line = NULL;
    size_t capacity = 0U;
    bool mounted = false;
    bool saw_line = false;

    if (mounts == NULL) {
        return true;
    }
    while (getline(&line, &capacity, mounts) >= 0) {
        unsigned int device_major;
        unsigned int device_minor;
        char mounted_sysfs[OMS_PATH_CAP];

        saw_line = true;
        if (sscanf(line, "%*u %*u %u:%u", &device_major, &device_minor) != 2 ||
            strstr(line, " - ") == NULL) {
            mounted = true;
            break;
        }
        if (device_major == 0U) {
            continue; /* Pseudo filesystems have no sysfs block entry. */
        }
        if (sysfs_for_device(device_major,
                             device_minor,
                             mounted_sysfs,
                             sizeof(mounted_sysfs)) != 0 ||
            device_depends_on_target(mounted_sysfs, target_sysfs, 0U)) {
            mounted = true;
            break;
        }
    }
    mounted = mounted || ferror(mounts) != 0 || !feof(mounts) || !saw_line;
    free(line);
    (void)fclose(mounts);
    return mounted;
}

/* proc paths encode whitespace and backslashes as octal escapes. */
static bool decode_path(char *path)
{
    char *input = path;
    char *output = path;
    while (*input != '\0') {
        if (*input == '\\') {
            unsigned int value;
            if (strlen(input) < 4U || input[1] < '0' || input[1] > '7' ||
                input[2] < '0' || input[2] > '7' || input[3] < '0' || input[3] > '7') {
                return false;
            }
            value = (unsigned int)((input[1] - '0') * 64 + (input[2] - '0') * 8 + input[3] - '0');
            if (value == 0U || value > 255U) {
                return false;
            }
            *output++ = (char)value;
            input += 4;
        } else {
            *output++ = *input++;
        }
    }
    *output = '\0';
    return true;
}

static bool target_is_swap(const struct oms_target *target)
{
    FILE *swaps = fopen(OMS_PROC_ROOT "/swaps", "r");
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
        char swap_type[32];
        unsigned long long swap_size;
        unsigned long long swap_used;
        int priority;

        if (first_line) {
            first_line = false;
            if (strncmp(line, "Filename", 8U) != 0) {
                active = true;
                break;
            }
            continue;
        }
        if (sscanf(line, "%4095s %31s %llu %llu %d", path, swap_type, &swap_size,
                   &swap_used, &priority) != 5 || !decode_path(path) || stat(path, &status) != 0) {
            active = true;
            break;
        }
        if (target->kind == OMS_TARGET_REGULAR) {
            if (S_ISREG(status.st_mode) && status.st_dev == target->filesystem_id &&
                status.st_ino == target->inode) {
                active = true;
                break;
            }
            continue;
        }
        dev_t swap_device = S_ISBLK(status.st_mode) ? status.st_rdev : status.st_dev;
        if (sysfs_for_device(major(swap_device),
                             minor(swap_device),
                             swap_sysfs,
                             sizeof(swap_sysfs)) != 0 ||
            device_depends_on_target(swap_sysfs, target->sysfs_path, 0U)) {
            active = true;
            break;
        }
    }
    active = active || ferror(swaps) != 0 || !feof(swaps) || first_line;
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
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            bool failed = errno != 0;
            return closedir(directory) != 0 || failed;
        }
        if (entry->d_name[0] != '.') {
            (void)closedir(directory);
            return true;
        }
    }
}

static bool target_has_holders(const char *target_sysfs)
{
    DIR *directory = opendir(OMS_SYS_ROOT "/class/block");
    struct dirent *entry;
    bool found_target = false;

    if (directory == NULL) {
        return true;
    }
    for (;;) {
        char class_path[OMS_PATH_CAP];
        char holders_path[OMS_PATH_CAP];
        char *resolved;

        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            bool failed = errno != 0 || !found_target;
            return closedir(directory) != 0 || failed;
        }
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (snprintf(class_path,
                     sizeof(class_path),
                     OMS_SYS_ROOT "/class/block/%s",
                     entry->d_name) >= (int)sizeof(class_path)) {
            (void)closedir(directory);
            return true;
        }
        resolved = realpath(class_path, NULL);
        if (resolved == NULL) {
            (void)closedir(directory);
            return true;
        }
        if (!same_or_descendant(resolved, target_sysfs) &&
            !same_or_descendant(target_sysfs, resolved)) {
            free(resolved);
            continue;
        }
        found_target = true;
        if (snprintf(holders_path,
                     sizeof(holders_path),
                     "%s/holders",
                     resolved) >= (int)sizeof(holders_path)) {
            free(resolved);
            (void)closedir(directory);
            return true;
        }
        free(resolved);
        if (directory_has_entries(holders_path)) {
            (void)closedir(directory);
            return true;
        }
    }
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

static bool file_has_loop_holder(const struct oms_target *target)
{
    DIR *directory = opendir(OMS_SYS_ROOT "/class/block");
    if (directory == NULL) {
        return true;
    }
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            bool failed = errno != 0;
            return closedir(directory) != 0 || failed;
        }
        if (strncmp(entry->d_name, "loop", 4U) != 0) {
            continue;
        }
        char backing_path[OMS_PATH_CAP];
        if (snprintf(backing_path, sizeof(backing_path), OMS_SYS_ROOT "/class/block/%s/loop/backing_file",
                     entry->d_name) >= (int)sizeof(backing_path)) {
            (void)closedir(directory);
            return true;
        }
        FILE *file = fopen(backing_path, "r");
        if (file == NULL) {
            if (errno == ENOENT) {
                continue;
            }
            (void)closedir(directory);
            return true;
        }
        char backing[OMS_PATH_CAP];
        bool readable = fgets(backing, sizeof(backing), file) != NULL;
        (void)fclose(file);
        if (!readable || strchr(backing, '\n') == NULL) {
            (void)closedir(directory);
            return true;
        }
        backing[strcspn(backing, "\n")] = '\0';
        if (!decode_path(backing)) {
            (void)closedir(directory);
            return true;
        }
        struct stat status;
        if (stat(backing, &status) != 0 || strcmp(backing, target->path) == 0 ||
            (status.st_dev == target->filesystem_id && status.st_ino == target->inode)) {
            (void)closedir(directory);
            return true;
        }
    }
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

static int read_sysfs_number(const char *base, const char *name, uint64_t *number)
{
    char path[OMS_PATH_CAP];
    unsigned long long value;
    if (snprintf(path, sizeof(path), "%s/%s", base, name) >= (int)sizeof(path)) {
        return -1;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    int result = fscanf(file, "%llu", &value) == 1 ? 0 : -1;
    (void)fclose(file);
    if (result == 0) {
        *number = (uint64_t)value;
    }
    return result;
}

static int inspect_block(const struct stat *status,
                         struct oms_target *target,
                         char *error,
                         size_t error_size)
{
    uint64_t read_only = 1U;
    uint64_t sectors = 0U;
    char flag_path[OMS_PATH_CAP];

    if (sysfs_for_device(major(status->st_rdev),
                         minor(status->st_rdev),
                         target->sysfs_path,
                         sizeof(target->sysfs_path)) != 0) {
        set_error(error, error_size, "cannot resolve the device in sysfs");
        return -1;
    }
    /* Inventory must work without write permissions. The erase path validates
       these values again using ioctls on its exclusively opened descriptor. */
    if (read_sysfs_number(target->sysfs_path, "size", &sectors) != 0 ||
        sectors > UINT64_MAX / 512U ||
        read_sysfs_number(target->sysfs_path, "ro", &read_only) != 0) {
        set_error(error, error_size, "cannot read device size or read-only state from sysfs");
        return -1;
    }

    target->kind = OMS_TARGET_BLOCK;
    target->size_bytes = sectors * 512U;
    target->device_id = status->st_rdev;
    if (read_sysfs_number(target->sysfs_path, "diskseq", &target->disk_sequence) != 0) {
        (void)read_sysfs_number(target->sysfs_path, "../diskseq", &target->disk_sequence);
    }
    target->read_only = read_only != 0;
    target->mounted = target_is_mounted(target->sysfs_path);
    target->swap_active = target_is_swap(target);
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
    target->links = status.st_nlink;
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
    target->swap_active = target_is_swap(target);
    target->has_holders = file_has_loop_holder(target);
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

static void print_json_string(FILE *output, const char *value)
{
    const unsigned char *p = (const unsigned char *)value;
    fputc('"', output);
    for (; *p != 0U; ++p) {
        if (*p == '"' || *p == '\\') {
            fprintf(output, "\\%c", *p);
        } else if (*p < 0x20U || *p == 0x7fU) {
            fprintf(output, "\\u%04x", *p);
        } else {
            fputc(*p, output);
        }
    }
    fputc('"', output);
}

void oms_target_identity(const struct oms_target *target, char *output, size_t size)
{
    (void)snprintf(output, size, "%llu:%llu:%llu:%llu:%llu", (unsigned long long)target->device_id,
                   (unsigned long long)target->filesystem_id, (unsigned long long)target->inode,
                   (unsigned long long)target->size_bytes, (unsigned long long)target->disk_sequence);
}

void oms_print_target_json(FILE *output, const struct oms_target *target)
{
    fputs("{\"path\":", output);
    print_json_string(output, target->path);
    fputs(",\"model\":", output);
    print_json_string(output, target->model);
    fprintf(output, ",\"kind\":\"%s\",\"size_bytes\":%llu,\"read_only\":%s,"
                    "\"mounted\":%s,\"swap_active\":%s,\"has_holders\":%s,\"removable\":%s,"
                    "\"identity\":\"%llu:%llu:%llu:%llu:%llu\"}",
            target->kind == OMS_TARGET_BLOCK ? "block" : "file",
            (unsigned long long)target->size_bytes,
            target->read_only ? "true" : "false", target->mounted ? "true" : "false",
            target->swap_active ? "true" : "false", target->has_holders ? "true" : "false",
            target->removable ? "true" : "false", (unsigned long long)target->device_id,
            (unsigned long long)target->filesystem_id, (unsigned long long)target->inode,
            (unsigned long long)target->size_bytes, (unsigned long long)target->disk_sequence);
}

int oms_list_targets(FILE *output, bool json)
{
    DIR *directory = opendir(OMS_SYS_ROOT "/class/block");
    struct dirent *entry;
    unsigned int found = 0U;

    if (directory == NULL) {
        fprintf(stderr, "Cannot enumerate block devices: %s\n", strerror(errno));
        return -1;
    }
    if (json) {
        fputc('[', output);
    } else {
        fprintf(output, "%-16s %-12s %-10s %-12s %s\n", "PATH", "SIZE", "REMOVABLE", "STATE", "MODEL");
    }
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
                     OMS_SYS_ROOT "/class/block/%s/partition",
                     entry->d_name) >= (int)sizeof(partition_path) ||
            stat(partition_path, &partition_status) == 0) {
            continue;
        }
        if (snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name) >=
            (int)sizeof(device_path)) {
            continue;
        }
        if (oms_inspect_target(device_path, false, &target, error, sizeof(error)) != 0) {
            fprintf(stderr, "Skipped %s: %s\n", device_path, error);
            continue;
        }
        if (json) {
            if (found > 0U) {
                fputc(',', output);
            }
            oms_print_target_json(output, &target);
            ++found;
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
    if (json) {
        fputs("]\n", output);
    } else if (found == 0U) {
        fprintf(output, "No supported block devices were visible.\n");
    }
    return 0;
}
