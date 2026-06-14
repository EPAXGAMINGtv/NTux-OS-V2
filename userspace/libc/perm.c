#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <syscall.h>

int chmod(const char *path, mode_t mode) {
    return (int)sys_chmod(path, (uint32_t)mode);
}
int fchmod(int fd, mode_t mode) {
    (void)fd; (void)mode;
    return 0;
}
mode_t umask(mode_t mask) {
    return (mode_t)sys_umask((uint32_t)mask);
}
int chown(const char *path, uid_t owner, gid_t group) {
    return (int)sys_chown(path, (uint32_t)owner, (uint32_t)group);
}
int fchown(int fd, uid_t owner, gid_t group) {
    (void)fd; (void)owner; (void)group;
    return 0;
}
