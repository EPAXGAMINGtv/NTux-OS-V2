#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>

int chmod(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    return 0;
}

int fchmod(int fd, mode_t mode) {
    (void)fd;
    (void)mode;
    return 0;
}

mode_t umask(mode_t mask) {
    (void)mask;
    return 0;
}

int chown(const char *path, uid_t owner, gid_t group) {
    (void)path;
    (void)owner;
    (void)group;
    return 0;
}

int fchown(int fd, uid_t owner, gid_t group) {
    (void)fd;
    (void)owner;
    (void)group;
    return 0;
}
