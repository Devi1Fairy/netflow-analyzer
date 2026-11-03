#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* 仅测试二进制：只允许真实/dev/pts/数字节点，拒绝物理串口路径。 */
static int is_pty_path(const char *path)
{
    if (path == NULL || strncmp(path, "/dev/pts/", 9U) != 0 ||
        path[9] == '\0') {
        return 0;
    }
    for (const char *p = path + 9; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

int __real_open(const char *path, int flags, ...);
int __real_ioctl(int fd, unsigned long request, ...);

int __wrap_open(const char *path, int flags, ...)
{
    if (!is_pty_path(path) || (flags & O_CREAT) != 0) {
        errno = EPERM;
        return -1;
    }
    return __real_open(path, flags);
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    char path[128];
    if (ttyname_r(fd, path, sizeof(path)) != 0 || !is_pty_path(path)) {
        errno = EPERM;
        return -1;
    }
    if (request == TIOCEXCL || request == TIOCNXCL) {
        return __real_ioctl(fd, request);
    }
    if (request != TIOCMBIC && request != TIOCMGET) {
        errno = EINVAL;
        return -1;
    }

    va_list arguments;
    va_start(arguments, request);
    int *value = va_arg(arguments, int *);
    va_end(arguments);
    if (value == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (request == TIOCMBIC) {
        const char *fail = getenv("BMS_TEST_MODEM_FAIL");
        if (*value != (TIOCM_DTR | TIOCM_RTS) ||
            (fail != NULL && strcmp(fail, "1") == 0)) {
            errno = EIO;
            return -1;
        }
    } else {
        *value = 0; /* PTY没有真实DTR/RTS，仅模拟该测试检查。 */
    }
    return 0;
}