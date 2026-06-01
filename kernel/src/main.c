#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <lib/string.h>

#include <limine.h>

#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <interrupt/pic.h>
#include <interrupt/interrupts.h>
#include <interrupt/timer.h>

#include <drivers/framebuffer/fb.h>
#include <drivers/framebuffer/cursor.h>
#include <drivers/framebuffer/kprint.h>
#include <drivers/ps2/keyboard.h>
#include <drivers/ps2/mouse.h>
#include <drivers/audio/audio.h>
#include <drivers/cmos/cmos.h>
#include <interrupt/apic/apic.h>
#include <net/net.h>
#include <drivers/sata/ata.h>
#include <drivers/nvme/nvme.h>
#include <drivers/sdmmc/sdmmc.h>
#include <drivers/gpu/gpu.h>

#include <sched/thread.h>
#include <arch/x86_64/cpu.h>
#include <mm/kmalloc.h>
#include <mm/hhdm.h>
#include <mm/vmm.h>
#include <fs/fs.h>
#include <syscall/syscall.h>

#include <elf/module_loader.h>

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

volatile struct limine_framebuffer* front_buffer;
volatile struct limine_framebuffer* back_buffer;

static struct limine_memmap_response* memmap;
static cursor_t boot_cursor;
static int g_qemu_debug_serial = 1;

static void init_fpu_sse(void) {
    uint64_t cr0 = 0;
    uint64_t cr4 = 0;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    cr0 &= ~(1ull << 2);
    cr0 |=  (1ull << 1);
    cr0 &= ~(1ull << 3);
    cr4 |=  (1ull << 9);
    cr4 |=  (1ull << 10);

    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile("fninit");
}

static void init_fb(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        while (1) {
            __asm__ volatile("hlt");
        }
    }

    front_buffer = framebuffer_request.response->framebuffers[0];
    back_buffer = framebuffer_request.response->framebuffers[0];

    clear_screen_lim(back_buffer, COLOR_BLACK);
    init_cursor(&boot_cursor, (int)back_buffer->width, (int)back_buffer->height);
    init_kprint_global(back_buffer, &boot_cursor, COLOR_WHITE);
    kprint_set_serial_enabled(g_qemu_debug_serial);
    {
        const char* title = "NTux-OS";
        int scale = 4;
        int tw = (int)strlen(title) * 8 * scale;
        int tx = ((int)back_buffer->width - tw) / 2;
        int ty = ((int)back_buffer->height / 2) - 40;
        draw_scaled_text_lim(back_buffer, tx, ty, title, COLOR_WHITE, scale);
        draw_scaled_text_lim(back_buffer, tx + 8, ty + 40, "booting...", 0xFFAAAAAAu, 2);
    }
    kprint_ok("Framebuffer init completed");
}

static void init_interrupts(void) {
    interrupts_disable();
    init_fpu_sse();
    kprint_ok("FPU/SSE init");
    gdt_init();
    kprint_ok("GDT init");

    pic_init();
    kprint_ok("PIC init");

    idt_init();
    kprint_ok("IDT init");

    init_timer();
    kprint_ok("Timer init");

    syscall_init();
    kprint_ok("Syscall init");

    interrupts_enable();
    kprint_ok("Interrupts enabled");
}

static void init_mem(void) {
    memmap = memmap_request.response;
    vmm_init(memmap);
    kmalloc_init();
    kprint_ok("Memory init completed");
    kprint("[MEM] HHDM offset=");
    kprint_hex64(hhdm_offset_get());
    kprint(hhdm_ready() ? " [OK]\n" : " [MISSING]\n");
}

static void init_cpu(void) {
    thread_init();
    kprint_ok("CPU/thread init completed");
}

static void init_storage_and_fs(void) {
    ata_init();
    nvme_init();
    sdmmc_init();
    fs_init();
    gpu_init();

    fs_mkdir("/", "home");
    fs_mkdir("/home", "user");
    fs_create_file("/home/user", "readme.txt", "Welcome to NTux-OS", 18);
    kprint_ok("Storage + FS init completed");
}

static void init_drivers(void) {
    keyboard_init();
    kprint_ok("PS/2 keyboard initialized");

    if (mouse_init()) {
        kprint_ok("PS/2 mouse initialized");
    } else {
        kprint_error("PS/2 mouse initialization failed");
    }

    beep(440, 4);
    kprint_ok("Audio initialized");

    net_init();
    if (g_net_state.virtio_net_present) {
        kprint_ok("VirtIO-Net detected");
    } else if (g_net_state.e1000_present) {
        kprint_ok("E1000 detected");
    } else {
        kprint_error("Network device not detected");
    }

}

static void init_kernel(void) {
    init_fb();
    init_interrupts();
    init_mem();
    init_drivers();
    init_cpu();
    init_storage_and_fs();
}

void kmain(void) {
    init_kernel();
    module_loader_init();

    cmos_init();
    if (!apic_init()) {
        kprint("[APIC] Fallback to PIC IRQ routing\n");
    }
    core_init();

    const char* app_status = NULL;
    if (module_loader_start_module_ring3("login", &app_status)) {
        kprint("[BOOT] login module autostart requested\n");
    } else {
        kprint("[BOOT] login module start failed: ");
        if (app_status) {
            kprint(app_status);
        } else {
            kprint("unknown");
        }
        kprint("\n");
        app_status = NULL;
        if (module_loader_start_module_ring3("konsole", &app_status)) {
            kprint("[BOOT] konsole module autostart requested\n");
        } else {
            kprint("[BOOT] konsole module start failed: ");
            if (app_status) {
                kprint(app_status);
            } else {
                kprint("unknown");
            }
            kprint("\n");
        }
    }

    while (1) {
        thread_yield();
        __asm__ volatile("hlt");
    }
}
