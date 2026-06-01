#include <sys/mman.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#define MMAP_MAGIC 0x4D4D4150u

typedef struct mmap_hdr {
    size_t len;
    uint32_t magic;
} mmap_hdr_t;

static void *mmap_alloc(size_t length) {
    size_t total = length + sizeof(mmap_hdr_t);
    if (total < length) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    mmap_hdr_t *hdr = (mmap_hdr_t *)malloc(total);
    if (!hdr) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    hdr->len = length;
    hdr->magic = MMAP_MAGIC;
    return (void *)(hdr + 1);
}

static mmap_hdr_t *mmap_hdr_from_user(void *addr) {
    if (!addr) return 0;
    mmap_hdr_t *hdr = ((mmap_hdr_t *)addr) - 1;
    if (hdr->magic != MMAP_MAGIC) return 0;
    return hdr;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)prot;

    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (addr && (flags & MAP_FIXED)) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    void *mem = mmap_alloc(length);
    if (mem == MAP_FAILED) return MAP_FAILED;

    if (fd >= 0 && !(flags & MAP_ANONYMOUS)) {
        off_t cur = lseek(fd, 0, SEEK_CUR);
        if (cur < 0) cur = 0;
        if (lseek(fd, offset, SEEK_SET) < 0) {
            munmap(mem, length);
            errno = EINVAL;
            return MAP_FAILED;
        }
        size_t got = 0;
        while (got < length) {
            ssize_t r = read(fd, (char *)mem + got, length - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (got < length) {
            memset((char *)mem + got, 0, length - got);
        }
        (void)lseek(fd, cur, SEEK_SET);
    } else {
        memset(mem, 0, length);
    }

    return mem;
}

int munmap(void *addr, size_t length) {
    (void)length;
    mmap_hdr_t *hdr = mmap_hdr_from_user(addr);
    if (!hdr) {
        errno = EINVAL;
        return -1;
    }
    hdr->magic = 0;
    free(hdr);
    return 0;
}

int mprotect(void *addr, size_t length, int prot) {
    (void)addr;
    (void)length;
    (void)prot;
    return 0;
}
