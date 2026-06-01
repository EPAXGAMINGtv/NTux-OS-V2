#include <fcntl.h>

#include <errno.h>

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    errno = EINVAL;
    return -1;
}
