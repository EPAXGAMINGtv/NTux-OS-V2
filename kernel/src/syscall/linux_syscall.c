#include <syscall/linux_syscall.h>

#include <sched/thread.h>
#include <drivers/framebuffer/kprint.h>
#include <fs/fd.h>
#include <fs/fs.h>
#include <interrupt/timer.h>
#include <mm/pmm.h>
#include <mm/umalloc.h>
#include <mm/vmm.h>
#include <lib/string.h>

#define LINUX_ENOSYS 38
#define LINUX_EINVAL 22
#define LINUX_EBADF 9
#define LINUX_EFAULT 14
#define LINUX_ENOMEM 12

#define LINUX_SYS_read 0
#define LINUX_SYS_write 1
#define LINUX_SYS_open 2
#define LINUX_SYS_close 3
#define LINUX_SYS_stat 4
#define LINUX_SYS_fstat 5
#define LINUX_SYS_lseek 8
#define LINUX_SYS_mmap 9
#define LINUX_SYS_mprotect 10
#define LINUX_SYS_munmap 11
#define LINUX_SYS_brk 12
#define LINUX_SYS_ioctl 16
#define LINUX_SYS_access 21
#define LINUX_SYS_pipe 22
#define LINUX_SYS_select 23
#define LINUX_SYS_sched_yield 24
#define LINUX_SYS_nanosleep 35
#define LINUX_SYS_getpid 39
#define LINUX_SYS_uname 63
#define LINUX_SYS_fcntl 72
#define LINUX_SYS_getdents 78
#define LINUX_SYS_getcwd 79
#define LINUX_SYS_chdir 80
#define LINUX_SYS_gettimeofday 96
#define LINUX_SYS_getuid 102
#define LINUX_SYS_getgid 104
#define LINUX_SYS_geteuid 107
#define LINUX_SYS_getegid 108
#define LINUX_SYS_arch_prctl 158
#define LINUX_SYS_gettid 186
#define LINUX_SYS_futex 202
#define LINUX_SYS_clock_gettime 228
#define LINUX_SYS_exit 60
#define LINUX_SYS_exit_group 231
#define LINUX_SYS_openat 257
#define LINUX_SYS_newfstatat 262
#define LINUX_SYS_unlinkat 263
#define LINUX_SYS_renameat 264
#define LINUX_SYS_mkdirat 258

#define LINUX_S_IFREG 0100000
#define LINUX_S_IFCHR 0020000

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_atime_nsec;
    int64_t st_mtime;
    int64_t st_mtime_nsec;
    int64_t st_ctime;
    int64_t st_ctime_nsec;
    int64_t __unused[3];
} linux_stat_t;

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} linux_utsname_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} linux_timespec_t;

static uint64_t linux_err(int err) {
    return (uint64_t)(-err);
}

static uint64_t linux_stat_path(const char* path, linux_stat_t* st) {
    if (!path || !st) return linux_err(LINUX_EFAULT);
    memset(st, 0, sizeof(*st));
    if (strncmp(path, "/dev/", 5) == 0) {
        st->st_mode = LINUX_S_IFCHR | 0600;
        return 0;
    }
    size_t len = 0;
    if (fs_read_file(path, NULL, 0, &len) != 0) {
        return linux_err(LINUX_EINVAL);
    }
    st->st_mode = LINUX_S_IFREG | 0644;
    st->st_size = (int64_t)len;
    return 0;
}

static uint64_t linux_fstat_fd(int fd, linux_stat_t* st) {
    if (!st) return linux_err(LINUX_EFAULT);
    memset(st, 0, sizeof(*st));
    size_t len = 0;
    int is_dev = 0;
    if (fd_stat(fd, &len, &is_dev) != 0) return linux_err(LINUX_EBADF);
    st->st_mode = is_dev ? (LINUX_S_IFCHR | 0600) : (LINUX_S_IFREG | 0644);
    st->st_size = (int64_t)len;
    return 0;
}

static uint64_t linux_uname(linux_utsname_t* out) {
    if (!out) return linux_err(LINUX_EFAULT);
    memset(out, 0, sizeof(*out));
    strncpy(out->sysname, "NTux", sizeof(out->sysname) - 1);
    strncpy(out->nodename, "ntux", sizeof(out->nodename) - 1);
    strncpy(out->release, "0.1", sizeof(out->release) - 1);
    strncpy(out->version, "NTux kernel", sizeof(out->version) - 1);
    strncpy(out->machine, "x86_64", sizeof(out->machine) - 1);
    strncpy(out->domainname, "local", sizeof(out->domainname) - 1);
    return 0;
}

static uint64_t linux_clock_gettime(int clock_id, linux_timespec_t* ts) {
    if (!ts) return linux_err(LINUX_EFAULT);
    (void)clock_id;
    uint64_t ticks = get_tick_count();
    uint32_t hz = timer_get_hz();
    ts->tv_sec = (int64_t)(ticks / (hz ? hz : TIMER_HZ));
    uint64_t rem = ticks % (hz ? hz : TIMER_HZ);
    ts->tv_nsec = (int64_t)((1000000000ull * rem) / (hz ? hz : TIMER_HZ));
    return 0;
}

static uint64_t linux_nanosleep(const linux_timespec_t* req, linux_timespec_t* rem) {
    if (!req) return linux_err(LINUX_EFAULT);
    uint64_t ns = (uint64_t)req->tv_sec * 1000000000ull + (uint64_t)req->tv_nsec;
    uint32_t hz = timer_get_hz();
    uint64_t ticks = ns / (1000000000ull / (hz ? hz : TIMER_HZ));
    sleep((uint32_t)ticks);
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

static uintptr_t g_brk = USER_HEAP_START;
static uintptr_t g_brk_mapped_end = USER_HEAP_START;

static uint64_t linux_brk(uintptr_t new_brk) {
    if (new_brk == 0) return (uint64_t)g_brk;
    if (new_brk < USER_HEAP_START) return linux_err(LINUX_EINVAL);
    while (g_brk_mapped_end < new_brk) {
        void* phys = pmm_alloc_page();
        if (!phys) return linux_err(LINUX_ENOMEM);
        if (!vmm_map_page((void*)g_brk_mapped_end, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
            pmm_free_page(phys);
            return linux_err(LINUX_ENOMEM);
        }
        g_brk_mapped_end += PAGE_SIZE;
    }
    g_brk = new_brk;
    return (uint64_t)g_brk;
}

uint64_t linux_syscall_dispatch(uint64_t num,
                                uint64_t a0,
                                uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5) {
    (void)a4;
    (void)a5;

    switch (num) {
        case LINUX_SYS_read:
            return (uint64_t)fd_read((int)a0, (void*)a1, (size_t)a2);
        case LINUX_SYS_write:
            return (uint64_t)fd_write((int)a0, (const void*)a1, (size_t)a2);
        case LINUX_SYS_open:
            return (uint64_t)fd_open((const char*)a0, (int)a1);
        case LINUX_SYS_openat:
            return (uint64_t)fd_open((const char*)a1, (int)a2);
        case LINUX_SYS_close:
            return (uint64_t)fd_close((int)a0);
        case LINUX_SYS_lseek:
            return (uint64_t)fd_lseek((int)a0, (long)a1, (int)a2);
        case LINUX_SYS_ioctl:
            return (uint64_t)fd_ioctl((int)a0, (uint64_t)a1, (void*)a2);
        case LINUX_SYS_stat:
            return linux_stat_path((const char*)a0, (linux_stat_t*)a1);
        case LINUX_SYS_fstat:
            return linux_fstat_fd((int)a0, (linux_stat_t*)a1);
        case LINUX_SYS_newfstatat:
            return linux_stat_path((const char*)a1, (linux_stat_t*)a2);
        case LINUX_SYS_access:
            return fs_exists((const char*)a0) ? 0 : linux_err(LINUX_EINVAL);
        case LINUX_SYS_getpid:
            return (uint64_t)((current_thread_id >= 0) ? current_thread_id : 1);
        case LINUX_SYS_gettid:
            return (uint64_t)((current_thread_id >= 0) ? current_thread_id : 1);
        case LINUX_SYS_getuid:
        case LINUX_SYS_getgid:
        case LINUX_SYS_geteuid:
        case LINUX_SYS_getegid:
            return 0;
        case LINUX_SYS_uname:
            return linux_uname((linux_utsname_t*)a0);
        case LINUX_SYS_clock_gettime:
            return linux_clock_gettime((int)a0, (linux_timespec_t*)a1);
        case LINUX_SYS_nanosleep:
            return linux_nanosleep((const linux_timespec_t*)a0, (linux_timespec_t*)a1);
        case LINUX_SYS_sched_yield:
            thread_yield();
            return 0;
        case LINUX_SYS_brk:
            return linux_brk((uintptr_t)a0);
        case LINUX_SYS_mmap: {
            size_t len = (size_t)a1;
            size_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
            void* addr = vmm_alloc_user_pages(pages);
            if (!addr) return linux_err(LINUX_ENOMEM);
            return (uint64_t)(uintptr_t)addr;
        }
        case LINUX_SYS_mprotect:
        case LINUX_SYS_munmap:
            return 0;
        case LINUX_SYS_getcwd:
        case LINUX_SYS_chdir:
        case LINUX_SYS_gettimeofday:
        case LINUX_SYS_fcntl:
        case LINUX_SYS_pipe:
        case LINUX_SYS_select:
        case LINUX_SYS_getdents:
        case LINUX_SYS_futex:
        case LINUX_SYS_arch_prctl:
        case LINUX_SYS_mkdirat:
        case LINUX_SYS_unlinkat:
        case LINUX_SYS_renameat:
            return linux_err(LINUX_ENOSYS);
        case LINUX_SYS_exit:
        case LINUX_SYS_exit_group:
            thread_exit_current();
            return 0;
        default:
            return linux_err(LINUX_ENOSYS);
    }
}
