#include "apic.h"
#include <mm/hhdm.h>
#include <arch/x86_64/io.h>
#include <drivers/framebuffer/kprint.h>
#include <interrupt/pic.h>
#include <interrupt/timer.h>
#include <lib/string.h>
#include <drivers/acpi/acpi.h>



static uintptr_t local_apic_base = (uintptr_t)APIC_BASE_DEFAULT;
static uintptr_t io_apic_base = (uintptr_t)IO_APIC_ADDRESS;
static bool apic_enabled = false;
static bool ioapic_enabled = false;
static uint32_t io_apic_gsi_base = 0;
static uint8_t io_apic_max_irq = 0;
static uint8_t g_irq_polarity[16];
static uint8_t g_irq_level[16];

#ifndef NTUX_FORCE_PIC
#define NTUX_FORCE_PIC 1
#endif

static uint64_t apic_read_msr(uint32_t msr);
static void apic_write_msr(uint32_t msr, uint64_t value);

static void apic_force_disable(void) {
    uint64_t apic_base = apic_read_msr(APIC_BASE_MSR);
    if (apic_base & 0x800) {
        apic_base &= ~0x800;
        apic_write_msr(APIC_BASE_MSR, apic_base);
    }
    apic_enabled = false;
}

static void ioapic_mask_all(void) {
    if (io_apic_base == 0) return;
    uint32_t version = ioapic_read(IO_APIC_VERSION);
    if (version == 0 || (version & 0xFF) == 0xFF) return;
    uint8_t max_irq = (version >> 16) & 0xFF;
    for (uint8_t i = 0; i <= max_irq; ++i) {
        uint32_t low = ioapic_read(IO_APIC_REDIR_TABLE_START + i * 2);
        low |= 0x10000; /* mask */
        ioapic_write(IO_APIC_REDIR_TABLE_START + i * 2, low);
    }
}
static bool apic_irq_routing_enabled = false;
static uint32_t g_irq_to_gsi[16];

static inline uintptr_t phys_to_virt(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    return off ? (uintptr_t)(phys + (uintptr_t)off) : phys;
}

static bool shutdown_pending = false;
static int shutdown_seconds = 0;
static bool shutdown_cancelled = false;
static bool apic_bases_detected = false;

static void apic_detect_bases_from_madt(const struct acpi_sdt_header* madt);

bool get_shutdown_pending(void) {
    return shutdown_pending;
}


int get_shutdown_seconds(void) {
    return shutdown_seconds;
}


bool is_shutdown_cancelled(void) {
    return shutdown_cancelled;
}


void cancel_shutdown(void) {
    shutdown_pending = false;
    shutdown_seconds = 0;
    shutdown_cancelled = true;
    kprint("[SHUTDOWN] Shutdown cancelled\n");
}


void initiate_shutdown(int seconds) {
    shutdown_pending = true;
    shutdown_seconds = seconds;
    shutdown_cancelled = false;
    kprint("[SHUTDOWN] System will shut down in ");
    kprint_int(seconds);
    kprint(" seconds\n");
}




static uint64_t apic_read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}


static void apic_write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)), "c"(msr));
}




uint32_t apic_read(uint32_t offset) {
    if (!apic_enabled) return 0;
    return *((volatile uint32_t*)(phys_to_virt(local_apic_base) + (uintptr_t)offset));
}


void apic_write(uint32_t offset, uint32_t value) {
    if (!apic_enabled) return;
    *((volatile uint32_t*)(phys_to_virt(local_apic_base) + (uintptr_t)offset)) = value;
    volatile uint32_t dummy = *((volatile uint32_t*)(phys_to_virt(local_apic_base) + (uintptr_t)offset));
    (void)dummy;
}


uint32_t apic_get_id(void) {
    return (apic_read(APIC_ID) >> 24) & 0xFF;
}


uint32_t apic_get_version(void) {
    return apic_read(APIC_VERSION) & 0xFF;
}

bool apic_is_enabled(void) {
    return apic_enabled;
}

bool apic_uses_ioapic(void) {
    return apic_irq_routing_enabled;
}


void apic_enable(void) {
    if (apic_enabled) return;
    
    
    uint64_t apic_base = apic_read_msr(APIC_BASE_MSR);
    local_apic_base = (uintptr_t)(apic_base & 0xFFFFF000ULL);
    apic_base |= 0x800;  
    apic_write_msr(APIC_BASE_MSR, apic_base);
    
    
    apic_base = apic_read_msr(APIC_BASE_MSR);
    if (apic_base & 0x800) {
        apic_enabled = true;
    }
}


void apic_disable(void) {
    if (!apic_enabled) return;
    
    
    uint64_t apic_base = apic_read_msr(APIC_BASE_MSR);
    apic_base &= ~0x800;  
    apic_write_msr(APIC_BASE_MSR, apic_base);
    
    apic_enabled = false;
}


void apic_send_eoi(void) {
    if (!apic_enabled) return;
    apic_write(APIC_EOI, 0);
}


void apic_set_spurious_vector(uint8_t vector) {
    if (!apic_enabled) return;
    uint32_t val = apic_read(APIC_SPURIOUS_VECTOR);
    val = (val & 0xFFFFFF00) | vector | APIC_SPURIOUS_ENABLE;
    apic_write(APIC_SPURIOUS_VECTOR, val);
}


void apic_timer_init(uint32_t divisor, uint32_t count) {
    if (!apic_enabled) return;
    
    
    apic_write(APIC_TIMER_DIVIDE, divisor & 0x0B);
    
    
    apic_write(APIC_TIMER_INIT_COUNT, count);
    
    
    uint32_t timer_vec = 0x20 | APIC_TIMER_PERIODIC;  
    apic_write(APIC_LVT_TIMER, timer_vec);
}


void apic_timer_stop(void) {
    if (!apic_enabled) return;
    apic_write(APIC_LVT_TIMER, 0x10000);  
}




uint32_t ioapic_read(uint32_t offset) {
    if (io_apic_base == 0) return 0;
    volatile uint32_t* const ioregsel = (volatile uint32_t*)phys_to_virt(io_apic_base);
    volatile uint32_t* const iowin = (volatile uint32_t*)(phys_to_virt(io_apic_base) + 0x10);
    *ioregsel = offset;
    return *iowin;
}


void ioapic_write(uint32_t offset, uint32_t value) {
    if (io_apic_base == 0) return;
    volatile uint32_t* const ioregsel = (volatile uint32_t*)phys_to_virt(io_apic_base);
    volatile uint32_t* const iowin = (volatile uint32_t*)(phys_to_virt(io_apic_base) + 0x10);
    *ioregsel = offset;
    *iowin = value;
}


static bool ioapic_gsi_to_index(uint32_t gsi, uint8_t* out_index) {
    if (!ioapic_enabled) return false;
    if (gsi < io_apic_gsi_base) return false;
    uint32_t idx = gsi - io_apic_gsi_base;
    if (idx > io_apic_max_irq) return false;
    if (out_index) *out_index = (uint8_t)idx;
    return true;
}

void ioapic_set_irq(uint32_t gsi, uint8_t vector, uint8_t polarity, bool level) {
    if (!ioapic_enabled) return;
    uint8_t idx = 0;
    if (!ioapic_gsi_to_index(gsi, &idx)) return;

    uint32_t low = vector | (polarity << 13) | (level ? 0x8000 : 0);
    uint32_t high = apic_get_id() << 24;

    ioapic_write(IO_APIC_REDIR_TABLE_START + idx * 2, low);
    ioapic_write(IO_APIC_REDIR_TABLE_START + idx * 2 + 1, high);
}


void ioapic_enable_irq(uint32_t gsi) {
    if (!ioapic_enabled) return;
    uint8_t idx = 0;
    if (!ioapic_gsi_to_index(gsi, &idx)) return;

    uint32_t low = ioapic_read(IO_APIC_REDIR_TABLE_START + idx * 2);
    low &= ~0x10000;  
    ioapic_write(IO_APIC_REDIR_TABLE_START + idx * 2, low);
}


void ioapic_disable_irq(uint32_t gsi) {
    if (!ioapic_enabled) return;
    uint8_t idx = 0;
    if (!ioapic_gsi_to_index(gsi, &idx)) return;

    uint32_t low = ioapic_read(IO_APIC_REDIR_TABLE_START + idx * 2);
    low |= 0x10000;  
    ioapic_write(IO_APIC_REDIR_TABLE_START + idx * 2, low);
}




static bool apic_init_local(void) {
    kprint("[APIC] Initializing Local APIC...\n");
    
    
    uint64_t apic_base = apic_read_msr(APIC_BASE_MSR);
    local_apic_base = (uintptr_t)(apic_base & 0xFFFFF000ULL);
    
    kprint("[APIC] Local APIC base: 0x");
    kprinthex((uint32_t)local_apic_base);
    kprint("\n");
    
    apic_enable();
    
    uint32_t version = apic_get_version();
    
    if (version == 0 || version == 0xFF) {
        kprint("[APIC] ERROR: No Local APIC found\n");
        return false;
    }
    
    kprint("[APIC] Local APIC version: 0x");
    kprinthex(version);
    kprint("\n");
    
    
    apic_set_spurious_vector(APIC_SPURIOUS_DEFAULT_VECTOR);
    apic_write(APIC_TASK_PRIORITY, 0);
    apic_write(APIC_LVT_INT0, 1u << 16);
    apic_write(APIC_LVT_INT1, 1u << 16);
    
    
    apic_write(APIC_ERROR_STATUS, 0);
    
    kprint("[APIC] Local APIC initialized successfully\n");
    return true;
}


static bool apic_init_io(void) {
    kprint("[APIC] Initializing I/O APIC...\n");
    
    
    uint32_t version = ioapic_read(IO_APIC_VERSION);
    if (version == 0 || (version & 0xFF) == 0xFF) {
        kprint("[APIC] ERROR: No I/O APIC found\n");
        return false;
    }
    
    uint8_t max_irq = (version >> 16) & 0xFF;
    io_apic_max_irq = max_irq;
    kprint("[APIC] I/O APIC version: 0x");
    kprinthex(version & 0xFF);
    kprint(", max IRQs: ");
    kprint_uint(max_irq);
    kprint("\n");
    
    ioapic_enabled = true;
    
    
    for (int i = 0; i <= max_irq; i++) {
        ioapic_disable_irq(io_apic_gsi_base + (uint32_t)i);
    }
    
    kprint("[APIC] I/O APIC initialized successfully\n");
    return true;
}


bool apic_init(void) {
    if (NTUX_FORCE_PIC) {
        if (!apic_bases_detected) {
            if (
                acpi_init()) {
                const struct acpi_sdt_header* madt = acpi_find_table("APIC");
                apic_detect_bases_from_madt(madt);
            }
        }
        ioapic_mask_all();
        apic_force_disable();
        apic_irq_routing_enabled = false;
        kprint("[APIC] Forced PIC mode (IOAPIC disabled)\n");
        return false;
    }
    kprint("[APIC] Starting APIC initialization...\n");
    if (!apic_bases_detected) {
        if (acpi_init()) {
            const struct acpi_sdt_header* madt = acpi_find_table("APIC");
            apic_detect_bases_from_madt(madt);
        }
    }
    
    
    if (!apic_init_local()) {
        kprint("[APIC] WARNING: Local APIC initialization failed, using PIC instead\n");
        return false;
    }
    
    
    if (!apic_init_io()) {
        kprint("[APIC] WARNING: I/O APIC initialization failed\n");
        apic_irq_routing_enabled = false;
        return false;
    }

    ioapic_set_irq(g_irq_to_gsi[0], 32, g_irq_polarity[0], g_irq_level[0] != 0);
    ioapic_enable_irq(g_irq_to_gsi[0]);
    ioapic_set_irq(g_irq_to_gsi[1], 33, g_irq_polarity[1], g_irq_level[1] != 0);
    ioapic_enable_irq(g_irq_to_gsi[1]);
    ioapic_set_irq(g_irq_to_gsi[12], 44, g_irq_polarity[12], g_irq_level[12] != 0);
    ioapic_enable_irq(g_irq_to_gsi[12]);

    for (uint8_t irq = 0; irq < 16; ++irq) {
        pic_set_mask(irq);
    }

    /* Verify that the IOAPIC routing actually delivers timer IRQs. */
    uint64_t tick_start = get_tick_count();
    for (uint64_t i = 0; i < 2000000ull; ++i) {
        __asm__ volatile("pause");
        if (get_tick_count() != tick_start) break;
    }

    if (get_tick_count() == tick_start) {
        kprint("[APIC] WARNING: IOAPIC timer IRQ not firing, falling back to PIC\n");
        ioapic_disable_irq(g_irq_to_gsi[0]);
        ioapic_disable_irq(g_irq_to_gsi[1]);
        ioapic_disable_irq(g_irq_to_gsi[12]);
        for (uint8_t irq = 0; irq < 16; ++irq) {
            pic_clear_mask(irq);
        }
        apic_irq_routing_enabled = false;
        return false;
    }

    apic_irq_routing_enabled = true;
    kprint("[APIC] APIC initialized successfully\n");
    return true;
}





struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_ioapic {
    struct acpi_madt_entry_header h;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_lapic_override {
    struct acpi_madt_entry_header h;
    uint16_t reserved;
    uint64_t lapic_addr;
} __attribute__((packed));

struct acpi_madt_iso {
    struct acpi_madt_entry_header h;
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

static void apic_detect_bases_from_madt(const struct acpi_sdt_header* madt) {
    if (!madt) return;
    if (memcmp(madt->signature, "APIC", 4) != 0) return;
    if (madt->length < sizeof(struct acpi_madt)) return;

    const struct acpi_madt* m = (const struct acpi_madt*)madt;
    if (m->lapic_address != 0) {
        local_apic_base = (uintptr_t)m->lapic_address;
    }

    const uint8_t* cur = (const uint8_t*)m + sizeof(struct acpi_madt);
    const uint8_t* end = (const uint8_t*)m + m->header.length;
    for (int i = 0; i < 16; ++i) {
        g_irq_to_gsi[i] = (uint32_t)i;
        g_irq_polarity[i] = 0; /* active high */
        g_irq_level[i] = 0;    /* edge */
    }

    while (cur + sizeof(struct acpi_madt_entry_header) <= end) {
        const struct acpi_madt_entry_header* h = (const struct acpi_madt_entry_header*)cur;
        if (h->length < sizeof(*h) || cur + h->length > end) break;

        if (h->type == 1 && h->length >= sizeof(struct acpi_madt_ioapic)) {
            const struct acpi_madt_ioapic* io = (const struct acpi_madt_ioapic*)cur;
            if (io->ioapic_addr != 0) {
                io_apic_base = (uintptr_t)io->ioapic_addr;
                io_apic_gsi_base = io->gsi_base;
            }
        } else if (h->type == 2 && h->length >= sizeof(struct acpi_madt_iso)) {
            const struct acpi_madt_iso* iso = (const struct acpi_madt_iso*)cur;
            if (iso->bus == 0 && iso->source < 16) {
                g_irq_to_gsi[iso->source] = iso->gsi;
                uint16_t flags = iso->flags;
                uint8_t pol = (uint8_t)(flags & 0x3u);
                uint8_t trg = (uint8_t)((flags >> 2) & 0x3u);
                /* Polarity: 0=conforming,1=high,3=low */
                if (pol == 3u) g_irq_polarity[iso->source] = 1u;
                else g_irq_polarity[iso->source] = 0u;
                /* Trigger: 0=conforming,1=edge,3=level */
                if (trg == 3u) g_irq_level[iso->source] = 1u;
                else g_irq_level[iso->source] = 0u;
            }
        } else if (h->type == 5 && h->length >= sizeof(struct acpi_madt_lapic_override)) {
            const struct acpi_madt_lapic_override* lo = (const struct acpi_madt_lapic_override*)cur;
            if (lo->lapic_addr != 0) local_apic_base = (uintptr_t)lo->lapic_addr;
        }
        cur += h->length;
    }

    apic_bases_detected = true;
    kprint("[APIC] MADT LAPIC base: 0x");
    kprinthex((uint32_t)local_apic_base);
    kprint(" IOAPIC base: 0x");
    kprinthex((uint32_t)io_apic_base);
    kprint(" IRQ->GSI[0]=");
    kprint_uint(g_irq_to_gsi[0]);
    kprint(" [1]=");
    kprint_uint(g_irq_to_gsi[1]);
    kprint(" [12]=");
    kprint_uint(g_irq_to_gsi[12]);
    kprint("\n");
}


static void try_apm_poweroff(void) {
    kprint("[APM] Trying APM power off...\n");
    
    
    outb(APM_CNT_PORT, APM_CNT_GET_INFO);
    uint16_t apm_info = inw(APM_DATA_PORT);
    
    if (apm_info == 0xFFFF || apm_info == 0) {
        kprint("[APM] Not available\n");
        return;
    }
    
    uint8_t apm_version = (apm_info >> 8) & 0xFF;
    uint8_t apm_flags = apm_info & 0xFF;
    
    kprint("[APM] Version: ");
    kprint_uint(apm_version);
    kprint(". Flags: 0x");
    kprinthex(apm_flags);
    kprint("\n");
    
    
    if (apm_flags & 0x02) {
        kprint("[APM] 32-bit interface supported\n");
    }
    
    
    outw(APM_CNT_PORT, 0x0400);  
    inw(APM_DATA_PORT);
    
    
    outw(APM_CNT_PORT, 0x8408);  
    inw(APM_DATA_PORT);
    
    
    outw(APM_CNT_PORT, 0x0401);
    inw(APM_DATA_PORT);
    
    kprint("[APM] APM power off attempted\n");
}

static void try_qemu_poweroff(void) {
    kprint("[SYSTEM] Trying virtual machine poweroff ports...\n");

    
    outw(0x501, 0x2000);

    
    outw(0x604, 0x2000);

    
    outw(0xB004, 0x2000);
}

void system_reboot(void) {
    kprint("[SYSTEM] Rebooting...\n");
    
    
    __asm__ volatile("cli");

    if (acpi_init()) {
        if (acpi_try_reset()) {
            while (1) {
                __asm__ volatile("hlt");
            }
        }
    }
    
    
    outb(0x64, 0xFE);  

    for (volatile int i = 0; i < 200000; ++i) {
        __asm__ volatile("" ::: "memory");
    }

    
    outb(0xCF9, 0x02); 
    outb(0xCF9, 0x06); 

    for (volatile int i = 0; i < 200000; ++i) {
        __asm__ volatile("" ::: "memory");
    }

    
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idtr = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(null_idtr));
    __asm__ volatile("int3");

    while (1) {
        __asm__ volatile("hlt");
    }
}


void system_shutdown(void) {
    kprint("[SYSTEM] Shutting down...\n");
    
    
    __asm__ volatile("cli");
    
    
    outb(0xA1, 0xFF);  
    outb(0x21, 0xFF);  
    
    
    acpi_init();
    acpi_enter_sleep(PM1_SLEEP_TYPE_S5);
    
    
    kprint("[SYSTEM] ACPI shutdown failed, trying APM...\n");
    try_apm_poweroff();
    
    
    try_qemu_poweroff();
    
    
    kprint("[SYSTEM] Shutdown failed, halting...\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}


void system_poweroff(void) {
    kprint("[SYSTEM] Powering off...\n");
    
    
    __asm__ volatile("cli");
    
    
    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);
    
    
    acpi_enter_sleep(PM1_SLEEP_TYPE_S5);
    
    
    kprint("[SYSTEM] ACPI poweroff failed, trying APM...\n");
    try_apm_poweroff();
    
    
    try_qemu_poweroff();
    
    
    kprint("[SYSTEM] Poweroff failed, halting...\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
