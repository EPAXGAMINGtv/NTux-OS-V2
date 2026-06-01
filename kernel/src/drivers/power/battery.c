#include <drivers/power/battery.h>

#include <interrupt/apic/apic.h>
#include <drivers/framebuffer/kprint.h>
#include <arch/x86_64/io.h>
#include <mm/hhdm.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_smbios_request g_smbios_request = {
    .id = LIMINE_SMBIOS_REQUEST_ID,
    .revision = 0
};

typedef struct __attribute__((packed)) {
    char anchor[4];
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint16_t max_struct_size;
    uint8_t entry_point_revision;
    uint8_t formatted_area[5];
    char intermediate_anchor[5];
    uint8_t intermediate_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t structure_count;
    uint8_t bcd_revision;
} smbios32_entry_t;

typedef struct __attribute__((packed)) {
    char anchor[5];
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint8_t docrev;
    uint8_t entry_point_revision;
    uint8_t reserved;
    uint32_t table_max_size;
    uint64_t table_address;
} smbios64_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint16_t handle;
} smbios_header_t;

static bool g_inited = false;
static bool g_has_battery = false;
static bool g_has_estimate = false;
static uint8_t g_estimate_percent = 0;
static bool g_apm_available = false;

static inline uintptr_t smbios_map_addr(uintptr_t addr) {
    uint64_t off = hhdm_offset_get();
    if (!addr) return 0;
    if (off == 0) {
        if (addr >= 0xFFFF800000000000ull) return addr;
        return 0;
    }
    if (addr >= off) return addr;
    return addr + (uintptr_t)off;
}

static uint8_t checksum8(const void* ptr, uint32_t len) {
    const uint8_t* p = (const uint8_t*)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) sum = (uint8_t)(sum + p[i]);
    return sum;
}

static const uint8_t* next_smbios_record(const uint8_t* rec, const uint8_t* end, uint8_t len) {
    if (!rec || rec >= end) return end;
    const uint8_t* p = rec + len;
    while (p + 1 < end) {
        if (p[0] == 0 && p[1] == 0) return p + 2;
        p++;
    }
    return end;
}

static void parse_smbios_table(const uint8_t* table, uint32_t len) {
    if (!table || len < sizeof(smbios_header_t)) return;
    const uint8_t* p = table;
    const uint8_t* end = table + len;

    while (p + sizeof(smbios_header_t) <= end) {
        const smbios_header_t* h = (const smbios_header_t*)p;
        if (h->length < sizeof(smbios_header_t) || p + h->length > end) break;
        if (h->type == 127) break;

        if (h->type == 22 && h->length >= 16) {
            g_has_battery = true;
        }

        p = next_smbios_record(p, end, h->length);
    }
}

static bool probe_apm_interface(void) {
    outb(APM_CNT_PORT, APM_CNT_GET_INFO);
    uint16_t info1 = inw(APM_DATA_PORT);
    outb(APM_CNT_PORT, APM_CNT_GET_INFO);
    uint16_t info2 = inw(APM_DATA_PORT);

    if (info1 == 0 || info1 == 0xFFFF) return false;
    if (info1 != info2) return false;
    return true;
}

static bool read_apm_percent(uint8_t* out_percent, bool* out_charging) {
    if (!out_percent || !out_charging || !g_apm_available) return false;

    outb(APM_CNT_PORT, APM_CNT_GET_PM_STATE);
    uint16_t pm = (uint16_t)inb(APM_DATA_PORT) | ((uint16_t)inb((uint16_t)(APM_DATA_PORT + 1)) << 8);
    if (pm == 0 || pm == 0xFFFF) return false;

    uint8_t lo = (uint8_t)(pm & 0xFF);
    uint8_t hi = (uint8_t)((pm >> 8) & 0xFF);
    uint8_t pct = 0xFF;

    if (hi <= 100) pct = hi;
    else if (lo <= 100) pct = lo;
    if (pct > 100) return false;

    *out_percent = pct;
    *out_charging = ((lo & 0x80u) != 0u);
    return true;
}

void battery_init(void) {
    if (g_inited) return;
    g_inited = true;
    g_apm_available = probe_apm_interface();
    if (g_apm_available) {
        kprint("[BAT] APM interface detected\n");
    }

    if (!g_smbios_request.response) {
        kprint("[BAT] SMBIOS not provided\n");
        return;
    }

    uint64_t ep64 = g_smbios_request.response->entry_64;
    uint64_t ep32 = g_smbios_request.response->entry_32;

    if (ep64) {
        smbios64_entry_t* e = (smbios64_entry_t*)(uintptr_t)smbios_map_addr((uintptr_t)ep64);
        if (!e) return;
        if (e->length >= sizeof(smbios64_entry_t) &&
            e->anchor[0] == '_' && e->anchor[1] == 'S' && e->anchor[2] == 'M' &&
            e->anchor[3] == '3' && e->anchor[4] == '_' &&
            checksum8(e, e->length) == 0) {
            const uint8_t* table = (const uint8_t*)(uintptr_t)smbios_map_addr((uintptr_t)e->table_address);
            if (!table) return;
            parse_smbios_table(table, e->table_max_size);
            return;
        }
    }

    if (ep32) {
        smbios32_entry_t* e = (smbios32_entry_t*)(uintptr_t)smbios_map_addr((uintptr_t)ep32);
        if (!e) return;
        if (e->length >= sizeof(smbios32_entry_t) &&
            e->anchor[0] == '_' && e->anchor[1] == 'S' && e->anchor[2] == 'M' && e->anchor[3] == '_' &&
            checksum8(e, e->length) == 0) {
            const uint8_t* table = (const uint8_t*)(uintptr_t)smbios_map_addr((uintptr_t)e->table_address);
            if (!table) return;
            parse_smbios_table(table, e->table_length);
        }
    }
}

void battery_get_status(battery_status_t* out_status) {
    if (!out_status) return;

    if (!g_inited) battery_init();

    out_status->present = g_has_battery || g_apm_available;
    out_status->has_percent = false;
    out_status->percent = 0;
    out_status->charging = false;

    uint8_t apm_percent = 0;
    bool apm_charging = false;
    if (read_apm_percent(&apm_percent, &apm_charging)) {
        out_status->present = true;
        out_status->has_percent = true;
        out_status->percent = apm_percent;
        out_status->charging = apm_charging;
        return;
    }

    if (g_has_battery && g_has_estimate) {
        out_status->has_percent = true;
        out_status->percent = g_estimate_percent;
    }
}
