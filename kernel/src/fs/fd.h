#ifndef FD_H
#define FD_H

#include <stddef.h>
#include <stdint.h>

#include <fs/devfs.h>
#include <fs/vfs.h>

#define FD_MAX 64

typedef enum {
    FD_KIND_NONE = 0,
    FD_KIND_FILE = 1,
    FD_KIND_DEV = 2
} fd_kind_t;

typedef struct {
    int used;
    fd_kind_t kind;
    int flags;
    uint64_t pos;
    char path[VFS_MAX_PATH];
    uint8_t* file_buf;
    size_t file_len;
    const devfs_ops_t* dev_ops;
    void* dev_ctx;
} fd_entry_t;

struct thread;

void fd_init_thread(struct thread* t);
void fd_close_all(struct thread* t);
int fd_open(const char* path, int flags);
long fd_read(int fd, void* out, size_t len);
long fd_write(int fd, const void* in, size_t len);
long fd_ioctl(int fd, uint64_t req, void* arg);
long fd_lseek(int fd, long offset, int whence);
int fd_close(int fd);
int fd_stat(int fd, size_t* out_size, int* out_is_dev);

#endif
