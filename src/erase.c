// SPDX-License-Identifier: Apache-2.0 OR MIT

#include "oms.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define OMS_BUFFER_SIZE (1024U * 1024U)

/* The Linux ioctl ABI is stable. Older cross-toolchain headers may omit this
 * definition even when the running kernel exports a disk sequence in sysfs. */
#ifndef BLKGETDISKSEQ
#define BLKGETDISKSEQ _IOR(0x12, 128, uint64_t)
#endif

static volatile sig_atomic_t interrupted = 0;
static bool machine_progress = false;
static unsigned int current_pass = 0U;
static unsigned int total_passes = 0U;

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

static void handle_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

const char *oms_method_name(enum oms_method method)
{
    switch (method) {
    case OMS_METHOD_ZERO:
        return "zero";
    case OMS_METHOD_ONES:
        return "ones";
    case OMS_METHOD_RANDOM:
        return "random";
    }
    return "unknown";
}

static int confirm_target(const struct oms_target *target,
                          const char *provided,
                          char *error,
                          size_t error_size)
{
    char response[OMS_PATH_CAP + 2U];
    size_t length;

    if (provided != NULL) {
        if (strcmp(provided, target->path) != 0) {
            set_error(error,
                      error_size,
                      "confirmation does not exactly match canonical path %s",
                      target->path);
            return -1;
        }
        return 0;
    }
    if (!isatty(STDIN_FILENO)) {
        set_error(error,
                  error_size,
                  "interactive confirmation is unavailable; pass --confirm %s",
                  target->path);
        return -1;
    }

    fprintf(stderr, "Type the full path '%s' to erase it: ", target->path);
    (void)fflush(stderr);
    if (fgets(response, (int)sizeof(response), stdin) == NULL) {
        set_error(error, error_size, "confirmation was not received");
        return -1;
    }
    length = strlen(response);
    if (length > 0U && response[length - 1U] == '\n') {
        response[--length] = '\0';
    }
    if (strcmp(response, target->path) != 0) {
        set_error(error, error_size, "confirmation did not match; no data was written");
        return -1;
    }
    return 0;
}

static int fill_random(int random_descriptor,
                       unsigned char *buffer,
                       size_t length,
                       char *error,
                       size_t error_size)
{
    size_t position = 0U;

    while (position < length) {
        if (interrupted != 0) {
            set_error(error, error_size, "operation interrupted; erase is incomplete");
            return -1;
        }
        ssize_t count = read(random_descriptor, buffer + position, length - position);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            set_error(error, error_size, "cannot read random data: %s",
                      count == 0 ? "unexpected end of random source" : strerror(errno));
            return -1;
        }
        position += (size_t)count;
    }
    return 0;
}

static int write_all_at(int descriptor,
                        const unsigned char *buffer,
                        size_t length,
                        uint64_t offset,
                        char *error,
                        size_t error_size)
{
    size_t position = 0U;

    while (position < length) {
        if (interrupted != 0) {
            set_error(error, error_size, "operation interrupted; erase is incomplete");
            return -1;
        }
        ssize_t count = pwrite(descriptor,
                               buffer + position,
                               length - position,
                               (off_t)(offset + position));

        if (count < 0 && errno == EINTR) {
            if (interrupted != 0) {
                set_error(error, error_size, "operation interrupted");
                return -1;
            }
            continue;
        }
        if (count <= 0) {
            set_error(error, error_size, "write failed at byte %llu: %s",
                      (unsigned long long)(offset + position),
                      count == 0 ? "no write progress" : strerror(errno));
            return -1;
        }
        position += (size_t)count;
    }
    return 0;
}

static int read_all_at(int descriptor,
                       unsigned char *buffer,
                       size_t length,
                       uint64_t offset,
                       char *error,
                       size_t error_size)
{
    size_t position = 0U;

    while (position < length) {
        if (interrupted != 0) {
            set_error(error, error_size, "operation interrupted; erase is incomplete");
            return -1;
        }
        ssize_t count = pread(descriptor,
                              buffer + position,
                              length - position,
                              (off_t)(offset + position));

        if (count < 0 && errno == EINTR) {
            if (interrupted != 0) {
                set_error(error, error_size, "operation interrupted");
                return -1;
            }
            continue;
        }
        if (count <= 0) {
            set_error(error, error_size, "read failed at byte %llu: %s",
                      (unsigned long long)(offset + position),
                      count == 0 ? "unexpected end of target" : strerror(errno));
            return -1;
        }
        position += (size_t)count;
    }
    return 0;
}

static unsigned int progress_percentage(uint64_t complete, uint64_t total)
{
    return total == 0U ? 100U :
        (unsigned int)(((long double)complete / (long double)total) * 100.0L);
}

static void show_progress(const char *phase, uint64_t complete, uint64_t total)
{
    unsigned int percentage = progress_percentage(complete, total);

    if (machine_progress) {
        fprintf(stderr, "OMS_PROGRESS %s %u %u %llu %llu\n", phase, current_pass,
                total_passes, (unsigned long long)complete, (unsigned long long)total);
    } else if (isatty(STDERR_FILENO)) {
        fprintf(stderr, "\r%-10s %3u%%", phase, percentage);
        (void)fflush(stderr);
        if (complete == total) {
            fputc('\n', stderr);
        }
    }
}

static int write_pass(int descriptor,
                      uint64_t size,
                      enum oms_method method,
                      unsigned char *buffer,
                      int random_descriptor,
                      char *error,
                      size_t error_size)
{
    uint64_t offset = 0U;
    unsigned int last_percentage = UINT_MAX;

    while (offset < size) {
        uint64_t remaining = size - offset;
        size_t length = remaining < OMS_BUFFER_SIZE ? (size_t)remaining : OMS_BUFFER_SIZE;
        unsigned int percentage;

        if (interrupted != 0) {
            set_error(error, error_size, "operation interrupted");
            return -1;
        }
        if (method == OMS_METHOD_RANDOM) {
            if (fill_random(random_descriptor, buffer, length, error, error_size) != 0) {
                return -1;
            }
        } else {
            memset(buffer, method == OMS_METHOD_ZERO ? 0x00 : 0xff, length);
        }
        if (write_all_at(descriptor, buffer, length, offset, error, error_size) != 0) {
            return -1;
        }
        offset += length;
        percentage = progress_percentage(offset, size);
        if (percentage != last_percentage) {
            show_progress("writing", offset, size);
            last_percentage = percentage;
        }
    }
    if (fsync(descriptor) != 0) {
        set_error(error, error_size, "cannot flush writes: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int verify_pattern(int descriptor,
                          uint64_t size,
                          unsigned char expected,
                          unsigned char *buffer,
                          char *error,
                          size_t error_size)
{
    uint64_t offset = 0U;
    unsigned int last_percentage = UINT_MAX;

#if defined(POSIX_FADV_DONTNEED)
    (void)posix_fadvise(descriptor, 0, 0, POSIX_FADV_DONTNEED);
#endif
    while (offset < size) {
        uint64_t remaining = size - offset;
        size_t length = remaining < OMS_BUFFER_SIZE ? (size_t)remaining : OMS_BUFFER_SIZE;
        size_t index;
        unsigned int percentage;

        if (interrupted != 0) {
            set_error(error, error_size, "operation interrupted");
            return -1;
        }
        if (read_all_at(descriptor, buffer, length, offset, error, error_size) != 0) {
            return -1;
        }
        for (index = 0U; index < length; ++index) {
            if (buffer[index] != expected) {
                set_error(error,
                          error_size,
                          "verification mismatch at byte %llu",
                          (unsigned long long)(offset + index));
                return -1;
            }
        }
        offset += length;
        percentage = progress_percentage(offset, size);
        if (percentage != last_percentage) {
            show_progress("verifying", offset, size);
            last_percentage = percentage;
        }
    }
    return 0;
}

static bool same_open_target(const struct oms_target *target, const struct stat *status)
{
    if (target->kind == OMS_TARGET_BLOCK) {
        return S_ISBLK(status->st_mode) && status->st_rdev == target->device_id;
    }
    return S_ISREG(status->st_mode) && status->st_dev == target->filesystem_id &&
           status->st_ino == target->inode && status->st_nlink == 1U &&
           status->st_size >= 0 && (uint64_t)status->st_size == target->size_bytes;
}

int oms_erase_target(const char *path,
                     const struct oms_erase_options *options,
                     char *error,
                     size_t error_size)
{
    struct oms_target target;
    struct oms_target current;
    struct stat open_status;
    struct sigaction action;
    struct sigaction old_interrupt;
    struct sigaction old_terminate;
    bool handlers_installed = false;
    bool writes_attempted = false;
    unsigned char *buffer = NULL;
    int descriptor = -1;
    int random_descriptor = -1;
    int open_flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    unsigned int pass;
    int result = -1;

    if (options == NULL || options->passes < 1U || options->passes > 16U ||
        (options->method != OMS_METHOD_ZERO && options->method != OMS_METHOD_ONES &&
         options->method != OMS_METHOD_RANDOM) ||
        (options->verify && options->method == OMS_METHOD_RANDOM)) {
        set_error(error, error_size, "invalid erase options");
        return -1;
    }
    if (oms_inspect_target(path,
                           options->allow_regular,
                           &target,
                           error,
                           error_size) != 0) {
        return -1;
    }
    if (options->expected_identity != NULL) {
        char identity[160];
        oms_target_identity(&target, identity, sizeof(identity));
        if (strcmp(identity, options->expected_identity) != 0) {
            set_error(error, error_size, "the target identity changed; inspect and confirm it again");
            return -1;
        }
    }
    if (target.kind == OMS_TARGET_REGULAR && target.links != 1U) {
        set_error(error, error_size, "test files must have exactly one hard link");
        return -1;
    }
    oms_print_target(stdout, &target);
    printf("Plan\n");
    printf("  Method:     %s\n", oms_method_name(options->method));
    printf("  Passes:     %u\n", options->passes);
    printf("  Verify:     %s\n", options->verify ? "yes" : "no");

    if (target.size_bytes == 0U) {
        set_error(error, error_size, "the target is empty");
        return -1;
    }
    if (target.size_bytes > (uint64_t)INT64_MAX) {
        set_error(error, error_size, "the target is too large for this build");
        return -1;
    }
    if (target.read_only) {
        set_error(error, error_size, "the target is read-only");
        return -1;
    }
    if (target.mounted || target.swap_active || target.has_holders) {
        set_error(error, error_size, "the target is active or its usage could not be determined");
        return -1;
    }
    if (!options->execute) {
        printf("\nDry run only. Add --execute to permit writes.\n");
        return 0;
    }
    if (target.kind == OMS_TARGET_BLOCK && geteuid() != 0) {
        set_error(error, error_size, "block-device erasure requires root privileges");
        return -1;
    }
    if (confirm_target(&target,
                       options->confirmation,
                       error,
                       error_size) != 0) {
        return -1;
    }

    if (oms_inspect_target(target.path,
                           options->allow_regular,
                           &current,
                           error,
                           error_size) != 0) {
        return -1;
    }
    if (strcmp(current.path, target.path) != 0 || current.kind != target.kind ||
        current.size_bytes != target.size_bytes || current.device_id != target.device_id ||
        current.filesystem_id != target.filesystem_id || current.inode != target.inode ||
        current.disk_sequence != target.disk_sequence) {
        set_error(error, error_size, "the target changed after confirmation");
        return -1;
    }
    if (current.read_only || current.mounted || current.swap_active || current.has_holders) {
        set_error(error, error_size, "the target became active after confirmation");
        return -1;
    }

#ifdef O_EXCL
    if (target.kind == OMS_TARGET_BLOCK) {
        open_flags |= O_EXCL;
    }
#endif
    descriptor = open(target.path, open_flags);
    if (descriptor < 0) {
        set_error(error, error_size, "cannot open target for writing: %s", strerror(errno));
        goto cleanup;
    }
    if (fstat(descriptor, &open_status) != 0 || !same_open_target(&target, &open_status)) {
        set_error(error, error_size, "the opened target does not match the inspected target");
        goto cleanup;
    }
    if (target.kind == OMS_TARGET_BLOCK) {
        unsigned long long opened_size = 0ULL;
        int read_only = 1;
        if (ioctl(descriptor, BLKGETSIZE64, &opened_size) != 0 ||
            ioctl(descriptor, BLKROGET, &read_only) != 0 || read_only != 0 ||
            opened_size != target.size_bytes) {
            set_error(error, error_size, "the opened device size or write permissions changed");
            goto cleanup;
        }
        if (target.disk_sequence != 0U) {
            unsigned long long disk_sequence = 0ULL;
            if (ioctl(descriptor, BLKGETDISKSEQ, &disk_sequence) != 0 ||
                disk_sequence != target.disk_sequence) {
                set_error(error, error_size, "the device was replaced; inspect and confirm it again");
                goto cleanup;
            }
        }
    } else if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        set_error(error, error_size, "the test file is locked by another operation");
        goto cleanup;
    }
    buffer = malloc(OMS_BUFFER_SIZE);
    if (buffer == NULL) {
        set_error(error, error_size, "cannot allocate the write buffer");
        goto cleanup;
    }
    if (options->method == OMS_METHOD_RANDOM) {
        random_descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (random_descriptor < 0) {
            set_error(error, error_size, "cannot open the system random source: %s", strerror(errno));
            goto cleanup;
        }
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    (void)sigemptyset(&action.sa_mask);
    interrupted = 0;
    if (sigaction(SIGINT, &action, &old_interrupt) != 0) {
        set_error(error, error_size, "cannot install the interrupt handler");
        goto cleanup;
    }
    if (sigaction(SIGTERM, &action, &old_terminate) != 0) {
        (void)sigaction(SIGINT, &old_interrupt, NULL);
        set_error(error, error_size, "cannot install the termination handler");
        goto cleanup;
    }
    handlers_installed = true;
    machine_progress = options->progress;
    total_passes = options->passes;

    for (pass = 1U; pass <= options->passes; ++pass) {
        current_pass = pass;
        writes_attempted = true;
        printf("Pass %u of %u: writing %s pattern\n",
               pass,
               options->passes,
               oms_method_name(options->method));
        if (write_pass(descriptor,
                       target.size_bytes,
                       options->method,
                       buffer,
                       random_descriptor,
                       error,
                       error_size) != 0) {
            goto cleanup;
        }
    }
    if (options->verify) {
        unsigned char expected = options->method == OMS_METHOD_ZERO ? 0x00 : 0xff;

        printf("Read-back verification\n");
        if (verify_pattern(descriptor,
                           target.size_bytes,
                           expected,
                           buffer,
                           error,
                           error_size) != 0) {
            goto cleanup;
        }
    }
    if (interrupted != 0 || fstat(descriptor, &open_status) != 0 ||
        !same_open_target(&target, &open_status)) {
        set_error(error, error_size, "operation interrupted or target changed; erase is incomplete");
        goto cleanup;
    }
    result = 0;

cleanup:
    free(buffer);
    if (random_descriptor >= 0) {
        (void)close(random_descriptor);
    }
    if (descriptor >= 0) {
        if (result != 0 && writes_attempted) {
            (void)fsync(descriptor);
        }
        if (close(descriptor) != 0 && result == 0) {
            set_error(error, error_size, "cannot close target: %s", strerror(errno));
            result = -1;
        }
    }
    if (handlers_installed) {
        if (interrupted != 0 && result == 0) {
            set_error(error, error_size, "operation interrupted; erase is incomplete");
            result = -1;
        }
        (void)sigaction(SIGINT, &old_interrupt, NULL);
        (void)sigaction(SIGTERM, &old_terminate, NULL);
    }
    if (result == 0) {
        printf("Erase completed for %s.\n", target.path);
    }
    return result;
}
