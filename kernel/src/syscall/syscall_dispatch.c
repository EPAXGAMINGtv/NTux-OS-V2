#include <stdint.h>
#include "syscall_dispatch.h"
#include "syscall_numbers.h"
#include <drivers/framebuffer/kprint.h>
#include <limine.h>
#include <drivers/gpu/graphics.h>

extern volatile struct limine_framebuffer_request framebuffer_request;
#include <interrupt/timer.h>
#include <fs/fs.h>
#include <syscall/linux_syscall.h>
#include <lib/string.h>

static uint64_t sys_write(const char* buf, uint64_t len) {
    if (!buf) return (uint64_t)-1;
    char c[2] = {0, 0};
    for (uint64_t i = 0; i < len; i++) {
        c[0] = buf[i];
        kprint(c);
    }
    return len;
}

uint64_t syscall_dispatch(uint64_t* r) {
    uint64_t num = r[8];
    uint64_t rdi = r[7];
    uint64_t rsi = r[6];
    uint64_t rdx = r[5];
    uint64_t r10 = r[4];

    switch (num) {
        case SYSCALL_PRINT: {
            return sys_write((const char*)rdi, rsi);
        }
        case SYSCALL_PUTCHAR: {
            char c[2] = { (char)rdi, 0 };
            kprint(c);
            return 1;
        }
        case SYSCALL_GET_TICKS: {
            return get_tick_count();
        }
        case SYSCALL_WAIT_TICKS: {
            uint64_t start = get_tick_count();
            while (get_tick_count() - start < rdi) {
                __asm__ volatile("pause");
            }
            return 0;
        }
        case SYSCALL_CLEAR_SCREEN: {
            gpu_clear_screen((uint32_t)rdi);
            gpu_flush_all();
            return 0;
        }
        case SYSCALL_FS_EXISTS: {
            return fs_exists((const char*)rdi) ? 1u : 0u;
        }
        case SYSCALL_FS_MKDIR: {
            return (uint64_t)fs_mkdir((const char*)rdi, (const char*)rsi);
        }
        case SYSCALL_FS_CREATE_FILE: {
            return (uint64_t)fs_create_file((const char*)rdi, (const char*)rsi, (const void*)rdx, (size_t)r10);
        }
        case SYSCALL_FS_WRITE_FILE: {
            return (uint64_t)fs_write_file((const char*)rdi, (const void*)rsi, (size_t)rdx);
        }
        case SYSCALL_FS_READ_FILE: {
            size_t out_len = 0;
            int rc = fs_read_file((const char*)rdi, (void*)rsi, (size_t)rdx, &out_len);
            if (r10) *((uint64_t*)(uintptr_t)r10) = (uint64_t)out_len;
            return (uint64_t)rc;
        }
        case SYSCALL_FS_REMOVE: {
            return (uint64_t)fs_remove((const char*)rdi);
        }
        case SYSCALL_FS_RENAME: {
            return (uint64_t)fs_rename((const char*)rdi, (const char*)rsi);
        }
        case SYSCALL_FS_COPY_FAST: {
            return (uint64_t)fs_copy_file_fast((const char*)rdi, (const char*)rsi);
        }
    }

    return linux_syscall_dispatch(num, rdi, rsi, rdx, r10, r[3], r[2]);
}
