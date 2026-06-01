#include <fs/fd.h>

#include <sched/thread.h>
#include <fs/fs.h>
#include <mm/kmalloc.h>
#include <lib/string.h>

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0040

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static thread_t* fd_current_thread(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS) return NULL;
    return thread_list[tid];
}

static int fd_can_read(const fd_entry_t* e) {
    if (!e) return 0;
    int mode = e->flags & 0x3;
    return (mode == O_RDONLY || mode == O_RDWR);
}

static int fd_can_write(const fd_entry_t* e) {
    if (!e) return 0;
    int mode = e->flags & 0x3;
    return (mode == O_WRONLY || mode == O_RDWR);
}

void fd_init_thread(thread_t* t) {
    if (!t) return;
    for (int i = 0; i < FD_MAX; ++i) {
        t->fds[i].used = 0;
        t->fds[i].kind = FD_KIND_NONE;
        t->fds[i].flags = 0;
        t->fds[i].pos = 0;
        t->fds[i].path[0] = '\0';
        t->fds[i].file_buf = NULL;
        t->fds[i].file_len = 0;
        t->fds[i].dev_ops = NULL;
        t->fds[i].dev_ctx = NULL;
    }
}

void fd_close_all(thread_t* t) {
    if (!t) return;
    for (int i = 0; i < FD_MAX; ++i) {
        if (!t->fds[i].used) continue;
        if (t->fds[i].file_buf) {
            kfree(t->fds[i].file_buf);
            t->fds[i].file_buf = NULL;
        }
        t->fds[i].used = 0;
        t->fds[i].kind = FD_KIND_NONE;
        t->fds[i].dev_ops = NULL;
        t->fds[i].dev_ctx = NULL;
    }
}

static int fd_alloc_slot(thread_t* t) {
    for (int i = 0; i < FD_MAX; ++i) {
        if (!t->fds[i].used) return i;
    }
    return -1;
}

static int fd_open_file(thread_t* t, const char* path, int flags) {
    size_t len = 0;
    if (!fs_exists(path)) {
        if ((flags & O_CREAT) == 0) return -1;
        char parent[VFS_MAX_PATH];
        char name[VFS_MAX_NAME];
        const char* slash = strrchr(path, '/');
        if (!slash || slash == path) return -1;
        size_t plen = (size_t)(slash - path);
        if (plen >= sizeof(parent)) return -1;
        memcpy(parent, path, plen);
        parent[plen] = '\0';
        strncpy(name, slash + 1, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        if (fs_create_file(parent, name, NULL, 0) != 0) return -1;
    }
    if (fs_read_file(path, NULL, 0, &len) != 0) return -1;
    uint8_t* buf = NULL;
    if (len > 0) {
        buf = kmalloc(len);
        if (!buf) return -1;
        if (fs_read_file(path, buf, len, &len) != 0) {
            kfree(buf);
            return -1;
        }
    }
    int fd = fd_alloc_slot(t);
    if (fd < 0) {
        if (buf) kfree(buf);
        return -1;
    }
    t->fds[fd].used = 1;
    t->fds[fd].kind = FD_KIND_FILE;
    t->fds[fd].flags = flags;
    t->fds[fd].pos = 0;
    strncpy(t->fds[fd].path, path, sizeof(t->fds[fd].path) - 1);
    t->fds[fd].path[sizeof(t->fds[fd].path) - 1] = '\0';
    t->fds[fd].file_buf = buf;
    t->fds[fd].file_len = len;
    return fd;
}

static int fd_open_dev(thread_t* t, const char* path, int flags) {
    const char* name = path + 5; /* skip /dev/ */
    const devfs_ops_t* ops = NULL;
    void* ctx = NULL;
    if (devfs_open(name, &ops, &ctx) != 0) return -1;
    int fd = fd_alloc_slot(t);
    if (fd < 0) return -1;
    t->fds[fd].used = 1;
    t->fds[fd].kind = FD_KIND_DEV;
    t->fds[fd].flags = flags;
    t->fds[fd].pos = 0;
    t->fds[fd].path[0] = '\0';
    t->fds[fd].file_buf = NULL;
    t->fds[fd].file_len = 0;
    t->fds[fd].dev_ops = ops;
    t->fds[fd].dev_ctx = ctx;
    return fd;
}

int fd_open(const char* path, int flags) {
    thread_t* t = fd_current_thread();
    if (!t || !path || path[0] != '/') return -1;
    if (strncmp(path, "/dev/", 5) == 0) {
        return fd_open_dev(t, path, flags);
    }
    return fd_open_file(t, path, flags);
}

long fd_read(int fd, void* out, size_t len) {
    thread_t* t = fd_current_thread();
    if (!t || fd < 0 || fd >= FD_MAX || !out) return -1;
    fd_entry_t* e = &t->fds[fd];
    if (!e->used || !fd_can_read(e)) return -1;
    if (e->kind == FD_KIND_DEV) {
        if (!e->dev_ops || !e->dev_ops->read) return -1;
        size_t got = 0;
        int rc = e->dev_ops->read(e->dev_ctx, out, len, &got);
        return (rc == 0) ? (long)got : -1;
    }
    if (e->kind != FD_KIND_FILE) return -1;
    if (e->pos >= e->file_len) return 0;
    size_t avail = e->file_len - (size_t)e->pos;
    size_t take = (len < avail) ? len : avail;
    memcpy(out, e->file_buf + e->pos, take);
    e->pos += take;
    return (long)take;
}

long fd_write(int fd, const void* in, size_t len) {
    thread_t* t = fd_current_thread();
    if (!t || fd < 0 || fd >= FD_MAX || !in) return -1;
    fd_entry_t* e = &t->fds[fd];
    if (!e->used || !fd_can_write(e)) return -1;
    if (e->kind == FD_KIND_DEV) {
        if (!e->dev_ops || !e->dev_ops->write) return -1;
        size_t done = 0;
        int rc = e->dev_ops->write(e->dev_ctx, in, len, &done);
        return (rc == 0) ? (long)done : -1;
    }
    if (e->kind != FD_KIND_FILE) return -1;
    if (e->pos != 0) return -1;
    if (fs_write_file(e->path, in, len) != 0) return -1;
    e->pos += len;
    return (long)len;
}

long fd_ioctl(int fd, uint64_t req, void* arg) {
    thread_t* t = fd_current_thread();
    if (!t || fd < 0 || fd >= FD_MAX) return -1;
    fd_entry_t* e = &t->fds[fd];
    if (!e->used) return -1;
    if (e->kind != FD_KIND_DEV || !e->dev_ops || !e->dev_ops->ioctl) return -1;
    return e->dev_ops->ioctl(e->dev_ctx, req, arg);
}

long fd_lseek(int fd, long offset, int whence) {
    thread_t* t = fd_current_thread();
    if (!t || fd < 0 || fd >= FD_MAX) return -1;
    fd_entry_t* e = &t->fds[fd];
    if (!e->used || e->kind != FD_KIND_FILE) return -1;
    long base = 0;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (long)e->pos;
    else if (whence == SEEK_END) base = (long)e->file_len;
    else return -1;
    long nv = base + offset;
    if (nv < 0) return -1;
    if ((size_t)nv > e->file_len) nv = (long)e->file_len;
    e->pos = (uint64_t)nv;
    return nv;
}

int fd_close(int fd) {
    thread_t* t = fd_current_thread();
    if (!t || fd < 0 || fd >= FD_MAX) return -1;
    fd_entry_t* e = &t->fds[fd];
    if (!e->used) return -1;
    if (e->file_buf) {
        kfree(e->file_buf);
        e->file_buf = NULL;
    }
    e->used = 0;
    e->kind = FD_KIND_NONE;
    e->dev_ops = NULL;
    e->dev_ctx = NULL;
    e->pos = 0;
    return 0;
}

int fd_stat(int fd, size_t* out_size, int* out_is_dev) {
    thread_t* t = fd_current_thread();
    if (!t || fd < 0 || fd >= FD_MAX) return -1;
    fd_entry_t* e = &t->fds[fd];
    if (!e->used) return -1;
    if (out_size) *out_size = (e->kind == FD_KIND_FILE) ? e->file_len : 0;
    if (out_is_dev) *out_is_dev = (e->kind == FD_KIND_DEV) ? 1 : 0;
    return 0;
}
