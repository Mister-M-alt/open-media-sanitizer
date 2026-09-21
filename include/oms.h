// SPDX-License-Identifier: Apache-2.0 OR MIT

#ifndef OMS_H
#define OMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define OMS_VERSION "0.3.0"
#define OMS_PATH_CAP 4096
#define OMS_NAME_CAP 256
#define OMS_MODEL_CAP 128

enum oms_target_kind {
    OMS_TARGET_BLOCK,
    OMS_TARGET_REGULAR
};

enum oms_method {
    OMS_METHOD_ZERO,
    OMS_METHOD_ONES,
    OMS_METHOD_RANDOM
};

struct oms_target {
    char path[OMS_PATH_CAP];
    char sysfs_path[OMS_PATH_CAP];
    char name[OMS_NAME_CAP];
    char model[OMS_MODEL_CAP];
    enum oms_target_kind kind;
    uint64_t size_bytes;
    uint64_t disk_sequence;
    dev_t device_id;
    dev_t filesystem_id;
    ino_t inode;
    bool read_only;
    bool removable;
    bool mounted;
    bool swap_active;
    bool has_holders;
    nlink_t links;
};

struct oms_erase_options {
    enum oms_method method;
    unsigned int passes;
    bool verify;
    bool execute;
    bool allow_regular;
    const char *confirmation;
    bool progress;
    const char *expected_identity;
};

int oms_inspect_target(const char *path,
                       bool allow_regular,
                       struct oms_target *target,
                       char *error,
                       size_t error_size);
int oms_list_targets(FILE *output, bool json);
void oms_print_target(FILE *output, const struct oms_target *target);
void oms_print_target_json(FILE *output, const struct oms_target *target);
void oms_target_identity(const struct oms_target *target, char *output, size_t size);
void oms_format_size(uint64_t bytes, char *output, size_t output_size);
const char *oms_method_name(enum oms_method method);

int oms_erase_target(const char *path,
                     const struct oms_erase_options *options,
                     char *error,
                     size_t error_size);

#endif
