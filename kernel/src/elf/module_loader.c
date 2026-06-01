#include <elf/module_loader.h>
#include <drivers/input/console_input.h>

#include <elf/elf.h>
#include <sched/thread.h>
#include <drivers/framebuffer/kprint.h>
#include <fs/fs.h>
#include <arch/x86_64/gdt.h>
#include <mm/kmalloc.h>
#include <mm/pmm.h>
#include <mm/paging.h>
#include <mm/hhdm.h>
#include <arch/x86_64/usermode.h>
#include <lib/string.h>
#include <limine.h>

#define NTUX_USERMODE_DISABLE_RING3 0
#define NTUX_ELF_PT_LOAD 1u
#define NTUX_USER_STACK_PAGES 16u
#define NTUX_USER_ASLR_MIN 0x00160000ull
#define NTUX_USER_ASLR_MAX 0x20000000ull
#define NTUX_USER_ASLR_STEP 0x00200000ull
#define NTUX_USER_HEAP_EXTRA_BYTES (128u * 1024u * 1024u)
#define NTUX_USER_RESV_MAX 64

typedef struct {
    void (*print)(const char *text);
} ntux_host_api_t;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request g_module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

static bool g_modules_ready = false;
static struct limine_module_response *g_module_response = NULL;
static const void *g_hello_module_addr = NULL;
static uint64_t g_hello_module_size = 0;
static volatile uint8_t g_hello_launch_busy = 0;

typedef enum {
    HELLO_LAUNCH_RING0 = 0,
    HELLO_LAUNCH_RING3 = 1
} hello_launch_mode_t;

static volatile hello_launch_mode_t g_hello_launch_mode = HELLO_LAUNCH_RING0;
static volatile uint8_t g_elf_launch_busy = 0;
static volatile int32_t g_hello_launch_tid = -1;
static volatile int32_t g_elf_launch_tid = -1;
static void* g_elf_launch_image = NULL;
static size_t g_elf_launch_size = 0;
static struct limine_file *find_module_by_token(const char *token);
static volatile uint8_t g_user_range_lock = 0;
static volatile uint32_t g_ring3_rr_hint = 0;
typedef struct {
    uint64_t start;
    uint64_t end;
    uint8_t in_use;
} user_range_reservation_t;

static user_range_reservation_t g_user_resv[NTUX_USER_RESV_MAX];
static volatile int g_hello_launch_result = 0;
static volatile int g_elf_launch_result = 0;

enum {
    NTUX_LAUNCH_PENDING = 0,
    NTUX_LAUNCH_OK = 1,
    NTUX_LAUNCH_ERR_TIMEOUT = -6,
    NTUX_LAUNCH_ERR_MISSING_IMAGE = -1,
    NTUX_LAUNCH_ERR_OOM = -2,
    NTUX_LAUNCH_ERR_NO_USER_RANGE = -3,
    NTUX_LAUNCH_ERR_ELF_LOAD = -4,
    NTUX_LAUNCH_ERR_USER_MAP = -5
};

static void user_range_lock(void) {
    while (__atomic_test_and_set(&g_user_range_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static void user_range_unlock(void) {
    __atomic_clear(&g_user_range_lock, __ATOMIC_RELEASE);
}

static uint64_t read_ticks(void) {
    extern uint64_t get_tick_count(void);
    return get_tick_count();
}

static int wait_launch_result(volatile int* result, uint64_t timeout_ticks) {
    uint64_t start = read_ticks();
    if (!result) return NTUX_LAUNCH_ERR_MISSING_IMAGE;
    for (;;) {
        int st = __atomic_load_n(result, __ATOMIC_ACQUIRE);
        if (st != NTUX_LAUNCH_PENDING) return st;
        if ((read_ticks() - start) >= timeout_ticks) return NTUX_LAUNCH_ERR_TIMEOUT;
        thread_yield();
        __asm__ volatile("pause");
    }
}

static int last_hello_launch_tid(void) {
    return (int)__atomic_load_n(&g_hello_launch_tid, __ATOMIC_ACQUIRE);
}

static int last_elf_launch_tid(void) {
    return (int)__atomic_load_n(&g_elf_launch_tid, __ATOMIC_ACQUIRE);
}

static const char* launch_result_text(int st) {
    switch (st) {
        case NTUX_LAUNCH_OK: return "task started";
        case NTUX_LAUNCH_PENDING: return "task launch pending";
        case NTUX_LAUNCH_ERR_TIMEOUT: return "task launch timeout";
        case NTUX_LAUNCH_ERR_MISSING_IMAGE: return "launch image missing";
        case NTUX_LAUNCH_ERR_OOM: return "out of memory";
        case NTUX_LAUNCH_ERR_NO_USER_RANGE: return "no free user VA range";
        case NTUX_LAUNCH_ERR_ELF_LOAD: return "invalid or unsupported ELF";
        case NTUX_LAUNCH_ERR_USER_MAP: return "user mapping failed";
        default: return "task launch failed";
    }
}

static uintptr_t phys_to_virt(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    if (off) {
        return (uintptr_t)(phys + (uintptr_t)off);
    }
    return phys;
}

static inline uint64_t read_cr3(void) {
    uint64_t v = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static uint64_t clone_kernel_pml4(void) {
    uint64_t cr3 = read_cr3();
    uint64_t cur_phys = cr3 & ~0xFFFull;
    uint64_t* cur = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)cur_phys);

    void* new_page = pmm_alloc_page();
    if (!new_page) return 0;
    uint64_t new_phys = (uint64_t)(uintptr_t)new_page & ~0xFFFull;
    uint64_t* dst = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)new_phys);
    memset(dst, 0, PAGE_SIZE);

    for (uint32_t i = 256; i < 512; ++i) {
        dst[i] = cur[i];
    }
    return new_phys;
}

static int map_user_page_pml4(uint64_t pml4_phys, uint64_t vaddr) {
    const uint64_t pml4_i = (vaddr >> 39) & 0x1FFu;
    const uint64_t pdpt_i = (vaddr >> 30) & 0x1FFu;
    const uint64_t pd_i   = (vaddr >> 21) & 0x1FFu;
    const uint64_t pt_i   = (vaddr >> 12) & 0x1FFu;

    uint64_t* pml4 = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)pml4_phys);
    uint64_t e = pml4[pml4_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pdpt = pmm_alloc_page();
        if (!new_pdpt) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_pdpt), 0, PAGE_SIZE);
        pml4[pml4_i] = ((uint64_t)(uintptr_t)new_pdpt & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        e = pml4[pml4_i];
    } else {
        pml4[pml4_i] = e | PAGE_USER | PAGE_WRITABLE;
    }

    uint64_t* pdpt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pdpt[pdpt_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pd = pmm_alloc_page();
        if (!new_pd) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_pd), 0, PAGE_SIZE);
        pdpt[pdpt_i] = ((uint64_t)(uintptr_t)new_pd & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        e = pdpt[pdpt_i];
    } else {
        pdpt[pdpt_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    if (e & PAGE_HUGE) return 0;

    uint64_t* pd = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pd[pd_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pt = pmm_alloc_page();
        if (!new_pt) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_pt), 0, PAGE_SIZE);
        pd[pd_i] = ((uint64_t)(uintptr_t)new_pt & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        e = pd[pd_i];
    } else {
        pd[pd_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    if (e & PAGE_HUGE) return 0;

    uint64_t* pt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pt[pt_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_page = pmm_alloc_page();
        if (!new_page) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_page), 0, PAGE_SIZE);
        pt[pt_i] = ((uint64_t)(uintptr_t)new_page & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    } else {
        pt[pt_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    return 0;
}

static int map_user_range_pml4(uint64_t pml4_phys, uint64_t start, uint64_t end) {
    if (end <= start) return -1;
    for (uint64_t v = start; v < end; v += 0x1000ull) {
        if (map_user_page_pml4(pml4_phys, v) != 0) return -1;
    }
    return 0;
}

static uint64_t align_up_u64(uint64_t v, uint64_t align) {
    if (align == 0) return v;
    uint64_t m = align - 1u;
    return (v + m) & ~m;
}

static uint64_t align_down_u64(uint64_t v, uint64_t align) {
    if (align == 0) return v;
    return v & ~(align - 1u);
}

static int extract_elf_load_range_raw(const void* elf_image, size_t elf_size, uint64_t* out_start, uint64_t* out_end) {
    if (!elf_image || elf_size < sizeof(Elf64_Ehdr) || !out_start || !out_end) return -1;

    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)elf_image;
    if (eh->e_phnum == 0 || eh->e_phentsize != sizeof(Elf64_Phdr)) return -1;
    if (eh->e_phoff > elf_size) return -1;

    uint64_t pht_bytes = (uint64_t)eh->e_phnum * (uint64_t)eh->e_phentsize;
    if (eh->e_phoff + pht_bytes > elf_size) return -1;

    const Elf64_Phdr* ph = (const Elf64_Phdr*)((const uint8_t*)elf_image + eh->e_phoff);
    uint64_t start = UINT64_MAX;
    uint64_t end = 0;
    int found = 0;

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != NTUX_ELF_PT_LOAD || ph[i].p_memsz == 0) continue;
        uint64_t seg_start = ph[i].p_vaddr;
        uint64_t seg_end = ph[i].p_vaddr + ph[i].p_memsz;
        if (seg_end < seg_start) return -1;
        if (seg_start < start) start = seg_start;
        if (seg_end > end) end = seg_end;
        found = 1;
    }

    if (!found) return -1;
    *out_start = start & ~0xFFFull;
    *out_end = align_up_u64(end, 0x1000ull);
    return (*out_end > *out_start) ? 0 : -1;
}

static int compute_user_stack_top(const void* elf_image, size_t elf_size, uint64_t* out_stack_top) {
    uint64_t user_start = 0;
    uint64_t user_end = 0;
    if (!out_stack_top) return -1;
    if (extract_elf_load_range_raw(elf_image, elf_size, &user_start, &user_end) != 0) return -1;
    uint64_t extra = 0x1000ull;
    extra += (uint64_t)NTUX_USER_STACK_PAGES * 0x1000ull;
    extra += (uint64_t)NTUX_USER_HEAP_EXTRA_BYTES;
    if (user_end <= UINT64_MAX - extra) {
        user_end += extra;
    }
    if (user_end < user_start + 0x1000ull + 0x10ull) return -1;
    /* SysV ABI: RSP%16==8 on entry (as if return address was pushed). */
    *out_stack_top = (user_end & ~0xFull) - 0x8ull;
    return 0;
}

static int get_current_thread_slot(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS) return -1;
    if (!thread_list[tid]) return -1;
    return tid;
}

static void set_current_thread_user_range(uint64_t start, uint64_t end) {
    int tid = get_current_thread_slot();
    if (tid < 0) return;
    thread_lock_global();
    thread_list[tid]->user_vstart = start;
    thread_list[tid]->user_vend = end;
    thread_unlock_global();
}

static bool user_ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end) {
    return !(a_end <= b_start || a_start >= b_end);
}

static bool user_range_overlaps_running(uint64_t start, uint64_t end) {
    bool overlap = false;
    thread_lock_global();
    for (int i = 0; i < MAX_THREADS; ++i) {
        thread_t* t = thread_list[i];
        if (!t) continue;
        if (t->state == THREAD_TERMINATED) continue;
        if (t->user_vstart == 0 || t->user_vend <= t->user_vstart) continue;
        if (user_ranges_overlap(start, end, t->user_vstart, t->user_vend)) {
            overlap = true;
            break;
        }
    }
    thread_unlock_global();
    return overlap;
}

static bool user_range_overlaps_reserved(uint64_t start, uint64_t end) {
    for (uint32_t i = 0; i < NTUX_USER_RESV_MAX; ++i) {
        if (!g_user_resv[i].in_use) continue;
        if (user_ranges_overlap(start, end, g_user_resv[i].start, g_user_resv[i].end)) return true;
    }
    return false;
}

static int reserve_user_range_slot(uint64_t start, uint64_t end) {
    for (uint32_t i = 0; i < NTUX_USER_RESV_MAX; ++i) {
        if (g_user_resv[i].in_use) continue;
        g_user_resv[i].start = start;
        g_user_resv[i].end = end;
        g_user_resv[i].in_use = 1;
        return (int)i;
    }
    return -1;
}

static void release_user_range_slot(int slot) {
    if (slot < 0 || slot >= (int)NTUX_USER_RESV_MAX) return;
    g_user_resv[slot].start = 0;
    g_user_resv[slot].end = 0;
    g_user_resv[slot].in_use = 0;
}

static int choose_dynamic_user_base(uint64_t span, uint64_t* out_base, int* out_resv_slot) {
    if (!out_base || span == 0) return -1;
    if (!out_resv_slot) return -1;
    span = align_up_u64(span, 0x1000ull);
    user_range_lock();
    for (uint64_t base = NTUX_USER_ASLR_MIN; base + span <= NTUX_USER_ASLR_MAX; base += NTUX_USER_ASLR_STEP) {
        uint64_t end = base + span;
        int slot = reserve_user_range_slot(base, end);
        if (slot < 0) break;
        *out_base = base;
        *out_resv_slot = slot;
        user_range_unlock();
        return 0;
    }
    user_range_unlock();
    return -1;
}

static int rebase_elf_image_inplace(void* elf_image, size_t elf_size, uint64_t* out_start, uint64_t* out_end, int* out_resv_slot) {
    if (!elf_image || elf_size < sizeof(Elf64_Ehdr)) return -1;
    if (!out_resv_slot) return -1;
    Elf64_Ehdr* eh = (Elf64_Ehdr*)elf_image;
    if (eh->e_phnum == 0 || eh->e_phentsize != sizeof(Elf64_Phdr)) return -1;
    if (eh->e_phoff > elf_size) return -1;
    uint64_t pht_bytes = (uint64_t)eh->e_phnum * (uint64_t)eh->e_phentsize;
    if (eh->e_phoff + pht_bytes > elf_size) return -1;

    uint64_t old_start = 0;
    uint64_t old_end = 0;
    if (extract_elf_load_range_raw(elf_image, elf_size, &old_start, &old_end) != 0) return -1;
    uint64_t raw_start = old_start;
    uint64_t raw_end = old_end;

    uint64_t span = 0;
    uint64_t extra = 0x1000ull; /* guard page */
    extra += (uint64_t)NTUX_USER_STACK_PAGES * 0x1000ull;
    extra += (uint64_t)NTUX_USER_HEAP_EXTRA_BYTES;
    if (raw_end < raw_start) return -1;
    span = align_up_u64(raw_end, 0x1000ull) - align_down_u64(raw_start, 0x1000ull);
    if (span > UINT64_MAX - extra) return -1;
    span += extra;

    if (eh->e_type == 2) { /* ET_EXEC: fixed virtual addresses */
        user_range_lock();
        int slot = reserve_user_range_slot(align_down_u64(raw_start, 0x1000ull),
                                           align_down_u64(raw_start, 0x1000ull) + span);
        user_range_unlock();
        if (slot < 0) return -1;
        *out_resv_slot = slot;
        if (out_start) *out_start = align_down_u64(raw_start, 0x1000ull);
        if (out_end) *out_end = align_down_u64(raw_start, 0x1000ull) + span;
        return 0;
    }

    uint64_t new_base = 0;
    int slot = -1;
    if (choose_dynamic_user_base(span, &new_base, &slot) != 0) return -1;
    *out_resv_slot = slot;
    if (new_base == raw_start) {
        if (out_start) *out_start = old_start;
        if (out_end) *out_end = align_down_u64(raw_start, 0x1000ull) + span;
        return 0;
    }

    uint64_t delta = new_base - raw_start;
    if (eh->e_entry > UINT64_MAX - delta) {
        release_user_range_slot(slot);
        *out_resv_slot = -1;
        return -1;
    }
    eh->e_entry += delta;

    Elf64_Phdr* ph = (Elf64_Phdr*)((uint8_t*)elf_image + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != NTUX_ELF_PT_LOAD || ph[i].p_memsz == 0) continue;
        if (ph[i].p_vaddr > UINT64_MAX - delta) {
            release_user_range_slot(slot);
            *out_resv_slot = -1;
            return -1;
        }
        if (ph[i].p_paddr > UINT64_MAX - delta) {
            release_user_range_slot(slot);
            *out_resv_slot = -1;
            return -1;
        }
        ph[i].p_vaddr += delta;
        ph[i].p_paddr += delta;
    }

    if (out_start) *out_start = align_down_u64(new_base, 0x1000ull);
    if (out_end) *out_end = align_down_u64(new_base, 0x1000ull) + span;
    return 0;
}

static void invlpg_addr(uint64_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"((uintptr_t)vaddr) : "memory");
}

static int mark_user_page_mapped(uint64_t vaddr) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    const uint64_t pml4_i = (vaddr >> 39) & 0x1FFu;
    const uint64_t pdpt_i = (vaddr >> 30) & 0x1FFu;
    const uint64_t pd_i   = (vaddr >> 21) & 0x1FFu;
    const uint64_t pt_i   = (vaddr >> 12) & 0x1FFu;

    uint64_t* pml4 = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(cr3 & ~0xFFFull));
    uint64_t e = pml4[pml4_i];
    if ((e & PAGE_PRESENT) == 0) return -1;
    pml4[pml4_i] = e | PAGE_USER | PAGE_WRITABLE;

    uint64_t* pdpt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pdpt[pdpt_i];
    if ((e & PAGE_PRESENT) == 0) return -1;
    pdpt[pdpt_i] = e | PAGE_USER | PAGE_WRITABLE;
    if (e & PAGE_HUGE) {
        invlpg_addr(vaddr);
        return 0;
    }

    uint64_t* pd = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pd[pd_i];
    if ((e & PAGE_PRESENT) == 0) return -1;
    pd[pd_i] = e | PAGE_USER | PAGE_WRITABLE;
    if (e & PAGE_HUGE) {
        invlpg_addr(vaddr);
        return 0;
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pt[pt_i];
    if ((e & PAGE_PRESENT) == 0) return -1;
    pt[pt_i] = e | PAGE_USER | PAGE_WRITABLE;
    invlpg_addr(vaddr);
    return 0;
}

static int map_user_page(uint64_t vaddr) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    const uint64_t pml4_i = (vaddr >> 39) & 0x1FFu;
    const uint64_t pdpt_i = (vaddr >> 30) & 0x1FFu;
    const uint64_t pd_i   = (vaddr >> 21) & 0x1FFu;
    const uint64_t pt_i   = (vaddr >> 12) & 0x1FFu;

    uint64_t* pml4 = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(cr3 & ~0xFFFull));
    uint64_t e = pml4[pml4_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pdpt = pmm_alloc_page();
        if (!new_pdpt) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_pdpt), 0, PAGE_SIZE);
        pml4[pml4_i] = ((uint64_t)(uintptr_t)new_pdpt & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        e = pml4[pml4_i];
    } else {
        pml4[pml4_i] = e | PAGE_USER | PAGE_WRITABLE;
    }

    uint64_t* pdpt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pdpt[pdpt_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pd = pmm_alloc_page();
        if (!new_pd) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_pd), 0, PAGE_SIZE);
        pdpt[pdpt_i] = ((uint64_t)(uintptr_t)new_pd & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        e = pdpt[pdpt_i];
    } else {
        pdpt[pdpt_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    if (e & PAGE_HUGE) {
        invlpg_addr(vaddr);
        return 0;
    }

    uint64_t* pd = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pd[pd_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_pt = pmm_alloc_page();
        if (!new_pt) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_pt), 0, PAGE_SIZE);
        pd[pd_i] = ((uint64_t)(uintptr_t)new_pt & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        e = pd[pd_i];
    } else {
        pd[pd_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    if (e & PAGE_HUGE) {
        invlpg_addr(vaddr);
        return 0;
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
    e = pt[pt_i];
    if ((e & PAGE_PRESENT) == 0) {
        void* new_page = pmm_alloc_page();
        if (!new_page) return -1;
        memset((void*)(uintptr_t)phys_to_virt((uintptr_t)new_page), 0, PAGE_SIZE);
        pt[pt_i] = ((uint64_t)(uintptr_t)new_page & ~0xFFFull) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    } else {
        pt[pt_i] = e | PAGE_USER | PAGE_WRITABLE;
    }
    invlpg_addr(vaddr);
    return 0;
}

static int map_user_range(uint64_t start, uint64_t end) {
    if (end <= start) return -1;
    for (uint64_t v = start; v < end; v += 0x1000ull) {
        if (map_user_page(v) != 0) return -1;
    }
    return 0;
}

static int mark_user_range_mapped(uint64_t start, uint64_t end) {
    if (end <= start) return -1;
    for (uint64_t v = start; v < end; v += 0x1000ull) {
        if (mark_user_page_mapped(v) != 0) return -1;
    }
    return 0;
}

static int table_has_present(uint64_t* table) {
    for (int i = 0; i < 512; ++i) {
        if (table[i] & PAGE_PRESENT) return 1;
    }
    return 0;
}

void module_loader_free_user_space(uint64_t pml4_phys, uint64_t start, uint64_t end) {
    if (!pml4_phys || end <= start) return;
    start = align_down_u64(start, 0x1000ull);
    end = align_up_u64(end, 0x1000ull);

    uint64_t* pml4 = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)pml4_phys);

    for (uint64_t v = start; v < end; v += 0x1000ull) {
        const uint64_t pml4_i = (v >> 39) & 0x1FFu;
        const uint64_t pdpt_i = (v >> 30) & 0x1FFu;
        const uint64_t pd_i   = (v >> 21) & 0x1FFu;
        const uint64_t pt_i   = (v >> 12) & 0x1FFu;

        uint64_t e = pml4[pml4_i];
        if ((e & PAGE_PRESENT) == 0) continue;
        uint64_t* pdpt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
        e = pdpt[pdpt_i];
        if ((e & PAGE_PRESENT) == 0 || (e & PAGE_HUGE)) continue;
        uint64_t* pd = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
        e = pd[pd_i];
        if ((e & PAGE_PRESENT) == 0 || (e & PAGE_HUGE)) continue;
        uint64_t* pt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
        e = pt[pt_i];
        if ((e & PAGE_PRESENT) == 0) continue;
        if (e & PAGE_USER) {
            pmm_free_page((void*)(uintptr_t)(e & ~0xFFFull));
        }
        pt[pt_i] = 0;
    }

    uint64_t pml4_start = (start >> 39) & 0x1FFu;
    uint64_t pml4_end = ((end - 1) >> 39) & 0x1FFu;
    for (uint64_t pml4_i = pml4_start; pml4_i <= pml4_end; ++pml4_i) {
        uint64_t e = pml4[pml4_i];
        if ((e & PAGE_PRESENT) == 0) continue;
        uint64_t* pdpt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e & ~0xFFFull));
        int pdpt_empty = 1;
        for (int pdpt_i = 0; pdpt_i < 512; ++pdpt_i) {
            uint64_t e2 = pdpt[pdpt_i];
            if ((e2 & PAGE_PRESENT) == 0) continue;
            if (e2 & PAGE_HUGE) { pdpt_empty = 0; continue; }
            uint64_t* pd = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e2 & ~0xFFFull));
            int pd_empty = 1;
            for (int pd_i = 0; pd_i < 512; ++pd_i) {
                uint64_t e3 = pd[pd_i];
                if ((e3 & PAGE_PRESENT) == 0) continue;
                if (e3 & PAGE_HUGE) { pd_empty = 0; continue; }
                uint64_t* pt = (uint64_t*)(uintptr_t)phys_to_virt((uintptr_t)(e3 & ~0xFFFull));
                if (!table_has_present(pt)) {
                    pmm_free_page((void*)(uintptr_t)(e3 & ~0xFFFull));
                    pd[pd_i] = 0;
                } else {
                    pd_empty = 0;
                }
            }
            if (pd_empty && !table_has_present(pd)) {
                pmm_free_page((void*)(uintptr_t)(e2 & ~0xFFFull));
                pdpt[pdpt_i] = 0;
            } else {
                pdpt_empty = 0;
            }
        }
        if (pdpt_empty && !table_has_present(pdpt)) {
            pmm_free_page((void*)(uintptr_t)(e & ~0xFFFull));
            pml4[pml4_i] = 0;
        }
    }

    pmm_free_page((void*)(uintptr_t)pml4_phys);
}

static int prepare_ring3_image(const void* elf_image, size_t elf_size, uint64_t* out_stack_top) {
    uint64_t user_start = 0;
    uint64_t user_end = 0;
    if (!out_stack_top) return -1;
    if (extract_elf_load_range_raw(elf_image, elf_size, &user_start, &user_end) != 0) return -1;
    uint64_t extra = 0x1000ull;
    extra += (uint64_t)NTUX_USER_STACK_PAGES * 0x1000ull;
    extra += (uint64_t)NTUX_USER_HEAP_EXTRA_BYTES;
    if (user_end <= UINT64_MAX - extra) {
        user_end += extra;
    }
    if (mark_user_range_mapped(user_start, user_end) != 0) return -1;
    if (user_end < user_start + 0x1000ull + 0x10ull) return -1;
    /* SysV ABI: RSP%16==8 on entry (as if return address was pushed). */
    *out_stack_top = (user_end & ~0xFull) - 0x8ull;
    return 0;
}

static __attribute__((unused)) uint64_t read_rsp(void) {
    uint64_t rsp = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static __attribute__((unused)) void prepare_user_entry_stack(void) {
    gdt_set_kernel_stack(read_rsp());
}

static __attribute__((unused)) void run_entry_in_kernel_context(uint64_t entry) {
    void (*fn)(void) = (void (*)(void))(uintptr_t)entry;
    fn();
    thread_exit_current();
}

static bool contains_token(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return false;

    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;

    for (size_t i = 0; i + nlen <= hlen; ++i) {
        if (strncmp(haystack + i, needle, nlen) == 0) return true;
    }
    return false;
}

static void copy_token_from_string(char out[64], const char *src) {
    size_t i = 0;
    if (!out) return;
    out[0] = '\0';
    if (!src) return;
    while (*src == ' ' || *src == '\t') src++;
    while (*src && *src != ' ' && *src != '\t' && i + 1 < 64) {
        out[i++] = *src++;
    }
    out[i] = '\0';
}

static void copy_basename_token(char out[64], const char *path) {
    const char *base = path;
    size_t len = 0;
    if (!out) return;
    out[0] = '\0';
    if (!path) return;
    for (const char *p = path; *p; ++p) {
        if (*p == '/') base = p + 1;
    }
    len = strlen(base);
    if (len >= 4 && strcmp(base + len - 4, ".elf") == 0) {
        len -= 4;
    }
    if (len >= 64) len = 63;
    memcpy(out, base, len);
    out[len] = '\0';
}

static void copy_path_field(char out[160], const char *path) {
    size_t len = 0;
    if (!out) return;
    out[0] = '\0';
    if (!path) return;
    len = strlen(path);
    if (len >= 160) len = 159;
    memcpy(out, path, len);
    out[len] = '\0';
}

static void host_print(const char *text) {
    if (!text) return;
    kprint(text);
}

static struct limine_file *find_module_hello(void) {
    if (!g_module_response || !g_module_response->modules) return NULL;
    return find_module_by_token("hello");
}

static struct limine_file *find_module_by_token(const char *token) {
    if (!g_module_response || !g_module_response->modules) return NULL;
    if (!token || !token[0]) return NULL;

    for (uint64_t i = 0; i < g_module_response->module_count; ++i) {
        struct limine_file *f = g_module_response->modules[i];
        if (!f) continue;

        if (f->string && contains_token(f->string, token)) {
            return f;
        }
        if (f->path && contains_token(f->path, token)) {
            return f;
        }
    }
    return NULL;
}

static void hello_launch_thread(void) {
    const void *hello_addr = g_hello_module_addr;
    uint64_t hello_size = g_hello_module_size;
    hello_launch_mode_t mode = g_hello_launch_mode;

    if (!hello_addr || hello_size == 0) {
        kprint("[MOD] hello module unavailable for launch\n");
        __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_MISSING_IMAGE, __ATOMIC_RELEASE);
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        return;
    }

    if (mode == HELLO_LAUNCH_RING3) {
        void* image = kmalloc((size_t)hello_size);
        int range_slot = -1;
        if (!image) {
            kprint("[MOD] ring3 alloc failed\n");
            __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_OOM, __ATOMIC_RELEASE);
            __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
            return;
        }
        memcpy(image, hello_addr, (size_t)hello_size);

        uint64_t entry = 0;
        uint64_t user_stack_top = 0;
        uint64_t user_start = 0;
        uint64_t user_end = 0;
        uint64_t user_cr3 = 0;
        if (rebase_elf_image_inplace(image, (size_t)hello_size, &user_start, &user_end, &range_slot) != 0) {
            kprint("[MOD] ring3 no free user VA range\n");
            __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_NO_USER_RANGE, __ATOMIC_RELEASE);
            kfree(image);
            __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
            return;
        }
        user_cr3 = clone_kernel_pml4();
        if (!user_cr3) {
            kprint("[MOD] ring3 pml4 alloc failed\n");
            __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_OOM, __ATOMIC_RELEASE);
            release_user_range_slot(range_slot);
            kfree(image);
            __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
            return;
        }
        if (map_user_range_pml4(user_cr3, user_start, user_end) != 0) {
            kprint("[MOD] ring3 user map alloc failed\n");
            __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_USER_MAP, __ATOMIC_RELEASE);
            release_user_range_slot(range_slot);
            kfree(image);
            __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
            return;
        }
        uint64_t saved_cr3 = read_cr3();
        write_cr3(user_cr3);
        if (load_elf_image(image, (size_t)hello_size, &entry) != 0) {
            kprint("[MOD] ring3 ELF load failed\n");
            __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_ELF_LOAD, __ATOMIC_RELEASE);
            release_user_range_slot(range_slot);
            kfree(image);
            __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
            write_cr3(saved_cr3);
            return;
        }
        if (compute_user_stack_top(image, (size_t)hello_size, &user_stack_top) != 0) {
            kprint("[MOD] ring3 user mapping failed\n");
            __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_USER_MAP, __ATOMIC_RELEASE);
            release_user_range_slot(range_slot);
            kfree(image);
            __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
            write_cr3(saved_cr3);
            return;
        }
        kprint("[MOD] user range ");
        kprint_hex64(user_start);
        kprint("..");
        kprint_hex64(user_end);
        kprint(" stack=");
        kprint_hex64(user_stack_top);
        kprint(" entry=");
        kprint_hex64(entry);
        kprint("\n");
        write_cr3(saved_cr3);
        set_current_thread_user_range(user_start, user_end);
        thread_set_current_cr3(user_cr3);
        release_user_range_slot(range_slot);
        kfree(image);

        __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_OK, __ATOMIC_RELEASE);
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
#if NTUX_USERMODE_DISABLE_RING3
        run_entry_in_kernel_context(entry);
#else
        write_cr3(user_cr3);
        prepare_user_entry_stack();
        enter_user(user_stack_top, entry);
#endif
        return;
    }

    ntux_host_api_t api = { .print = host_print };
    const char *ret = NULL;
    int rc = run_elf_module_with_api(hello_addr, (size_t)hello_size, &api, &ret);
    if (rc != 0) {
        kprint("[MOD] ring0 hello failed\n");
        __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_ERR_ELF_LOAD, __ATOMIC_RELEASE);
    } else if (ret) {
        kprint("[MOD] ");
        kprint(ret);
        kprint("\n");
        __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_OK, __ATOMIC_RELEASE);
    } else {
        kprint("[MOD] ring0 hello finished\n");
        __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_OK, __ATOMIC_RELEASE);
    }

    __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
}

static void elf_launch_thread(void) {
    void* elf_image = g_elf_launch_image;
    size_t elf_size = g_elf_launch_size;
    g_elf_launch_image = NULL;
    g_elf_launch_size = 0;

    if (!elf_image || elf_size == 0) {
        kprint("[MOD] elf launch image missing\n");
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_MISSING_IMAGE, __ATOMIC_RELEASE);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        return;
    }

    uint64_t entry = 0;
    uint64_t user_stack_top = 0;
    uint64_t user_start = 0;
    uint64_t user_end = 0;
    uint64_t user_cr3 = 0;
    int range_slot = -1;
    if (rebase_elf_image_inplace(elf_image, elf_size, &user_start, &user_end, &range_slot) != 0) {
        kprint("[MOD] no free user VA range\n");
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_NO_USER_RANGE, __ATOMIC_RELEASE);
        kfree(elf_image);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        return;
    }
    user_cr3 = clone_kernel_pml4();
    if (!user_cr3) {
        kprint("[MOD] elf pml4 alloc failed\n");
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_OOM, __ATOMIC_RELEASE);
        release_user_range_slot(range_slot);
        kfree(elf_image);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        return;
    }
    if (map_user_range_pml4(user_cr3, user_start, user_end) != 0) {
        kprint("[MOD] elf user map alloc failed\n");
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_USER_MAP, __ATOMIC_RELEASE);
        release_user_range_slot(range_slot);
        kfree(elf_image);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        return;
    }
    uint64_t saved_cr3 = read_cr3();
    write_cr3(user_cr3);
    if (load_elf_image(elf_image, elf_size, &entry) != 0) {
        kprint("[MOD] elf load failed\n");
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_ELF_LOAD, __ATOMIC_RELEASE);
        release_user_range_slot(range_slot);
        kfree(elf_image);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        write_cr3(saved_cr3);
        return;
    }
    if (compute_user_stack_top(elf_image, elf_size, &user_stack_top) != 0) {
        kprint("[MOD] elf user mapping failed\n");
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_USER_MAP, __ATOMIC_RELEASE);
        release_user_range_slot(range_slot);
        kfree(elf_image);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        write_cr3(saved_cr3);
        return;
    }
    kprint("[MOD] user range ");
    kprint_hex64(user_start);
    kprint("..");
    kprint_hex64(user_end);
    kprint(" stack=");
    kprint_hex64(user_stack_top);
    kprint(" entry=");
    kprint_hex64(entry);
    kprint("\n");
    write_cr3(saved_cr3);
    set_current_thread_user_range(user_start, user_end);
    thread_set_current_cr3(user_cr3);
    release_user_range_slot(range_slot);

    kfree(elf_image);
    __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_OK, __ATOMIC_RELEASE);
    __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
#if NTUX_USERMODE_DISABLE_RING3
    run_entry_in_kernel_context(entry);
#else
    write_cr3(user_cr3);
    prepare_user_entry_stack();
    enter_user(user_stack_top, entry);
#endif
}

void module_loader_init(void) {
    g_module_response = NULL;
    g_modules_ready = false;

    if (!g_module_request.response) {
        kprint("[MOD] No module response from Limine\n");
        return;
    }

    g_module_response = g_module_request.response;
    g_modules_ready = true;
    g_hello_module_addr = NULL;
    g_hello_module_size = 0;
    for (uint32_t i = 0; i < NTUX_USER_RESV_MAX; ++i) {
        g_user_resv[i].start = 0;
        g_user_resv[i].end = 0;
        g_user_resv[i].in_use = 0;
    }

    kprint("[MOD] Module count: ");
    kprint_uint((uint32_t)g_module_response->module_count);
    kprint("\n");

    struct limine_file *hello = find_module_hello();
    if (hello && hello->address && hello->size > 0) {
        g_hello_module_addr = (const void *)hello->address;
        g_hello_module_size = hello->size;
        kprint("[MOD] hello module is ready\n");
    }
}

bool module_loader_run_hello(const char **result_text) {
    if (result_text) *result_text = NULL;
    if (!g_modules_ready) {
        if (result_text) *result_text = "Module system not initialized";
        return false;
    }

    if (__atomic_test_and_set(&g_hello_launch_busy, __ATOMIC_ACQUIRE)) {
        if (result_text) *result_text = "hello launch already running";
        return false;
    }

    struct limine_file *hello = find_module_hello();
    if (!hello || !hello->address || hello->size == 0) {
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "hello module not found";
        return false;
    }

    g_hello_module_addr = (const void *)hello->address;
    g_hello_module_size = hello->size;
    g_hello_launch_mode = HELLO_LAUNCH_RING0;
    __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_PENDING, __ATOMIC_RELEASE);
    int tid = thread_create(hello_launch_thread);
    if (tid < 0) {
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "failed to create hello task";
        return false;
    }
    console_input_force_owner(tid);

    int st = wait_launch_result(&g_hello_launch_result, 300);
    if (result_text) *result_text = launch_result_text(st);
    return (st == NTUX_LAUNCH_OK);
}

bool module_loader_start_hello_ring3(const char **result_text) {
    if (result_text) *result_text = NULL;
    if (!g_modules_ready) {
        if (result_text) *result_text = "Module system not initialized";
        return false;
    }

    if (__atomic_test_and_set(&g_hello_launch_busy, __ATOMIC_ACQUIRE)) {
        if (result_text) *result_text = "hello launch already running";
        return false;
    }

    struct limine_file *hello = find_module_hello();
    if (!hello || !hello->address || hello->size == 0) {
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "hello module not found";
        return false;
    }

    g_hello_module_addr = (const void *)hello->address;
    g_hello_module_size = hello->size;
    g_hello_launch_mode = HELLO_LAUNCH_RING3;
    __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_PENDING, __ATOMIC_RELEASE);

    int tid = thread_create(hello_launch_thread);
    if (tid < 0) {
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "failed to create ring3 hello task";
        return false;
    }
    (void)thread_set_name(tid, "hello");
    __atomic_store_n(&g_hello_launch_tid, tid, __ATOMIC_RELEASE);
    console_input_force_owner(tid);

    int st = wait_launch_result(&g_hello_launch_result, 300);
    if (result_text) *result_text = launch_result_text(st);
    return (st == NTUX_LAUNCH_OK);
}

bool module_loader_start_module_ring3(const char* token, const char **result_text) {
    if (result_text) *result_text = NULL;
    if (!g_modules_ready) {
        if (result_text) *result_text = "Module system not initialized";
        return false;
    }
    if (!token || !token[0]) {
        if (result_text) *result_text = "invalid module token";
        return false;
    }

    if (__atomic_test_and_set(&g_hello_launch_busy, __ATOMIC_ACQUIRE)) {
        if (result_text) *result_text = "module launch already running";
        return false;
    }

    struct limine_file *mod = find_module_by_token(token);
    if (!mod || !mod->address || mod->size == 0) {
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "module not found";
        return false;
    }

    g_hello_module_addr = (const void *)mod->address;
    g_hello_module_size = mod->size;
    g_hello_launch_mode = HELLO_LAUNCH_RING3;
    __atomic_store_n(&g_hello_launch_result, NTUX_LAUNCH_PENDING, __ATOMIC_RELEASE);
    int tid = thread_create(hello_launch_thread);
    if (tid < 0) {
        __atomic_store_n(&g_hello_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "failed to create module task";
        return false;
    }
    (void)thread_set_name(tid, token);
    __atomic_store_n(&g_hello_launch_tid, tid, __ATOMIC_RELEASE);
    console_input_force_owner(tid);

    int st = wait_launch_result(&g_hello_launch_result, 300);
    if (result_text) *result_text = launch_result_text(st);
    return (st == NTUX_LAUNCH_OK);
}

bool module_loader_start_elf_ring3(const char* path, const char **result_text) {
    if (result_text) *result_text = NULL;
    if (!path || !path[0]) {
        if (result_text) *result_text = "invalid path";
        return false;
    }

    if (__atomic_test_and_set(&g_elf_launch_busy, __ATOMIC_ACQUIRE)) {
        if (result_text) *result_text = "elf launch already running";
        return false;
    }
    __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_PENDING, __ATOMIC_RELEASE);

    size_t file_len = 0;
    if (fs_read_file(path, NULL, 0, &file_len) != 0 || file_len == 0 || file_len > (16u * 1024u * 1024u)) {
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "failed to read elf size";
        return false;
    }

    void* image = kmalloc(file_len);
    if (!image) {
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_OOM, __ATOMIC_RELEASE);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "out of memory";
        return false;
    }

    size_t got = 0;
    if (fs_read_file(path, image, file_len, &got) != 0 || got == 0) {
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_MISSING_IMAGE, __ATOMIC_RELEASE);
        kfree(image);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "failed to read elf";
        return false;
    }

    g_elf_launch_image = image;
    g_elf_launch_size = got;
    int tid = thread_create(elf_launch_thread);
    if (tid < 0) {
        g_elf_launch_image = NULL;
        g_elf_launch_size = 0;
        kfree(image);
        __atomic_store_n(&g_elf_launch_result, NTUX_LAUNCH_ERR_MISSING_IMAGE, __ATOMIC_RELEASE);
        __atomic_store_n(&g_elf_launch_busy, 0, __ATOMIC_RELEASE);
        if (result_text) *result_text = "failed to create elf task";
        return false;
    }
    {
        const char* base = strrchr(path, '/');
        const char* name = (base && base[1]) ? (base + 1) : path;
        (void)thread_set_name(tid, name && name[0] ? name : "elf");
    }
    __atomic_store_n(&g_elf_launch_tid, tid, __ATOMIC_RELEASE);
    (void)path;
    console_input_force_owner(tid);

    int st = wait_launch_result(&g_elf_launch_result, 300);
    if (result_text) *result_text = launch_result_text(st);
    return (st == NTUX_LAUNCH_OK);
}

int module_loader_list(ntux_module_info_t* out, uint64_t max_entries, uint64_t* out_count) {
    if (!g_modules_ready || !g_module_response || !g_module_response->modules) {
        if (out_count) *out_count = 0;
        return -1;
    }
    uint64_t total = g_module_response->module_count;
    if (out_count) *out_count = total;
    if (!out || max_entries == 0) return 0;

    uint64_t limit = (total < max_entries) ? total : max_entries;
    for (uint64_t i = 0; i < limit; ++i) {
        struct limine_file *f = g_module_response->modules[i];
        out[i].token[0] = '\0';
        out[i].path[0] = '\0';
        if (!f) continue;
        if (f->string && f->string[0]) {
            copy_token_from_string(out[i].token, f->string);
        }
        if (!out[i].token[0]) {
            copy_basename_token(out[i].token, f->path);
        }
        copy_path_field(out[i].path, f->path);
    }
    return 0;
}

int module_loader_last_hello_tid(void) {
    return last_hello_launch_tid();
}

int module_loader_last_elf_tid(void) {
    return last_elf_launch_tid();
}
