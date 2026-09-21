// SPDX-License-Identifier: Apache-2.0 OR MIT
/* Link-time syscall injection used only by build/oms-faults. */
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

ssize_t __real_pwrite64(int, const void *, size_t, off_t);
ssize_t __real_pread64(int, void *, size_t, off_t);
int __real_fsync(int);
int __real_close(int);
int __real_fstat64(int, struct stat *);

static int match(const char *name)
{
    const char *mode = getenv("OMS_TEST_FAULT");
    return mode != NULL && strcmp(mode, name) == 0;
}

ssize_t __wrap_pwrite64(int fd, const void *buffer, size_t count, off_t offset)
{
    static int calls = 0;
    ++calls;
    if (match("write-zero")) {
        return 0;
    }
    if (match("write-error")) {
        errno = EIO;
        return -1;
    }
    if (match("write-eintr") && calls == 1) {
        errno = EINTR;
        return -1;
    }
    if (match("short-write") && count > 4096U) {
        count = 4096U;
    }
    ssize_t result = __real_pwrite64(fd, buffer, count, offset);
    if (match("cancel") && calls == 1) {
        (void)raise(SIGTERM);
    }
    return result;
}

ssize_t __wrap_pread64(int fd, void *buffer, size_t count, off_t offset)
{
    if (match("read-eof")) {
        return 0;
    }
    ssize_t result = __real_pread64(fd, buffer, count, offset);
    if (result > 0 && match("verify-mismatch")) {
        ((unsigned char *)buffer)[0] ^= 0xff;
    }
    return result;
}

int __wrap_fsync(int fd)
{
    if (match("flush-error")) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (match("close-error")) {
        errno = EIO;
        return -1;
    }
    return result;
}

int __wrap_fstat64(int fd, struct stat *status)
{
    int result = __real_fstat64(fd, status);
    if (result == 0 && match("changed-size")) {
        ++status->st_size;
    }
    return result;
}
