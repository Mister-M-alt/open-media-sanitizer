// SPDX-License-Identifier: Apache-2.0 OR MIT

#include "oms.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *output)
{
    fprintf(output,
            "Open Media Sanitizer %s\n"
            "\n"
            "Usage:\n"
            "  oms list\n"
            "  oms inspect TARGET [--allow-file]\n"
            "  oms erase TARGET [OPTIONS]\n"
            "\n"
            "Erase options:\n"
            "  --method zero|ones|random  Data pattern (default: zero)\n"
            "  --passes NUMBER            Number of passes, 1 through 16 (default: 1)\n"
            "  --verify                    Read back and check zero/ones patterns\n"
            "  --execute                   Perform the erase; otherwise show a plan\n"
            "  --confirm PATH              Exact canonical path for non-interactive use\n"
            "  --allow-file                Permit a regular file as the target\n"
            "  -h, --help                  Show this help\n"
            "  -V, --version               Show the version\n",
            OMS_VERSION);
}

static int parse_method(const char *value, enum oms_method *method)
{
    if (strcmp(value, "zero") == 0) {
        *method = OMS_METHOD_ZERO;
        return 0;
    }
    if (strcmp(value, "ones") == 0) {
        *method = OMS_METHOD_ONES;
        return 0;
    }
    if (strcmp(value, "random") == 0) {
        *method = OMS_METHOD_RANDOM;
        return 0;
    }
    return -1;
}

static int parse_passes(const char *value, unsigned int *passes)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1UL || parsed > 16UL ||
        parsed > UINT_MAX) {
        return -1;
    }
    *passes = (unsigned int)parsed;
    return 0;
}

static int command_inspect(int argc, char **argv)
{
    struct oms_target target;
    char error[512];
    const char *path = NULL;
    bool allow_regular = false;
    int i;

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--allow-file") == 0) {
            allow_regular = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "Only one target may be inspected.\n");
            return 2;
        }
    }

    if (path == NULL) {
        fprintf(stderr, "inspect requires a target.\n");
        return 2;
    }
    if (oms_inspect_target(path, allow_regular, &target, error, sizeof(error)) != 0) {
        fprintf(stderr, "Inspection failed: %s\n", error);
        return 1;
    }

    oms_print_target(stdout, &target);
    return 0;
}

static int command_erase(int argc, char **argv)
{
    struct oms_erase_options options = {
        .method = OMS_METHOD_ZERO,
        .passes = 1U,
        .verify = false,
        .execute = false,
        .allow_regular = false,
        .confirmation = NULL,
    };
    char error[512];
    const char *path = NULL;
    int i;

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--method") == 0) {
            if (++i >= argc || parse_method(argv[i], &options.method) != 0) {
                fprintf(stderr, "--method must be zero, ones, or random.\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--passes") == 0) {
            if (++i >= argc || parse_passes(argv[i], &options.passes) != 0) {
                fprintf(stderr, "--passes must be an integer from 1 through 16.\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--verify") == 0) {
            options.verify = true;
        } else if (strcmp(argv[i], "--execute") == 0) {
            options.execute = true;
        } else if (strcmp(argv[i], "--allow-file") == 0) {
            options.allow_regular = true;
        } else if (strcmp(argv[i], "--confirm") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--confirm requires a path.\n");
                return 2;
            }
            options.confirmation = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "Only one erase target is permitted.\n");
            return 2;
        }
    }

    if (path == NULL) {
        fprintf(stderr, "erase requires a target.\n");
        return 2;
    }
    if (options.verify && options.method == OMS_METHOD_RANDOM) {
        fprintf(stderr, "Read-back verification is available for zero and ones patterns.\n");
        return 2;
    }
    if (oms_erase_target(path, &options, error, sizeof(error)) != 0) {
        fprintf(stderr, "Erase failed: %s\n", error);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("oms %s\n", OMS_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        if (argc != 2) {
            fprintf(stderr, "list does not accept arguments.\n");
            return 2;
        }
        return oms_list_targets(stdout) == 0 ? 0 : 1;
    }
    if (strcmp(argv[1], "inspect") == 0) {
        return command_inspect(argc, argv);
    }
    if (strcmp(argv[1], "erase") == 0) {
        return command_erase(argc, argv);
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    print_usage(stderr);
    return 2;
}
