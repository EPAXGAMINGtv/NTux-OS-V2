#ifndef USER_SYS_STAT_H
#define USER_SYS_STAT_H

#include <sys/types.h>

struct stat {
    mode_t st_mode;
    off_t st_size;
    long st_mtime;
    long st_atime;
    long st_ctime;
    uint64_t st_ino;
    uint64_t st_dev;
    uid_t st_uid;
    gid_t st_gid;
    uint32_t st_nlink;
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000
#define S_IFBLK 0060000

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

int mkdir(const char *path, mode_t mode);
int rmdir(const char *path);
int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
mode_t umask(mode_t mask);

#endif
