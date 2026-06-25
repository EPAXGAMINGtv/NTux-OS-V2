/*
 * int80.c -- Syscall dispatch for int 0x80 (syscall/interrupt handler).
 *
 * This is THE interface between user-space programs and the kernel.
 * User programs trigger a software interrupt (int 0x80) with:
 *   - syscall number in rax
 *   - up to 3 arguments in rdi, rsi, rdx
 * The kernel saves all registers into an int80_regs_t struct, then
 * calls syscall_int80_dispatch() which handles each syscall number
 * in a giant switch statement.
 *
 * Each handler must:
 *   1. Validate all user-space pointers (user_ptr_range_ok / user_cstr_ok)
 *   2. Perform the requested operation
 *   3. Store the return value in regs->rax
 *   4. Return 0 (continue execution) or 1 (exit thread)
 *
 * Security: Every pointer from userspace is validated before use.
 * The helper functions current_user_range(), user_ptr_range_ok(),
 * and user_cstr_ok() prevent user-space programs from accessing
 * kernel memory or unmapped regions.
 */

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
#include <drivers/audio/intel_hda.h>

#include <lib/info.h>
#include <mm/kmalloc.h>
#include <mm/pmm.h>
#include <mm/umalloc.h>
#include <lib/string.h>
#include <lib/kutils.h>
#include <sys/user.h>

extern volatile struct limine_framebuffer_request framebuffer_request;

/*
 * Parse a dotted-decimal IPv4 string (e.g. "192.168.1.1") into an
 * ipv4_address_t struct. Returns 0 on success, -1 on parse error.
 */
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

/*
 * Directory entry struct for INT80_FS_LIST_DIR syscall.
 * Mirror of vfs_dirent_t but with explicit padding to ensure
 * a fixed ABI between kernel and userspace.
 */
typedef struct {
    char name[64];
    uint8_t is_dir;
    uint8_t _pad[7];
    uint64_t size;
} int80_fs_dirent_t;

#define COM1_PORT 0x3F8

static uint8_t g_serial_init_done = 0;

/*
 * Scale an 8-bit color channel (0-255) to an arbitrary bit depth.
 * For example, converting 8-bit red to 5-bit red (0-31).
 * Uses proper rounding to avoid banding artifacts.
 */
static uint32_t scale_chan8_to_n(uint8_t v, uint8_t bits) {
    if (bits == 0) return 0;
    if (bits >= 8) {
        /* Just shift up: fits exactly or exceeds destination width */
        return ((uint32_t)v) << (bits - 8);
    }
    /* Scale down with rounding: v * (2^bits - 1) / 255 */
    uint32_t maxv = (1u << bits) - 1u;
    return ((uint32_t)v * maxv + 127u) / 255u;
}

/*
 * Pack R,G,B values into a framebuffer-native pixel based on the
 * framebuffer's color mask layout (shift + size per channel).
 * This handles RGB565, RGB888, RGB332, and other bit layouts.
 */
static uint32_t pack_rgb_for_fb(uint8_t r, uint8_t g, uint8_t b, volatile struct limine_framebuffer *fb) {
    uint32_t pr = scale_chan8_to_n(r, fb->red_mask_size) << fb->red_mask_shift;
    uint32_t pg = scale_chan8_to_n(g, fb->green_mask_size) << fb->green_mask_shift;
    uint32_t pb = scale_chan8_to_n(b, fb->blue_mask_size) << fb->blue_mask_shift;
    return pr | pg | pb;
}

/*
 * Initialize COM1 serial port (if not done yet).
 * This sets up 115200 baud (divisor = 1 with DLAB on),
 * 8 data bits, no parity, 1 stop bit.
 *
 * Register layout (PC standard UART 16550):
 *   BASE+0 = data (DLAB=0) / divisor low (DLAB=1)
 *   BASE+1 = int enable (DLAB=0) / divisor high (DLAB=1)
 *   BASE+2 = interrupt ID / FIFO control
 *   BASE+3 = line control (bit 7 = DLAB, bits 0-1 = data, bit 3 = parity, bit 2 = stop)
 *   BASE+4 = modem control
 *   BASE+5 = line status
 */
static void serial_init_once(void) {
    if (g_serial_init_done) return;
    /* Disable interrupts */
    outb(COM1_PORT + 1, 0x00);
    /* Set DLAB=1 (bit 7) to configure baud rate divisor */
    outb(COM1_PORT + 3, 0x80);
    /* Divisor = 1 → 115200 baud (with 1.8432 MHz oscillator) */
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);
    /* Line control: 8N1 (8 bits, no parity, 1 stop) */
    outb(COM1_PORT + 3, 0x03);
    /* FIFO control: enable, clear, 14-byte threshold */
    outb(COM1_PORT + 2, 0xC7);
    /* Modem control: DTR+RTS on, enable IRQ routing */
    outb(COM1_PORT + 4, 0x0B);
    g_serial_init_done = 1;
}

/*
 * Try to read one character from the COM1 serial port.
 * Returns 1 if a character was read, 0 if no data available.
 * CR (\r) is converted to LF (\n) for consistency with the
 * terminal input convention used elsewhere in the kernel.
 */
static int serial_try_getchar(char* out) {
    if (!out) return 0;
    serial_init_once();
    /* Check line status register bit 0 = data ready */
    if ((inb(COM1_PORT + 5) & 0x01) == 0) return 0;
    uint8_t c = inb(COM1_PORT + 0);
    if (c == '\r') c = '\n';
    *out = (char)c;
    return 1;
}

/*
 * Safe multiplication: compute a * b, detect overflow.
 * Returns 0 on success (result in *out), 1 if overflow would occur.
 * If either operand is 0, result is 0 (trivially safe).
 */
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

/*
 * Get the virtual address range that the current user thread is
 * allowed to access. Returns 1 if a specific range is enforced,
 * 0 if unrestricted (e.g. kernel threads).
 */
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

/*
 * Validate that a user-space pointer + length is safe to access.
 *
 * Checks:
 *   1. If length is 0, any pointer (even NULL) is OK (no access needed).
 *   2. NULL pointer with non-zero length is rejected.
 *   3. Integer overflow from ptr + (len-1) is detected.
 *   4. If the thread has a restricted memory range, the whole range
 *      must be inside it.
 *
 * This prevents user-space programs from passing kernel pointers
 * or accessing memory they don't own.
 */
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

/*
 * Validate that a user-space C string is fully accessible and null-terminated.
 *
 * Scans up to 'max_scan' bytes for a null terminator, but also ensures
 * every byte scanned is within the user's allowed memory range.
 * Returns 1 if the string is safe to read, 0 otherwise.
 *
 * This is important because we can't trust strlen() on user pointers;
 * the string might not be null-terminated, or might cross into kernel memory.
 */
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

/*
 * Get the thread ID of the currently running thread.
 * Returns -1 if no valid thread is running (shouldn't happen).
 *
 * Note: Uses the global current_thread_id directly without locking.
 * This is safe because only one CPU core is active and the scheduler
 * only changes this during a context switch, which won't happen
 * while we're in a syscall handler.
 */
static int int80_current_tid(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS) return -1;
    if (!thread_list[tid]) return -1;
    return tid;
}

/*
 * Console input ownership helpers.
 * The "console" is the text-mode terminal input stream.
 * Only one thread can own it at a time (the foreground task).
 * These helpers check/release/claim ownership in terms of
 * the current thread's TID.
 */
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

/*
 * Case-insensitive substring search.
 * Returns 1 if 'sub' appears anywhere in 's', 0 otherwise.
 * The OR-with-32 trick converts uppercase ASCII to lowercase.
 */
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

/*
 * Main syscall dispatch handler.
 *
 * Called from the interrupt 0x80 handler (in assembly) after all registers
 * have been saved into an int80_regs_t struct. The syscall number is in
 * regs->rax, arguments are in regs->rdi, regs->rsi, regs->rdx.
 *
 * Each case in the switch handles one syscall:
 *   - Validates user pointers
 *   - Performs the operation
 *   - Stores result in regs->rax
 *   - Returns 0 to continue execution, or 1 to terminate the thread
 *
 * The syscall numbers are defined in int80.h.
 */
uint64_t syscall_int80_dispatch(int80_regs_t *regs) {
    if (!regs) return 0;

    switch (regs->rax) {
        /*
         * ── Console / Terminal Output ──────────────────────────────────
         */

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
        /*
         * ── Thread Lifecycle ───────────────────────────────────────────
         */

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
        /*
         * Block the current thread for a given number of timer ticks.
         *
         * How this works:
         *   1. We set the thread's wake_tick to the target time.
         *   2. We mark it BLOCKED, remove it from the runqueue, and add
         *      it to the wake_list (a list of sleeping threads).
         *   3. We call scheduler() which picks another thread to run.
         *   4. When the timer interrupt fires, the timer ISR checks the
         *      wake_list and marks any expired threads as READY, adding
         *      them back to the runqueue.
         *   5. When the scheduler picks our thread again, execution resumes
         *      here. We re-lock, check if we're still blocked (might have
         *      been woken early by something else), and HLT-loop until
         *      the wake time is actually reached.
         *
         * The HLT loop after scheduler() is a safety net: if the thread
         * was woken but the timer hasn't reached wake_tick yet, we wait
         * with minimal power consumption.
         */
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
                    wake_list_add(thread_list[tid]);
                }
            }
            thread_unlock_global();
            /*
             * Call the scheduler. This may context-switch away from
             * this thread if another thread is ready to run. When we
             * return here, either:
             *   a) A context switch happened (we were BLOCKED → READY
             *      by timer ISR → RUNNING by scheduler), OR
             *   b) No switch (still BLOCKED because no one else is ready).
             */
            scheduler();
            /*
             * Safety HLT loop: ensure we actually wait until wake time.
             * The timer ISR may wake us up early (e.g. to check conditions),
             * so we loop until the tick count reaches the target.
             * HLT puts the CPU in a low-power state until the next IRQ.
             */
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
                    wake_list_remove(t);
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
        /*
         * ── Graphics / Input / Screen ──────────────────────────────────
         */

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
        /*
         * ── System Control ─────────────────────────────────────────────
         */

        case INT80_REBOOT:
            system_reboot();
            regs->rax = 0;
            return 0;
        case INT80_SHUTDOWN:
            system_shutdown();
            regs->rax = 0;
            return 0;

        /*
         * ── Scheduling ─────────────────────────────────────────────────
         */

        case INT80_YIELD:
            thread_yield();
            regs->rax = 0;
            return 0;
        /*
         * ── Console Ownership ──────────────────────────────────────────
         * The console input can only be read by one thread at a time
         * (foreground task). These syscalls let user-space manage that.
         */

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
        /*
         * ── Process / Thread Management ────────────────────────────────
         * Start, list, kill, and query threads from user-space.
         */

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
        case INT80_GET_TID: {
            int tid = int80_current_tid();
            regs->rax = (tid >= 0) ? (uint64_t)tid : (uint64_t)-1;
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
        /*
         * ── User / Group / Permissions ─────────────────────────────────
         */

        case INT80_GETEUID: {
            regs->rax = (uint64_t)thread_get_current_euid();
            return 0;
        }
        case INT80_SETGID: {
            regs->rax = (uint64_t)thread_set_current_gid((uint32_t)regs->rdi);
            return 0;
        }
        case INT80_GETGID: {
            regs->rax = (uint64_t)thread_get_current_gid();
            return 0;
        }
        case INT80_SETEGID: {
            regs->rax = (uint64_t)thread_set_current_egid((uint32_t)regs->rdi);
            return 0;
        }
        case INT80_GETEGID: {
            regs->rax = (uint64_t)thread_get_current_egid();
            return 0;
        }
        case INT80_CHMOD: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            uint32_t mode = (uint32_t)regs->rsi;
            if (!user_cstr_ok(path, 1024u)) { regs->rax = (uint64_t)-1; return 0; }
            regs->rax = (uint64_t)vfs_chmod(path, (uint16_t)mode);
            return 0;
        }
        case INT80_CHOWN: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            uint32_t owner = (uint32_t)regs->rsi;
            uint32_t group = (uint32_t)regs->rdx;
            if (!user_cstr_ok(path, 1024u)) { regs->rax = (uint64_t)-1; return 0; }
            regs->rax = (uint64_t)vfs_chown(path, owner, group);
            return 0;
        }
        case INT80_STAT: {
            const char* path = (const char*)(uintptr_t)regs->rdi;
            void* buf = (void*)(uintptr_t)regs->rsi;
            if (!user_cstr_ok(path, 1024u) || !user_ptr_range_ok(buf, 64)) {
                regs->rax = (uint64_t)-1; return 0;
            }
            uint16_t mode;
            uint32_t uid, gid;
            if (vfs_getattr(path, &mode, &uid, &gid) != 0) {
                regs->rax = (uint64_t)-1; return 0;
            }
            size_t size = 0;
            vfs_read_file(path, NULL, 0, &size);
            memset(buf, 0, 64);
            ((uint16_t*)buf)[0] = mode;
            ((uint32_t*)buf)[1] = uid;
            ((uint32_t*)buf)[2] = gid;
            ((uint64_t*)buf)[2] = size;
            regs->rax = 0;
            return 0;
        }
        case INT80_UMASK: {
            regs->rax = 0;
            return 0;
        }
        case INT80_GETGROUPS: {
            uint32_t* out = (uint32_t*)(uintptr_t)regs->rdi;
            int max = (int)regs->rsi;
            if (out && !user_ptr_range_ok(out, (size_t)max * sizeof(uint32_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)thread_get_current_groups(out, max);
            return 0;
        }
        case INT80_SETGROUPS: {
            const uint32_t* groups = (const uint32_t*)(uintptr_t)regs->rdi;
            int n = (int)regs->rsi;
            if (groups && !user_ptr_range_ok(groups, (size_t)n * sizeof(uint32_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)thread_set_current_groups(groups, n);
            return 0;
        }
        case INT80_AUTH_USER: {
            const char* name = (const char*)(uintptr_t)regs->rdi;
            const char* pass = (const char*)(uintptr_t)regs->rsi;
            if (!user_cstr_ok(name, USER_MAX_NAME) || !user_cstr_ok(pass, USER_MAX_PASS)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)sys_auth_user(name, pass);
            return 0;
        }
        case INT80_GETPWNAM: {
            char* out = (char*)(uintptr_t)regs->rdi;
            const char* name = (const char*)(uintptr_t)regs->rsi;
            if (!out || !user_ptr_range_ok(out, 256) || !user_cstr_ok(name, USER_MAX_NAME)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            const user_entry_t* u = sys_user_get_by_name(name);
            if (!u) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char buf[256];
            int pos = 0;
            char* p = u->name; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            char uid_s[16]; itoa((int)u->uid, uid_s, 10);
            p = uid_s; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            char gid_s[16]; itoa((int)u->gid, gid_s, 10);
            p = gid_s; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            p = u->home; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            p = u->shell; while (*p && pos < 250) buf[pos++] = *p++;
            buf[pos] = '\0';
            memcpy(out, buf, (size_t)pos + 1);
            regs->rax = 0;
            return 0;
        }
        case INT80_GETPWUID: {
            char* out = (char*)(uintptr_t)regs->rdi;
            uint32_t uid = (uint32_t)regs->rsi;
            if (!out || !user_ptr_range_ok(out, 256)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            const user_entry_t* u = sys_user_get_by_uid(uid);
            if (!u) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char buf[256];
            int pos = 0;
            char* p = u->name; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            char uid_s[16]; itoa((int)u->uid, uid_s, 10);
            p = uid_s; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            char gid_s[16]; itoa((int)u->gid, gid_s, 10);
            p = gid_s; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            p = u->home; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            p = u->shell; while (*p && pos < 250) buf[pos++] = *p++;
            buf[pos] = '\0';
            memcpy(out, buf, (size_t)pos + 1);
            regs->rax = 0;
            return 0;
        }
        case INT80_GETGRNAM: {
            char* out = (char*)(uintptr_t)regs->rdi;
            const char* name = (const char*)(uintptr_t)regs->rsi;
            if (!out || !user_ptr_range_ok(out, 256) || !user_cstr_ok(name, USER_MAX_NAME)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            const group_entry_t* g = sys_group_get_by_name(name);
            if (!g) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char buf[256];
            int pos = 0;
            char* p = g->name; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            char gid_s[16]; itoa((int)g->gid, gid_s, 10);
            p = gid_s; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            for (int j = 0; j < g->member_count; ++j) {
                if (j > 0 && pos < 250) buf[pos++] = ',';
                char uid_s[16]; itoa((int)g->members[j], uid_s, 10);
                p = uid_s; while (*p && pos < 250) buf[pos++] = *p++;
            }
            buf[pos] = '\0';
            memcpy(out, buf, (size_t)pos + 1);
            regs->rax = 0;
            return 0;
        }
        case INT80_GETGRGID: {
            char* out = (char*)(uintptr_t)regs->rdi;
            uint32_t gid = (uint32_t)regs->rsi;
            if (!out || !user_ptr_range_ok(out, 256)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            const group_entry_t* g = sys_group_get_by_gid(gid);
            if (!g) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            char buf[256];
            int pos = 0;
            char* p = g->name; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            char gid_s[16]; itoa((int)g->gid, gid_s, 10);
            p = gid_s; while (*p && pos < 250) buf[pos++] = *p++;
            if (pos < 250) buf[pos++] = ':';
            for (int j = 0; j < g->member_count; ++j) {
                if (j > 0 && pos < 250) buf[pos++] = ',';
                char uid_s[16]; itoa((int)g->members[j], uid_s, 10);
                p = uid_s; while (*p && pos < 250) buf[pos++] = *p++;
            }
            buf[pos] = '\0';
            memcpy(out, buf, (size_t)pos + 1);
            regs->rax = 0;
            return 0;
        }
        case INT80_GETPPID: {
            regs->rax = 0;
            return 0;
        }
        case INT80_SETSID: {
            regs->rax = 0;
            return 0;
        }
        case INT80_GETPGID: {
            regs->rax = 0;
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
        /*
         * ── Filesystem Operations ──────────────────────────────────────
         * CRITICAL: All paths are validated via user_cstr_ok() before use.
         * Paths must be null-terminated and within user address space.
         */

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
        /*
         * ── Mouse / Keyboard Input ─────────────────────────────────────
         */

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
        case INT80_KBD_SET_LAYOUT: {
            const char* name = (const char*)(uintptr_t)regs->rdi;
            if (!user_cstr_ok(name, 32u) || !name[0]) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (keyboard_set_layout(name) == 0) ? 0u : (uint64_t)-1;
            return 0;
        }
        /*
         * ── Framebuffer / Pixel Graphics ───────────────────────────────
         * These syscalls allow user-space to draw pixels directly on the
         * framebuffer. The kernel validates all pixel data bounds and
         * converts from 32-bit RGBA to the framebuffer's native pixel format.
         */

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
        /*
         * Blit a 32-bit RGBA image from user-space to the framebuffer.
         *
         * The source image has one 32-bit pixel per element (R, G, B, A
         * in bytes 2,1,0,3 respectively). The framebuffer may use a
         * different pixel format (e.g. RGB565, XRGB8888), so we convert
         * each pixel using the framebuffer's color mask information.
         *
         * Validation steps:
         *   1. Source dimensions and pitch must be non-zero and consistent.
         *   2. The entire source buffer must be within user address space.
         *   3. The framebuffer must be RGB format with valid color masks.
         *   4. Copy dimensions are clamped to framebuffer dimensions.
         */
        case INT80_FB_BLIT32: {
            const uint32_t* src = (const uint32_t*)(uintptr_t)regs->rdi;
            uint32_t src_w = (uint32_t)regs->rsi;
            uint32_t src_h = (uint32_t)regs->rdx;
            uint32_t src_pitch = (uint32_t)regs->rcx;
            if (!src || src_w == 0 || src_h == 0 || src_pitch == 0) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            /* Check for integer overflow in width */
            if (src_w > (UINT32_MAX / sizeof(uint32_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            /* Pitch must be at least width * pixel size (no negative stride) */
            if (src_pitch < src_w * sizeof(uint32_t)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            /* Verify the full source buffer is in user address space */
            if ((uint64_t)src_pitch > 0 && src_h > 0) {
                size_t src_bytes = 0;
                if (mul_overflow_size((size_t)src_pitch, (size_t)src_h, &src_bytes) || !user_ptr_range_ok(src, src_bytes)) {
                    regs->rax = (uint64_t)-1;
                    return 0;
                }
            }
            /* Check framebuffer availability and format */
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
            /* All color channels must have non-zero mask size */
            if (fb->red_mask_size == 0 || fb->green_mask_size == 0 || fb->blue_mask_size == 0) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            /* Color masks must not overflow the pixel width */
            if ((uint32_t)fb->red_mask_shift + (uint32_t)fb->red_mask_size > (uint32_t)fb->bpp ||
                (uint32_t)fb->green_mask_shift + (uint32_t)fb->green_mask_size > (uint32_t)fb->bpp ||
                (uint32_t)fb->blue_mask_shift + (uint32_t)fb->blue_mask_size > (uint32_t)fb->bpp) {
                regs->rax = (uint64_t)-1;
                return 0;
            }

            /* Clamp copy size to framebuffer dimensions */
            uint32_t copy_w = src_w;
            uint32_t copy_h = src_h;
            if (copy_w > (uint32_t)fb->width) copy_w = (uint32_t)fb->width;
            if (copy_h > (uint32_t)fb->height) copy_h = (uint32_t)fb->height;

            /*
             * Convert pixels row by row.
             * Source: 32-bit RGBA (user-space)
             * Destination: framebuffer-native pixel format
             *
             * We use the framebuffer's color mask shift/size info to
             * pack each channel correctly. The source is always treated
             * as little-endian 0xAABBGGRR (standard x86 layout).
             */
            uint8_t* dst = (uint8_t*)(uintptr_t)fb->address;
            for (uint32_t y = 0; y < copy_h; ++y) {
                const uint8_t* src_row = (const uint8_t*)(const void*)src + (size_t)y * (size_t)src_pitch;
                uint8_t* dst_row = dst + (size_t)y * (size_t)fb->pitch;
                const uint32_t* s = (const uint32_t*)(const void*)src_row;
                for (uint32_t x = 0; x < copy_w; ++x) {
                    uint32_t p = s[x];
                    /* Extract R,G,B from 32-bit word (x86: B at byte 0, G at 1, R at 2) */
                    uint8_t b = (uint8_t)(p & 0xFFu);
                    uint8_t g = (uint8_t)((p >> 8) & 0xFFu);
                    uint8_t r = (uint8_t)((p >> 16) & 0xFFu);
                    uint32_t out = pack_rgb_for_fb(r, g, b, fb);
                    /* Write pixel bytes in framebuffer-native byte order */
                    if (fb->bpp == 32) {
                        dst_row[x * 4u + 0u] = (uint8_t)(out & 0xFFu);
                        dst_row[x * 4u + 1u] = (uint8_t)((out >> 8) & 0xFFu);
                        dst_row[x * 4u + 2u] = (uint8_t)((out >> 16) & 0xFFu);
                        dst_row[x * 4u + 3u] = (uint8_t)((out >> 24) & 0xFFu);
                    } else {
                        /* 24-bit framebuffer (3 bytes per pixel, no alpha) */
                        dst_row[x * 3u + 0u] = (uint8_t)(out & 0xFFu);
                        dst_row[x * 3u + 1u] = (uint8_t)((out >> 8) & 0xFFu);
                        dst_row[x * 3u + 2u] = (uint8_t)((out >> 16) & 0xFFu);
                    }
                }
            }
            regs->rax = 0;
            return 0;
        }
        /*
         * ── Misc / Time / Info ─────────────────────────────────────────
         */

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
        /*
         * ── Block Device I/O ───────────────────────────────────────────
         * Raw sector-level access to storage devices (AHCI, ATA, etc.).
         * Users can list drives, partitions, read/write sectors,
         * and create/format filesystems.
         */

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
        /*
         * ── File Descriptor Operations ─────────────────────────────────
         * POSIX-like open/read/write/close/ioctl/lseek on files.
         * These wrap the fd_*() layer which provides per-thread
         * file descriptor tables on top of the VFS.
         */

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
        /*
         * ── Networking ─────────────────────────────────────────────────
         * Ping (ICMP), HTTP GET (TCP), and debug/status queries.
         * All network operations go through the kernel's network stack.
         */

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
        /*
         * HTTP GET request — a simple user-space HTTP client via the kernel.
         *
         * Handles:
         *   - URL parsing ("http://host/path")
         *   - DNS lookup (if host is not an IP address)
         *   - TCP connection on port 80
         *   - HTTP/1.1 GET request with redirect following (up to 5)
         *   - Response body extraction (content-length or chunked transfer)
         *   - Plain-text reply (no TLS — HTTPS returns an error)
         *
         * This is intentionally a minimal implementation for a browser.
         * A more complete TCP/IP stack would separate these concerns,
         * but for a hobby OS this works well enough.
         */
        case INT80_NET_HTTP_GET: {
            const char* url = (const char*)(uintptr_t)regs->rdi;
            char* out = (char*)(uintptr_t)regs->rsi;
            uint64_t cap = regs->rdx;
            if (!user_cstr_ok(url, 512u) || !out || cap == 0 || cap > 65536u || !user_ptr_range_ok(out, (size_t)cap)) {
                regs->rax = (uint64_t)-1;
                return 0;
            }

            /*
             * Allocate 65537 bytes so we have room for a null terminator
             * after reading up to 65536 bytes of HTTP response data.
             */
            char* buf = (char*)kmalloc(65537);
            if (!buf) { regs->rax = -1; return 0; }

            char curr_url[512];
            strncpy(curr_url, url, sizeof(curr_url) - 1);
            curr_url[sizeof(curr_url) - 1] = '\0';

            int redirects = 0;
            int body_len = -1;

            /*
             * Follow redirects up to 5 times. Each iteration:
             *   1. Parse the URL into host + path
             *   2. DNS lookup (if host isn't an IP)
             *   3. Connect via TCP on port 80
             *   4. Send HTTP GET request
             *   5. Parse response headers
             *   6. If 3xx redirect, update URL and retry
             *   7. If 2xx OK, extract body and return
             */
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

                /* Parse host and path from URL */
                char host[128];
                char path[256];
                int slash = -1;
                for (int i = 0; p[i]; i++) {
                    if (p[i] == '/' && slash < 0) slash = i;
                }
                if (slash < 0) {
                    /* No path: use "/" as default */
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

                /* Find the empty line (\r\n\r\n) separating headers from body */
                char* hdr_end = NULL;
                for (int i = 0; i < total - 3; i++) {
                    if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                        hdr_end = buf + i;
                        break;
                    }
                }
                if (!hdr_end) { body_len = -1; break; }

                /* Parse HTTP status code (e.g. "HTTP/1.1 200 OK") */
                int status = 0;
                char* sp = buf;
                while (*sp && *sp != ' ') sp++;
                if (*sp == ' ') { sp++; while (*sp >= '0' && *sp <= '9') { status = status * 10 + (*sp - '0'); sp++; } }

                /*
                 * Parse response headers looking for:
                 *   - Content-Length: size of body
                 *   - Transfer-Encoding: chunked
                 *   - Location: redirect URL (for 3xx responses)
                 *
                 * Headers are CRLF-delimited. We null-terminate each line
                 * temporarily to use strcmp, then restore the delimiter.
                 */
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

                /* Handle 3xx redirect: update current URL and retry */
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
                    /*
                     * Chunked transfer encoding: body is split into chunks.
                     * Each chunk: <size-in-hex>\r\n<data>\r\n
                     * Last chunk has size 0. We decode in-place.
                     */
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
        /*
         * Return internal network stack debug statistics as a string.
         * Shows packet counts, initialization status, and IP assignment.
         */
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
        /*
         * ── Desktop IPC (DeskAPI) ──────────────────────────────────────
         * Simple message passing between user-space programs.
         * Used by the GUI desktop environment.
         */

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
        /*
         * ── System Information ─────────────────────────────────────────
         */

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
        /*
         * ── Audio ──────────────────────────────────────────────────────
         */

        case INT80_AUDIO_PLAY: {
            const int16_t* buf = (const int16_t*)(uintptr_t)regs->rdi;
            uint32_t count = (uint32_t)regs->rsi;
            if (!buf || count == 0 || !user_ptr_range_ok(buf, count * sizeof(int16_t))) {
                regs->rax = (uint64_t)-1;
                return 0;
            }
            regs->rax = (uint64_t)audio_play(buf, count);
            return 0;
        }
        case INT80_AUDIO_STOP: {
            regs->rax = (uint64_t)audio_stop();
            return 0;
        }
        case INT80_AUDIO_STATUS: {
            regs->rax = audio_is_playing() ? 1u : 0u;
            return 0;
        }
        /*
         * ── User-Space Memory Allocation ───────────────────────────────
         * These syscalls let user-space allocate/free heap memory
         * through the kernel's umalloc/ufree interface.
         */

        case INT80_UMALLOC: {
            size_t sz = (size_t)regs->rdi;
            void *p = umalloc(sz);
            regs->rax = (uint64_t)(uintptr_t)p;
            return 0;
        }
        case INT80_UFREE: {
            void *ptr = (void *)(uintptr_t)regs->rdi;
            ufree(ptr);
            regs->rax = 0;
            return 0;
        }
        default:
            regs->rax = (uint64_t)-1;
            return 0;
    }
}

/*
 * Called when a user-space thread exits (e.g. via return from main()
 * or explicit syscall exit). Cleans up console ownership and then
 * terminates the thread through the scheduler.
 *
 * Note: This function never returns — thread_exit_current() does
 * a context switch to the next ready thread.
 */
void syscall_user_thread_exit(int80_regs_t *regs) {
    (void)regs;
    console_input_release_if_current();
    thread_exit_current();
}




