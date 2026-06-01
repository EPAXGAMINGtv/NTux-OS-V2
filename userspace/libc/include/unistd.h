#ifndef USER_UNISTD_H
#define USER_UNISTD_H

#include <sys/types.h>
#include <stdint.h>

int usleep(unsigned int usec);
int isatty(int fd);
int open(const char* path, int flags, ...);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int access(const char *path, int mode);
int unlink(const char *path);
int rmdir(const char *path);
int getpid(void);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int readlink(const char *path, char *buf, size_t bufsiz);
int symlink(const char *target, const char *linkpath);
int link(const char *oldpath, const char *newpath);
int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int gethostname(char *name, size_t len);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
void _exit(int code);
int brk(void *addr);
void *sbrk(intptr_t increment);
int getpagesize(void);

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#endif
