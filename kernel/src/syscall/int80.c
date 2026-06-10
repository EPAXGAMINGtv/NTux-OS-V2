#include <syscall/int80.h>
#include <syscall/deskapi.h>
#include <limine.h>

#include <sched/thread.h>
#include <drivers/gpu/graphics.h>
#include <drivers/framebuffer/kprint.h>
#include <drivers/input/input.h>
#include <drivers/input/console_input.h>
#include <drivers/ps2/keyboard.h>
#include <drivers/ps2/mouse.h>
#include <interrupt/apic/apic.h>
#include <drivers/cmos/cmos.h>
#include <elf/module_loader.h>
#include <fs/fd.h>
#include <fs/fs.h>
#include <network.h>
#include <net_defs.h>
#include <interrupt/timer.h>
#include <arch/x86_64/io.h>

#include <lib/info.h>
#include <mm/kmalloc.h>
#include <mm/pmm.h>
#include <lib/string.h>
#include <lib/kutils.h>

extern volatile struct limine_framebuffer_request framebuffer_request;

static int parse_ipv4(const char* s, ipv4_address_t* ip) {
    int octets[4] = { -1, -1, -1, -1 };
    int cur = 0, val = 0;
    for (int i = 0; s[i] && cur < 4; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
        } else if (s[i] == '.') {
            if (i == 0 || s[i-1] == '.') return -1;
            octets[cur++] = val; val = 0;
        } else {
            return -1;
        }
    }
    if (cur != 3) return -1;
    octets[3] = val;
    for (int i = 0; i < 4; i++)
        if (octets[i] < 0 || octets[i] > 255) return -1;
    ip->bytes[0] = (uint8_t)octets[0];
    ip->bytes[1] = (uint8_t)octets[1];
    ip->bytes[2] = (uint8_t)octets[2];
    ip->bytes[3] = (uint8_t)octets[3];
    return 0;
}

typedef struct {
    char name[64];
    uint8_t is_dir;
    uint8_t _pad[7];
    uint64_t size;
} int80_fs_dirent_t;

#define COM1_PORT 0x3F8

static uint8_t g_serial_init_done = 0;

static uint32_t scale_chan8_to_n(uint8_t v, uint8_t bits) {
    if (bits == 0) return 0;
    if (bits >= 8) {
        return ((uint32_t)v) << (bits - 8);
    }
    uint32_t maxv = (1u << bits) - 1u;
    return ((uint32_t)v * maxv + 127u) / 255u;
}

static uint32_t pack_rgb_for_fb(uint8_t r, uint8_t g, uint8_t b, volatile struct limine_framebuffer *fb) {
    uint32_t pr = scale_chan8_to_n(r, fb->red_mask_size) << fb->red_mask_shift;
    uint32_t pg = scale_chan8_to_n(g, fb->green_mask_size) << fb->green_mask_shift;
    uint32_t pb = scale_chan8_to_n(b, fb->blue_mask_size) << fb->blue_mask_shift;
    return pr | pg | pb;
}

static void serial_init_once(void) {
    if (g_serial_init_done) return;
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
    g_serial_init_done = 1;
}

static int serial_try_getchar(char* out) {
    if (!out) return 0;
    serial_init_once();
    if ((inb(COM1_PORT + 5) & 0x01) == 0) return 0;
    uint8_t c = inb(COM1_PORT + 0);
    if (c == '\r') c = '\n';
    *out = (char)c;
    return 1;
}

static int mul_overflow_size(size_t a, size_t b, size_t* out) {
    if (!out) return 0;
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    if (a > ((size_t)-1) / b) return 1;
    *out = a * b;
    return 0;
}

static int current_user_range(uintptr_t* out_start, uintptr_t* out_end) {
    uint64_t start = 0;
    uint64_t end = 0;
    if (thread_get_current_user_range(&start, &end) != 0 || start == 0 || end <= start) {
        if (out_start) *out_start = 0;
        if (out_end) *out_end = UINTPTR_MAX;
        return 0;
    }
    if (out_start) *out_start = (uintptr_t)start;
    if (out_end) *out_end = (uintptr_t)end;
    return 1;
}

static int user_ptr_range_ok(const void* ptr, size_t len) {
    if (len == 0) return 1;
    if (!ptr) return 0;
    uintptr_t start = 0;
    uintptr_t end = 0;
    int restricted = current_user_range(&start, &end);
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t last = p + (uintptr_t)(len - 1u);
    if (last < p) return 0;
    if (!restricted) return 1;
    return p >= start && last < end;
}

static int user_cstr_ok(const char* s, size_t max_scan) {
    if (!s || max_scan == 0) return 0;
    uintptr_t start = 0;
    uintptr_t end = 0;
    int restricted = current_user_range(&start, &end);
    uintptr_t p = (uintptr_t)s;
    if (restricted && (p < start || p >= end)) return 0;

    size_t limit = max_scan;
    if (restricted) {
        size_t room = (size_t)(end - p);
        if (room < limit) limit = room;
    }
    if (limit == 0) return 0;

    for (size_t i = 0; i < limit; ++i) {
        if (s[i] == '\0') return 1;
    }
    return 0;
}

static int int80_current_tid(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS) return -1;
    if (!thread_list[tid]) return -1;
    return tid;
}

static int console_input_owner_is_current(void) {
    int tid = int80_current_tid();
    if (tid < 0) return 0;
    return console_input_owner_is_tid(tid);
}

static void console_input_release_if_current(void) {
    int tid = int80_current_tid();
    if (tid < 0) return;
    console_input_release_if_tid(tid);
}

static int console_input_claim_or_is_current_for_current(void) {
    int tid = int80_current_tid();
    if (tid < 0) return 0;
    return console_input_claim_or_is_current(tid);
}

static int str_has_ci(const char* s, const char* sub) {
    if (!s || !sub || !*sub) return 0;
    for (; *s; s++) {
        const char* a = s;
        const char* b = sub;
        while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

uint64_t syscall_int80_dispatch(int80_regs_t *regs) {
    if (!regs) return 0;

    switch (regs->rax) {
        case INT80_WRITE: {
            const char *buf = (const char *)regs->rdi;
            uint64_t len = regs->rsi;
            if (!buf || !user_ptr_range_ok(buf, (size_t)len)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (kprint_get_user_stdout_serial_only()) {
                for (uint64_t i = 0; i < len; ++i) {
                    kprint_serial_char(buf[i]);
                }
            } else {
                for (uint64_t i = 0; i < len; ++i) {
                    char c[2] = {buf[i], 0};
                    kprint(c);
                }
            }
            regs->rax = len;
            return 0;
        }
        case INT80_EXIT:
            regs->rax = 0;
            return 1;
        case INT80_PUTCHAR: {
            char c[2] = {(char)regs->rdi, 0};
            if (kprint_get_user_stdout_serial_only()) {
                kprint_serial_char(c[0]);
            } else {
                kprint(c);
            }
            regs->rax = 1;
            return 0;
        }
        case INT80_GET_TICKS:
            regs->rax = get_tick_count();
            return 0;
        case INT80_WAIT_TICKS: {
            uint64_t wait_for = regs->rdi;
            if (wait_for == 0) {
                regs->rax = 0;
                return 0;
            }
            uint64_t wake = get_tick_count() + wait_for;
            thread_lock_global();
            int tid = current_thread_id;
            if (tid >= 0 && tid < MAX_THREADS && thread_list[tid]) {
                thread_list[tid]->wake_tick = wake;
                if (thread_list[tid]->state != THREAD_BLOCKED) {
                    thread_list[tid]->state = THREAD_BLOCKED;
                    rq_remove(thread_list[tid]);
                    g_thread_blocked_count++;
                }
            }
            thread_unlock_global();
            scheduler();
            /* After scheduler: either a context switch happened (we were
             * BLOCKED → READY → RUNNING) or no switch (still BLOCKED).
             * In either case, wait until wake time and fix up state. */
            thread_lock_global();
            if (tid >= 0 && tid < MAX_THREADS && thread_list[tid]) {
                thread_t* t = thread_list[tid];
                while (t->state == THREAD_BLOCKED && get_tick_count() < wake) {
                    thread_unlock_global();
                    __asm__ volatile("hlt");
                    thread_lock_global();
                    if (tid < 0 || tid >= MAX_THREADS) break;
                    t = thread_list[tid];
                    if (!t) break;
                }
                if (t) {
                    t->state = THREAD_RUNNING;
                    t->wake_tick = 0;
                    rq_remove(t);
                    current_thread_id = tid;
                }
            }
            thread_unlock_global();
            regs->rax = 0;
            return 0;
        }
        case INT80_CLEAR_SCREEN: {
            gpu_clear_screen((uint32_t)regs->rdi);
            gpu_flush_all();
            if (g_printer.cursor) {
                g_printer.cursor->x = 0;
                g_printer.cursor->y = 0;
            }
            regs->rax = 0;
            return 0;
        }
        case INT80_GETCHAR: {
            if (!console_input_claim_or_is_current_for_current()) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char c = 0;
            if (input_try_getchar(&c) != 0) {
                regs->rax = (uint64_t)(uint8_t)c;
            } else {
                if (serial_try_getchar(&c)) {
                    regs->rax = (uint64_t)(uint8_t)c;
                    return 0;
                }
                regs->rax = (uint64_t)-1;
                return 0;
            }
            return 0;
        }
        case INT80_REBOOT:
            system_reboot();
            regs->rax = 0;
            return 0;
        case INT80_SHUTDOWN:
            system_shutdown();
            regs->rax = 0;
            return 0;
        case INT80_YIELD:
            thread_yield();
            regs->rax = 0;
            return 0;
        case INT80_CONSOLE_RELEASE:
            console_input_release_if_current();
            regs->rax = 0;
            return 0;
        case INT80_CONSOLE_IS_FREE: {
            regs->rax = console_input_is_free() ? 1u : 0u;
            return 0;
        }
        case INT80_CONSOLE_CLAIM: {
            regs->rax = console_input_claim_or_is_current_for_current() ? 0u : (uint64_t)-1;
            return 0;
        }
        case INT80_CONSOLE_FORCE_CLAIM: {
            console_input_force_owner(int80_current_tid());
            regs->rax = 0u;
            return 0;
        }
        case INT80_TASK_ADD: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            const char* status = 0;
            if (!user_cstr_ok(path, 512u) || !path[0]) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (console_input_owner_is_current()) {
                console_input_release_if_current();
            }
            if (!module_loader_start_elf_ring3(path, &status)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            int tid = module_loader_last_elf_tid();
            regs->rax = (tid >= 0) ? (uint64_t)tid : 0u;
            return 0;
        }
        case INT80_TASK_ADD_MODULE: {
            const char* token = (const char*)(uintptr_t)regs->rdi;
            const char* status = 0;
            if (!user_cstr_ok(token, 128u) || !token[0]) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (console_input_owner_is_current()) {
                console_input_release_if_current();
            }
            if (!module_loader_start_module_ring3(token, &status)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            int tid = module_loader_last_hello_tid();
            regs->rax = (tid >= 0) ? (uint64_t)tid : 0u;
            return 0;
        }
        case INT80_TASK_LIST: {
            int80_task_info_t* out = (int80_task_info_t*)(uintptr_t)regs->rdi;
            size_t max_entries = (size_t)regs->rsi;
            uint64_t* out_count_ptr = (uint64_t*)(uintptr_t)regs->rdx;
            size_t total = 0;
            size_t out_bytes = 0;

            if (max_entries > 0 && (!out || mul_overflow_size(max_entries, sizeof(*out), &out_bytes) || !user_ptr_range_ok(out, out_bytes))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (out_count_ptr && !user_ptr_range_ok(out_count_ptr, sizeof(*out_count_ptr))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }

            thread_lock_global();
            for (size_t i = 0; i < MAX_THREADS; ++i) {
                thread_t* t = thread_list[i];
                if (!t) continue;
                if (out && total < max_entries) {
                    out[total].id = t->id;
                    memset(out[total].name, 0, sizeof(out[total].name));
                    if (t->name[0]) {
                        strncpy(out[total].name, t->name, sizeof(out[total].name) - 1);
                        out[total].name[sizeof(out[total].name) - 1] = '\0';
                    }
                    out[total].state = (uint32_t)t->state;
                    out[total].running_core = 0;
                    out[total].affinity_core = 0;
                    out[total].uid = t->uid;
                    out[total].active = (t->state != THREAD_TERMINATED) ? 1u : 0u;
                    out[total].cpu_ticks = t->cpu_ticks;
                    out[total].mem_bytes = t->user_mem_bytes;
                }
                total++;
            }
            thread_unlock_global();

            if (out_count_ptr) *out_count_ptr = (uint64_t)total;
            regs->rax = 0;
            return 0;
        }
        case INT80_TASK_KILL: {
            int tid = (int)regs->rdi;
            regs->rax = (uint64_t)thread_kill(tid);
            return 0;
        }
        case INT80_MODULE_LIST: {
            ntux_module_info_t* out = (ntux_module_info_t*)(uintptr_t)regs->rdi;
            size_t max_entries = (size_t)regs->rsi;
            uint64_t* out_count_ptr = (uint64_t*)(uintptr_t)regs->rdx;
            size_t out_bytes = 0;

            if (max_entries > 0 && (!out || mul_overflow_size(max_entries, sizeof(*out), &out_bytes) || !user_ptr_range_ok(out, out_bytes))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (out_count_ptr && !user_ptr_range_ok(out_count_ptr, sizeof(*out_count_ptr))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }

            regs->rax = (uint64_t)module_loader_list(out, max_entries, out_count_ptr);
            return 0;
        }
        case INT80_SET_UID: {
            regs->rax = (uint64_t)thread_set_current_uid((uint32_t)regs->rdi);
            return 0;
        }
        case INT80_GET_UID: {
            regs->rax = (uint64_t)thread_get_current_uid();
            return 0;
        }
        case INT80_FS_EXISTS: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            if (!user_cstr_ok(path, 1024u)) {
                regs->rax = 0;
                return 0;
            }
            regs->rax = fs_exists(path) ? 1u : 0u;
            return 0;
        }
        case INT80_FS_MKDIR: {
            const char* parent = (const char*)(uintptr_t)regs->rdi;
            const char* name = (const char*)(uintptr_t)regs->rsi;
            if (!user_cstr_ok(parent, 1024u) || !user_cstr_ok(name, 256u)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_mkdir(parent, name);
            return 0;
        }
        case INT80_FS_CREATE_FILE: {
            const char* parent = (const char*)(uintptr_t)regs->rdi;
            const char* name = (const char*)(uintptr_t)regs->rsi;
            const void* data = (const void*)(uintptr_t)regs->rdx;
            size_t len = (size_t)regs->rcx;
            if (!user_cstr_ok(parent, 1024u) || !user_cstr_ok(name, 256u) || (len > 0 && !user_ptr_range_ok(data, len))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_create_file(parent, name, data, len);
            return 0;
        }
        case INT80_FS_WRITE_FILE: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            const void* data = (const void*)(uintptr_t)regs->rsi;
            size_t len = (size_t)regs->rdx;
            if (!user_cstr_ok(path, 1024u) || (len > 0 && !user_ptr_range_ok(data, len))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_write_file(path, data, len);
            return 0;
        }
        case INT80_FS_READ_FILE: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            void* out = (void*)(uintptr_t)regs->rsi;
            size_t out_cap = (size_t)regs->rdx;
            uint64_t* out_len_ptr = (uint64_t*)(uintptr_t)regs->rcx;
            size_t out_len = 0;
            if (!user_cstr_ok(path, 1024u) ||
                (out_cap > 0 && !user_ptr_range_ok(out, out_cap)) ||
                (out_len_ptr && !user_ptr_range_ok(out_len_ptr, sizeof(*out_len_ptr)))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            int rc = fs_read_file(path, out, out_cap, &out_len);
            if (out_len_ptr) *out_len_ptr = (uint64_t)out_len;
            regs->rax = (uint64_t)rc;
            return 0;
        }
        case INT80_FS_LIST_DIR: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            int80_fs_dirent_t* out = (int80_fs_dirent_t*)(uintptr_t)regs->rsi;
            size_t max_entries = (size_t)regs->rdx;
            uint64_t* out_count_ptr = (uint64_t*)(uintptr_t)regs->rcx;
            size_t out_count = 0;
            size_t out_bytes = 0;
            if (!user_cstr_ok(path, 1024u) ||
                (out && (mul_overflow_size(max_entries, sizeof(*out), &out_bytes) || !user_ptr_range_ok(out, out_bytes))) ||
                (out_count_ptr && !user_ptr_range_ok(out_count_ptr, sizeof(*out_count_ptr)))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            int rc = 0;
            if (!out || max_entries == 0) {
                rc = fs_list_dir(path, NULL, 0, &out_count);
            } else {
                size_t cap = max_entries;
                if (cap > 64u) cap = 64u;
                fs_dirent_t* tmp = (fs_dirent_t*)kmalloc(sizeof(fs_dirent_t) * cap);
                if (!tmp) {
                    regs->rax = (uint64_t)-1;
                    return 0;
                }
                rc = fs_list_dir(path, tmp, cap, &out_count);
                size_t copy_n = out_count;
                if (copy_n > cap) copy_n = cap;
                if (rc == 0) {
                    for (size_t i = 0; i < copy_n; ++i) {
                        memcpy(out[i].name, tmp[i].name, sizeof(out[i].name));
                        out[i].is_dir = tmp[i].is_dir ? 1u : 0u;
                        out[i]._pad[0] = 0;
                        out[i]._pad[1] = 0;
                        out[i]._pad[2] = 0;
                        out[i]._pad[3] = 0;
                        out[i]._pad[4] = 0;
                        out[i]._pad[5] = 0;
                        out[i]._pad[6] = 0;
                        out[i].size = (uint64_t)tmp[i].size;
                    }
                }
                kfree(tmp);
            }
            if (out_count_ptr) *out_count_ptr = (uint64_t)out_count;
            regs->rax = (uint64_t)rc;
            return 0;
        }
        case INT80_FS_REMOVE: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            if (!user_cstr_ok(path, 1024u)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_remove(path);
            return 0;
        }
        case INT80_FS_RENAME: {
            const char* old_path = (const char*)(uintptr_t)regs->rdi;
            const char* new_path = (const char*)(uintptr_t)regs->rsi;
            if (!user_cstr_ok(old_path, 1024u) || !user_cstr_ok(new_path, 1024u)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_rename(old_path, new_path);
            return 0;
        }
        case INT80_FS_COPY_FAST: {
            const char* src = (const char*)(uintptr_t)regs->rdi;
            const char* dst = (const char*)(uintptr_t)regs->rsi;
            if (!user_cstr_ok(src, 1024u) || !user_cstr_ok(dst, 1024u)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_copy_file_fast(src, dst);
            return 0;
        }
        case INT80_FS_RESCAN: {
            fs_rescan_storage();
            regs->rax = 0;
            return 0;
        }
        case INT80_MOUSE_GET_STATE: {
            int80_mouse_state_t* out = (int80_mouse_state_t*)(uintptr_t)regs->rdi;
            if (!out || !user_ptr_range_ok(out, sizeof(*out))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            input_mouse_state_t m;
            int bound_w = 0;
            int bound_h = 0;
            if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
                volatile struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
                if (fb) {
                    bound_w = (int)fb->width;
                    bound_h = (int)fb->height;
                }
            }
            input_mouse_get_state(&m, bound_w, bound_h);
            out->x = m.x;
            out->y = m.y;
            out->scroll = m.scroll;
            out->left = m.left;
            out->right = m.right;
            out->middle = m.middle;
            out->_pad = 0;
            regs->rax = 0;
            return 0;
        }
        case INT80_KBD_IS_PRESSED: {
            uint8_t key = (uint8_t)(regs->rdi & 0x7Fu);
            regs->rax = input_key_pressed(key) ? 1u : 0u;
            return 0;
        }
        case INT80_KBD_GET_STATE: {
            uint8_t* out = (uint8_t*)(uintptr_t)regs->rdi;
            uint64_t len = regs->rsi;
            if (!out || len == 0 || len > 128u || !user_ptr_range_ok(out, (size_t)len)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            input_copy_key_state(out, (size_t)len);
            regs->rax = 0;
            return 0;
        }
        case INT80_KBD_CONSUME_SUPER_PRESS: {
            regs->rax = input_consume_super_press() ? 1u : 0u;
            return 0;
        }
        case INT80_FB_GET_INFO: {
            int80_fb_info_t* out = (int80_fb_info_t*)(uintptr_t)regs->rdi;
            if (!out || !user_ptr_range_ok(out, sizeof(*out))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
                volatile struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
                out->width = (uint32_t)fb->width;
                out->height = (uint32_t)fb->height;
                out->pitch = (uint32_t)fb->pitch;
                out->bpp = (uint32_t)fb->bpp;
                out->memory_model = fb->memory_model;
                out->red_mask_size = fb->red_mask_size;
                out->red_mask_shift = fb->red_mask_shift;
                out->green_mask_size = fb->green_mask_size;
                out->green_mask_shift = fb->green_mask_shift;
                out->blue_mask_size = fb->blue_mask_size;
                out->blue_mask_shift = fb->blue_mask_shift;
            } else {
                out->width = 0;
                out->height = 0;
                out->pitch = 0;
                out->bpp = 0;
                out->memory_model = 0;
                out->red_mask_size = 0;
                out->red_mask_shift = 0;
                out->green_mask_size = 0;
                out->green_mask_shift = 0;
                out->blue_mask_size = 0;
                out->blue_mask_shift = 0;
            }
            out->_pad = 0;
            regs->rax = 0;
            return 0;
        }
        case INT80_FB_BLIT32: {
            const uint32_t* src = (const uint32_t*)(uintptr_t)regs->rdi;
            uint32_t src_w = (uint32_t)regs->rsi;
            uint32_t src_h = (uint32_t)regs->rdx;
            uint32_t src_pitch = (uint32_t)regs->rcx;
            if (!src || src_w == 0 || src_h == 0 || src_pitch == 0) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (src_w > (UINT32_MAX / sizeof(uint32_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (src_pitch < src_w * sizeof(uint32_t)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if ((uint64_t)src_pitch > 0 && src_h > 0) {
                size_t src_bytes = 0;
                if (mul_overflow_size((size_t)src_pitch, (size_t)src_h, &src_bytes) || !user_ptr_range_ok(src, src_bytes)) {
                    regs->rax = (uint64_t)-1;
                    return 0;
                }
            }
            if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            volatile struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
            if (fb->bpp != 32 && fb->bpp != 24) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (fb->red_mask_size == 0 || fb->green_mask_size == 0 || fb->blue_mask_size == 0) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if ((uint32_t)fb->red_mask_shift + (uint32_t)fb->red_mask_size > (uint32_t)fb->bpp ||
                (uint32_t)fb->green_mask_shift + (uint32_t)fb->green_mask_size > (uint32_t)fb->bpp ||
                (uint32_t)fb->blue_mask_shift + (uint32_t)fb->blue_mask_size > (uint32_t)fb->bpp) {
                regs->rax = (uint64_t)-1;
                return 0;
            }

            uint32_t copy_w = src_w;
            uint32_t copy_h = src_h;
            if (copy_w > (uint32_t)fb->width) copy_w = (uint32_t)fb->width;
            if (copy_h > (uint32_t)fb->height) copy_h = (uint32_t)fb->height;

            uint8_t* dst = (uint8_t*)(uintptr_t)fb->address;
            for (uint32_t y = 0; y < copy_h; ++y) {
                const uint8_t* src_row = (const uint8_t*)(const void*)src + (size_t)y * (size_t)src_pitch;
                uint8_t* dst_row = dst + (size_t)y * (size_t)fb->pitch;
                const uint32_t* s = (const uint32_t*)(const void*)src_row;
                for (uint32_t x = 0; x < copy_w; ++x) {
                    uint32_t p = s[x];
                    uint8_t b = (uint8_t)(p & 0xFFu);
                    uint8_t g = (uint8_t)((p >> 8) & 0xFFu);
                    uint8_t r = (uint8_t)((p >> 16) & 0xFFu);
                    uint32_t out = pack_rgb_for_fb(r, g, b, fb);
                    if (fb->bpp == 32) {
                        dst_row[x * 4u + 0u] = (uint8_t)(out & 0xFFu);
                        dst_row[x * 4u + 1u] = (uint8_t)((out >> 8) & 0xFFu);
                        dst_row[x * 4u + 2u] = (uint8_t)((out >> 16) & 0xFFu);
                        dst_row[x * 4u + 3u] = (uint8_t)((out >> 24) & 0xFFu);
                    } else {
                        dst_row[x * 3u + 0u] = (uint8_t)(out & 0xFFu);
                        dst_row[x * 3u + 1u] = (uint8_t)((out >> 8) & 0xFFu);
                        dst_row[x * 3u + 2u] = (uint8_t)((out >> 16) & 0xFFu);
                    }
                }
            }
            regs->rax = 0;
            return 0;
        }
        case INT80_SET_TEXT_COLOR: {
            g_printer.color = (uint32_t)regs->rdi;
            regs->rax = 0;
            return 0;
        }
        case INT80_GET_TIME: {
            int80_time_t* out = (int80_time_t*)(uintptr_t)regs->rdi;
            if (!out || !user_ptr_range_ok(out, sizeof(*out))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            cmos_time_t t;
            if (!cmos_read_time(&t)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            out->second = t.second;
            out->minute = t.minute;
            out->hour = t.hour;
            out->day = t.day;
            out->month = t.month;
            out->year = t.year;
            regs->rax = 0;
            return 0;
        }
        case INT80_GET_TIMER_HZ: {
            regs->rax = (uint64_t)timer_get_hz();
            return 0;
        }
        case INT80_BLK_LIST: {
            int80_block_device_info_t* out = (int80_block_device_info_t*)(uintptr_t)regs->rdi;
            size_t max_entries = (size_t)regs->rsi;
            uint64_t* out_count_ptr = (uint64_t*)(uintptr_t)regs->rdx;
            size_t out_bytes = 0;
            if (max_entries > 16u) max_entries = 16u;
            if (out && (mul_overflow_size(max_entries, sizeof(*out), &out_bytes) || !user_ptr_range_ok(out, out_bytes))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (out_count_ptr && !user_ptr_range_ok(out_count_ptr, sizeof(*out_count_ptr))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            size_t count = fs_get_block_devices((fs_block_device_info_t*)out, out ? max_entries : 0u);
            if (out_count_ptr) *out_count_ptr = (uint64_t)count;
            regs->rax = 0;
            return 0;
        }
        case INT80_BLK_PART_LIST: {
            uint8_t drive = (uint8_t)regs->rdi;
            int80_partition_info_t* out = (int80_partition_info_t*)(uintptr_t)regs->rsi;
            size_t max_entries = (size_t)regs->rdx;
            uint64_t* out_count_ptr = (uint64_t*)(uintptr_t)regs->rcx;
            size_t out_bytes = 0;
            if (max_entries > 16u) max_entries = 16u;
            if (out && (mul_overflow_size(max_entries, sizeof(*out), &out_bytes) || !user_ptr_range_ok(out, out_bytes))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (out_count_ptr && !user_ptr_range_ok(out_count_ptr, sizeof(*out_count_ptr))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            size_t count = fs_list_partitions(drive, (fs_partition_info_t*)out, out ? max_entries : 0u);
            if (out_count_ptr) *out_count_ptr = (uint64_t)count;
            regs->rax = 0;
            return 0;
        }
        case INT80_BLK_READ: {
            uint8_t drive = (uint8_t)regs->rdi;
            uint64_t lba = regs->rsi;
            uint32_t sectors = (uint32_t)regs->rdx;
            void* out = (void*)(uintptr_t)regs->rcx;
            size_t bytes = 0;
            if (mul_overflow_size((size_t)sectors, 512u, &bytes) || !user_ptr_range_ok(out, bytes)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_block_read(drive, lba, sectors, out);
            return 0;
        }
        case INT80_BLK_WRITE: {
            uint8_t drive = (uint8_t)regs->rdi;
            uint64_t lba = regs->rsi;
            uint32_t sectors = (uint32_t)regs->rdx;
            const void* in = (const void*)(uintptr_t)regs->rcx;
            size_t bytes = 0;
            if (mul_overflow_size((size_t)sectors, 512u, &bytes) || !user_ptr_range_ok(in, bytes)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_block_write(drive, lba, sectors, in);
            return 0;
        }
        case INT80_BLK_SET_MBR_PART: {
            const int80_mbr_part_req_t* req = (const int80_mbr_part_req_t*)(uintptr_t)regs->rdi;
            if (!req || !user_ptr_range_ok(req, sizeof(*req))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fs_set_mbr_partition(
                req->drive,
                req->part_index,
                req->lba_start,
                req->sectors,
                req->type,
                req->bootable
            );
            return 0;
        }
        case INT80_MKFS_EXT2: {
            regs->rax = (uint64_t)fs_mkfs_ext2((uint8_t)regs->rdi, (uint32_t)regs->rsi, (uint32_t)regs->rdx);
            return 0;
        }
        case INT80_MKFS_EXT4: {
            regs->rax = (uint64_t)fs_mkfs_ext4((uint8_t)regs->rdi, (uint32_t)regs->rsi, (uint32_t)regs->rdx);
            return 0;
        }
        case INT80_MKFS_FAT: {
            regs->rax = (uint64_t)fs_mkfs_fat((uint8_t)regs->rdi, (uint32_t)regs->rsi, (uint32_t)regs->rdx, (uint8_t)regs->rcx);
            return 0;
        }
        case INT80_OPEN: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            int flags = (int)regs->rsi;
            if (!user_cstr_ok(path, 1024u)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fd_open(path, flags);
            return 0;
        }
        case INT80_READ: {
            int fd = (int)regs->rdi;
            void* out = (void*)(uintptr_t)regs->rsi;
            size_t len = (size_t)regs->rdx;
            if (len > 0 && !user_ptr_range_ok(out, len)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fd_read(fd, out, len);
            return 0;
        }
        case INT80_WRITE_FD: {
            int fd = (int)regs->rdi;
            const void* in = (const void*)(uintptr_t)regs->rsi;
            size_t len = (size_t)regs->rdx;
            if (len > 0 && !user_ptr_range_ok(in, len)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fd_write(fd, in, len);
            return 0;
        }
        case INT80_CLOSE: {
            int fd = (int)regs->rdi;
            regs->rax = (uint64_t)fd_close(fd);
            return 0;
        }
        case INT80_IOCTL: {
            int fd = (int)regs->rdi;
            uint64_t req = regs->rsi;
            void* arg = (void*)(uintptr_t)regs->rdx;
            if (arg && !user_ptr_range_ok(arg, 256u)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)fd_ioctl(fd, req, arg);
            return 0;
        }
        case INT80_LSEEK: {
            int fd = (int)regs->rdi;
            long offset = (long)regs->rsi;
            int whence = (int)regs->rdx;
            regs->rax = (uint64_t)fd_lseek(fd, offset, whence);
            return 0;
        }
        case INT80_NET_PING: {
            const char* host = (const char*)(uintptr_t)regs->rdi;
            char* out = (char*)(uintptr_t)regs->rsi;
            uint64_t cap = regs->rdx;
            if (!user_cstr_ok(host, 256u) || !out || cap == 0 || cap > 2048u || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            ipv4_address_t ip;
            if (parse_ipv4(host, &ip) != 0) {
                if (network_dns_lookup(host, &ip) != 0) {
                    regs->rax = (uint64_t)-1;
                    return 0;
                }
            }
            int rtt = network_icmp_single_ping(&ip);
            if (rtt < 0) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char buf[128];
            int pos = 0;
            const char* m = "Reply from ";
            while (*m && pos < 120) buf[pos++] = *m++;
            for (int i = 0; i < 4; i++) {
                if (i > 0 && pos < 120) buf[pos++] = '.';
                char num[16];
                itoa(ip.bytes[i], num, 10);
                for (int k = 0; num[k] && pos < 120; k++) buf[pos++] = num[k];
            }
            m = " time=";
            while (*m && pos < 120) buf[pos++] = *m++;
            char num[16];
            itoa(rtt, num, 10);
            for (int k = 0; num[k] && pos < 120; k++) buf[pos++] = num[k];
            m = "ms";
            while (*m && pos < 120) buf[pos++] = *m++;
            buf[pos] = '\0';
            size_t copy_len = (pos < (int)cap - 1) ? pos : (size_t)(cap - 1);
            for (size_t i = 0; i < copy_len; i++) out[i] = buf[i];
            out[copy_len] = '\0';
            regs->rax = (uint64_t)copy_len;
            return 0;
        }
        case INT80_NET_HTTP_GET: {
            const char* url = (const char*)(uintptr_t)regs->rdi;
            char* out = (char*)(uintptr_t)regs->rsi;
            uint64_t cap = regs->rdx;
            if (!user_cstr_ok(url, 512u) || !out || cap == 0 || cap > 65536u || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }

            char* buf = (char*)kmalloc(65536);
            if (!buf) { regs->rax = -1; return 0; }

            char curr_url[512];
            strncpy(curr_url, url, sizeof(curr_url) - 1);
            curr_url[sizeof(curr_url) - 1] = '\0';

            int redirects = 0;
            int body_len = -1;

            while (redirects < 5) {
                const char* p = curr_url;
                int is_https = 0;
                if (strncmp(p, "https://", 8) == 0) { is_https = 1; p += 8; }
                else if (strncmp(p, "http://", 7) == 0) p += 7;
                else { body_len = -1; break; }

                if (is_https) {
                    const char* msg = "HTTPS not supported.\n";
                    size_t mlen = strlen(msg);
                    if (mlen > cap) mlen = cap;
                    memcpy(out, msg, mlen);
                    regs->rax = mlen;
                    kfree(buf);
                    return 0;
                }

                char host[128];
                char path[256];
                int slash = -1;
                for (int i = 0; p[i]; i++) {
                    if (p[i] == '/' && slash < 0) slash = i;
                }
                if (slash < 0) {
                    strncpy(host, p, sizeof(host) - 1);
                    host[sizeof(host) - 1] = '\0';
                    path[0] = '/'; path[1] = '\0';
                } else {
                    int hlen = slash;
                    if (hlen > 127) hlen = 127;
                    memcpy(host, p, (size_t)hlen);
                    host[hlen] = '\0';
                    int plen = 0;
                    for (int i = slash; p[i] && plen < 255; i++) path[plen++] = p[i];
                    path[plen] = '\0';
                }

                ipv4_address_t ip;
                if (parse_ipv4(host, &ip) != 0) {
                    if (network_dns_lookup(host, &ip) != 0) { body_len = -1; break; }
                }

                if (network_tcp_connect(&ip, 80) != 0) { body_len = -1; break; }

                char req[512];
                int rp = 0;
                const char* g = "GET ";
                while (*g && rp < 500) req[rp++] = *g++;
                char* pp = path;
                while (*pp && rp < 500) req[rp++] = *pp++;
                const char* v = " HTTP/1.1\r\nHost: ";
                while (*v && rp < 500) req[rp++] = *v++;
                char* hp = host;
                while (*hp && rp < 500) req[rp++] = *hp++;
                const char* h2 = "\r\nUser-Agent: NTux-Browser/1.0\r\nAccept: text/html,*/*;q=0.8\r\nAccept-Language: en\r\nConnection: close\r\n\r\n";
                while (*h2 && rp < 500) req[rp++] = *h2++;
                network_tcp_send(req, rp);

                int total = 0;
                int n;
                while ((n = network_tcp_recv(buf + total, 65536 - total)) > 0) {
                    total += n;
                    if (total >= 65536) break;
                }
                network_tcp_close();

                if (total <= 0) { body_len = -1; break; }
                buf[total] = '\0';

                // Find end of headers
                char* hdr_end = NULL;
                for (int i = 0; i < total - 3; i++) {
                    if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                        hdr_end = buf + i;
                        break;
                    }
                }
                if (!hdr_end) { body_len = -1; break; }

                // Parse status
                int status = 0;
                char* sp = buf;
                while (*sp && *sp != ' ') sp++;
                if (*sp == ' ') { sp++; while (*sp >= '0' && *sp <= '9') { status = status * 10 + (*sp - '0'); sp++; } }

                // Parse headers
                char* body = hdr_end + 4;
                int body_remain = total - (int)(body - buf);
                int content_len = -1;
                int is_chunked = 0;
                char* location = NULL;

                char* line = buf;
                while (line && line < hdr_end) {
                    char* nl = NULL;
                    for (int i = 0; line + i < hdr_end; i++) {
                        if (line[i] == '\r' && line + i + 1 < hdr_end && line[i+1] == '\n') {
                            nl = line + i;
                            break;
                        }
                    }
                    if (!nl) break;
                    *nl = '\0';

                    // Check header name
                    char* val = line;
                    while (*val && *val != ':') val++;
                    if (*val == ':') {
                        *val = '\0';
                        val++;
                        while (*val == ' ') val++;

                        if (strcmp(line, "Content-Length") == 0) content_len = atoi(val);
                        else if (strcmp(line, "Location") == 0) location = val;
                        else {
                            char* low = line;
                            for (int i = 0; low[i]; i++) if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] + 32);
                            if (strcmp(low, "transfer-encoding") == 0 && str_has_ci(val, "chunked")) is_chunked = 1;
                        }
                        *val = ':';
                    }

                    line = nl + 2;
                }

                // Handle redirect
                if (status >= 300 && status < 400 && location) {
                    int loc_len = 0;
                    while (location[loc_len] && location[loc_len] != '\r' && location[loc_len] != '\n') loc_len++;
                    location[loc_len] = '\0';
                    if (location[0] == '/') {
                        char tmp[512];
                        int ti = 0;
                        const char* tsrc = "http://";
                        while (*tsrc && ti < 510) tmp[ti++] = *tsrc++;
                        int hi = 0;
                        while (host[hi] && ti < 510) tmp[ti++] = host[hi++];
                        int li = 0;
                        while (location[li] && ti < 510) tmp[ti++] = location[li++];
                        tmp[ti] = '\0';
                        strncpy(curr_url, tmp, sizeof(curr_url) - 1);
                    } else {
                        strncpy(curr_url, location, sizeof(curr_url) - 1);
                    }
                    curr_url[sizeof(curr_url) - 1] = '\0';
                    redirects++;
                    continue;
                }

                if (status >= 200 && status < 300) {
                    if (is_chunked) {
                        char* src = body;
                        char* dst = body;
                        int decoded = 0;
                        while (src < buf + total) {
                            char* size_end = NULL;
                            for (int i = 0; src + i < buf + total; i++) {
                                if (src[i] == '\r' && src + i + 1 <= buf + total && src[i+1] == '\n') {
                                    size_end = src + i;
                                    break;
                                }
                            }
                            if (!size_end) break;
                            *size_end = '\0';
                            int chunk_sz = 0;
                            for (char* h = src; *h; h++) {
                                char c = *h;
                                chunk_sz <<= 4;
                                if (c >= '0' && c <= '9') chunk_sz |= (c - '0');
                                else if (c >= 'a' && c <= 'f') chunk_sz |= (c - 'a' + 10);
                                else if (c >= 'A' && c <= 'F') chunk_sz |= (c - 'A' + 10);
                            }
                            src = size_end + 2;
                            if (chunk_sz == 0) break;
                            memmove(dst, src, (size_t)chunk_sz);
                            dst += chunk_sz;
                            decoded += chunk_sz;
                            src += chunk_sz + 2;
                        }
                        body_remain = decoded;
                    } else if (content_len >= 0 && body_remain > content_len) {
                        body_remain = content_len;
                    }

                    int copy = body_remain;
                    if (copy < 0) copy = 0;
                    if (copy > (int)cap - 1) copy = (int)cap - 1;
                    if (copy > 0) memcpy(out, body, (size_t)copy);
                    out[copy] = '\0';
                    body_len = copy;
                    break;
                }

                body_len = -1;
                break;
            }

            if (body_len < 0) {
                const char* err = "HTTP request failed.\n";
                size_t elen = strlen(err);
                if (elen > cap) elen = cap;
                memcpy(out, err, elen);
                regs->rax = elen;
            } else {
                regs->rax = (uint64_t)body_len;
            }

            kfree(buf);
            return 0;
        }
        case INT80_NET_DEBUG: {
            char* out = (char*)(uintptr_t)regs->rdi;
            uint64_t cap = regs->rsi;
            if (!out || cap == 0 || cap > 2048u || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char buf[256];
            int pos = 0;
            char num[16];
            const char* s = "NET rx:";
            while (*s && pos < 240) buf[pos++] = *s++;
            itoa(network_get_frames_received(), num, 10);
            for (int k = 0; num[k] && pos < 240; k++) buf[pos++] = num[k];
            s = " tx:";
            while (*s && pos < 240) buf[pos++] = *s++;
            itoa(network_get_frames_sent(), num, 10);
            for (int k = 0; num[k] && pos < 240; k++) buf[pos++] = num[k];
            s = " udp_rx:";
            while (*s && pos < 240) buf[pos++] = *s++;
            itoa(network_get_udp_packets_received(), num, 10);
            for (int k = 0; num[k] && pos < 240; k++) buf[pos++] = num[k];
            s = " init:";
            while (*s && pos < 240) buf[pos++] = *s++;
            itoa(network_is_initialized() ? 1 : 0, num, 10);
            for (int k = 0; num[k] && pos < 240; k++) buf[pos++] = num[k];
            s = " has_ip:";
            while (*s && pos < 240) buf[pos++] = *s++;
            itoa(network_has_ip() ? 1 : 0, num, 10);
            for (int k = 0; num[k] && pos < 240; k++) buf[pos++] = num[k];
            buf[pos] = '\0';
            size_t copy_len = (pos < (int)cap - 1) ? pos : (size_t)(cap - 1);
            for (size_t i = 0; i < copy_len; i++) out[i] = buf[i];
            out[copy_len] = '\0';
            regs->rax = (uint64_t)copy_len;
            return 0;
        }
        case INT80_NET_SET_DNS: {
            uint32_t ip_val = (uint32_t)regs->rdi;
            ipv4_address_t ip;
            ip.bytes[0] = (ip_val >> 24) & 0xFF;
            ip.bytes[1] = (ip_val >> 16) & 0xFF;
            ip.bytes[2] = (ip_val >> 8) & 0xFF;
            ip.bytes[3] = ip_val & 0xFF;
            regs->rax = (uint64_t)network_set_dns_server(&ip);
            return 0;
        }
        case INT80_DESKAPI_PUSH: {
            const char* buf = (const char*)(uintptr_t)regs->rdi;
            uint64_t len = regs->rsi;
            if (!buf || len == 0 || len > DESKAPI_MAX_MSG || !user_ptr_range_ok(buf, (size_t)len)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)deskapi_push(buf, len);
            return 0;
        }
        case INT80_DESKAPI_POP: {
            char* out = (char*)(uintptr_t)regs->rdi;
            uint64_t cap = regs->rsi;
            uint64_t* out_len_ptr = (uint64_t*)(uintptr_t)regs->rdx;
            if (!out || cap == 0 || cap > (DESKAPI_MAX_MSG + 1u) || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (out_len_ptr && !user_ptr_range_ok(out_len_ptr, sizeof(uint64_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)deskapi_pop(out, cap, out_len_ptr);
            return 0;
        }
        case INT80_GET_MEM_INFO: {
            int80_mem_info_t* out = (int80_mem_info_t*)(uintptr_t)regs->rdi;
            if (!out || !user_ptr_range_ok(out, sizeof(*out))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            out->total_bytes = (uint64_t)pmm_get_total_usable_memory();
            out->free_bytes = (uint64_t)pmm_get_free_memory();
            regs->rax = 0;
            return 0;
        }
        case INT80_GET_DISK_STATS: {
            int80_disk_stats_t* out = (int80_disk_stats_t*)(uintptr_t)regs->rdi;
            if (!out || !user_ptr_range_ok(out, sizeof(*out))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            fs_get_io_stats(&out->read_bytes, &out->write_bytes);
            regs->rax = 0;
            return 0;
        }
        case INT80_GET_CPU_INFO: {
            int80_cpu_info_t* out = (int80_cpu_info_t*)(uintptr_t)regs->rdi;
            if (!out || !user_ptr_range_ok(out, sizeof(*out))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            out->ticks = get_tick_count();
            out->idle_ticks = get_idle_tick_count();
            out->hz = timer_get_hz();
            out->_pad = 0;
            regs->rax = 0;
            return 0;
        }
        case INT80_GET_CPU_BRAND: {
            char* out = (char*)(uintptr_t)regs->rdi;
            uint64_t cap = regs->rsi;
            if (!out || cap == 0 || cap > 128u || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            info_get_cpu_brand(out, (size_t)cap);
            regs->rax = 0;
            return 0;
        }
        case INT80_DIALOG_POP: {
            char* out = (char*)(uintptr_t)regs->rdi;
            uint64_t cap = regs->rsi;
            uint32_t* out_code = (uint32_t*)(uintptr_t)regs->rdx;
            int tid = int80_current_tid();
            if (tid < 0 || tid >= MAX_THREADS) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (!out || cap == 0 || cap > (uint64_t)DIALOG_MAX_TEXT || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (out_code && !user_ptr_range_ok(out_code, sizeof(uint32_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)deskapi_dialog_pop(tid, out, cap, out_code);
            return 0;
        }
        case INT80_DIALOG_PUSH: {
            int tid = (int)regs->rdi;
            uint32_t code = (uint32_t)regs->rsi;
            const char* text = (const char*)(uintptr_t)regs->rdx;
            if (tid < 0 || tid >= MAX_THREADS) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            if (!text || !user_cstr_ok(text, DIALOG_MAX_TEXT)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)deskapi_dialog_push(tid, code, text);
            return 0;
        }
        default:
            regs->rax = (uint64_t)-1;
            return 0;
    }
}

void syscall_user_thread_exit(int80_regs_t *regs) {
    (void)regs;
    console_input_release_if_current();
    thread_exit_current();
}




