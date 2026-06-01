#include "acpi.h"
#include <mm/hhdm.h>
#include <arch/x86_64/io.h>
#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request g_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

static bool acpi_tables_ready = false;
static struct acpi_sdt_header* acpi_root = 0;
static bool acpi_root_is_xsdt = false;

static uint16_t acpi_pm1a_cnt_port = 0;
static uint16_t acpi_pm1b_cnt_port = 0;
static uint8_t acpi_slp_typa = PM1_SLEEP_TYPE_S5;
static uint8_t acpi_slp_typb = PM1_SLEEP_TYPE_S5;
static bool acpi_s5_found = false;
static uint32_t acpi_smi_cmd_port = 0;
static uint8_t acpi_acpi_enable = 0;
static uint8_t acpi_acpi_disable = 0;

struct acpi_gas {
    uint8_t space_id;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

static struct acpi_gas acpi_reset_reg = {0};
static uint8_t acpi_reset_value = 0;
static bool acpi_reset_supported = false;
static struct acpi_gas acpi_pm1a_cnt_reg = {0};
static struct acpi_gas acpi_pm1b_cnt_reg = {0};

struct acpi_rsdp_v1 {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_rsdp_v2 {
    struct acpi_rsdp_v1 first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

static inline uintptr_t acpi_map_addr(uintptr_t addr) {
    uint64_t off = hhdm_offset_get();
    if (!addr) return 0;
    if (off == 0) {
        if (addr >= 0xFFFF800000000000ull) return addr;
        return 0;
    }
    if (addr >= off) return addr;
    return addr + (uintptr_t)off;
}

static bool acpi_gas_get_io_port(const struct acpi_gas* reg, uint16_t* out_port) {
    if (!reg || !out_port) return false;
    if (reg->space_id != 1) return false; /* System I/O */
    if (reg->address == 0 || reg->address > 0xFFFFu) return false;
    *out_port = (uint16_t)reg->address;
    return true;
}

static bool acpi_gas_get_access_size(const struct acpi_gas* reg, uint8_t* out_size) {
    if (!reg || !out_size) return false;
    uint8_t size = reg->access_size;
    if (size == 0) {
        if (reg->bit_width == 0) return false;
        if (reg->bit_width <= 8) size = 1;
        else if (reg->bit_width <= 16) size = 2;
        else if (reg->bit_width <= 32) size = 4;
        else if (reg->bit_width <= 64) size = 8;
        else return false;
    } else {
        if (size == 1) size = 1;
        else if (size == 2) size = 2;
        else if (size == 3) size = 4;
        else if (size == 4) size = 8;
        else return false;
    }
    *out_size = size;
    return true;
}

static bool acpi_gas_read(const struct acpi_gas* reg, uint64_t* out_val) {
    if (!reg || !out_val || reg->address == 0) return false;
    uint8_t size = 0;
    if (!acpi_gas_get_access_size(reg, &size)) return false;
    if (reg->space_id == 1) {
        uint16_t port = 0;
        if (!acpi_gas_get_io_port(reg, &port)) return false;
        if (size == 1) *out_val = inb(port);
        else if (size == 2) *out_val = inw(port);
        else if (size == 4) *out_val = inl(port);
        else return false;
        return true;
    }
    if (reg->space_id == 0) {
        uintptr_t addr = acpi_map_addr((uintptr_t)reg->address);
        if (addr == 0) return false;
        if (size == 1) *out_val = *(volatile uint8_t*)addr;
        else if (size == 2) *out_val = *(volatile uint16_t*)addr;
        else if (size == 4) *out_val = *(volatile uint32_t*)addr;
        else if (size == 8) *out_val = *(volatile uint64_t*)addr;
        else return false;
        return true;
    }
    return false;
}

static bool acpi_gas_write(const struct acpi_gas* reg, uint64_t val) {
    if (!reg || reg->address == 0) return false;
    uint8_t size = 0;
    if (!acpi_gas_get_access_size(reg, &size)) return false;
    if (reg->space_id == 1) {
        uint16_t port = 0;
        if (!acpi_gas_get_io_port(reg, &port)) return false;
        if (size == 1) outb(port, (uint8_t)val);
        else if (size == 2) outw(port, (uint16_t)val);
        else if (size == 4) outl(port, (uint32_t)val);
        else return false;
        return true;
    }
    if (reg->space_id == 0) {
        uintptr_t addr = acpi_map_addr((uintptr_t)reg->address);
        if (addr == 0) return false;
        if (size == 1) *(volatile uint8_t*)addr = (uint8_t)val;
        else if (size == 2) *(volatile uint16_t*)addr = (uint16_t)val;
        else if (size == 4) *(volatile uint32_t*)addr = (uint32_t)val;
        else if (size == 8) *(volatile uint64_t*)addr = (uint64_t)val;
        else return false;
        return true;
    }
    return false;
}

static bool acpi_pm1_read(const struct acpi_gas* reg, uint16_t port, uint16_t* out_val) {
    if (!out_val) return false;
    if (reg && reg->address != 0 && (reg->space_id == 0 || reg->space_id == 1)) {
        uint64_t v = 0;
        if (acpi_gas_read(reg, &v)) {
            *out_val = (uint16_t)v;
            return true;
        }
    }
    if (port == 0) return false;
    *out_val = inw(port);
    return true;
}

static bool acpi_pm1_write(const struct acpi_gas* reg, uint16_t port, uint16_t val) {
    if (reg && reg->address != 0 && (reg->space_id == 0 || reg->space_id == 1)) {
        if (acpi_gas_write(reg, val)) return true;
    }
    if (port == 0) return false;
    outw(port, val);
    return true;
}

static bool acpi_checksum_ok(const void* table, uint32_t length) {
    if (!table || length == 0) return false;
    const uint8_t* p = (const uint8_t*)table;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; ++i) sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}

static struct acpi_rsdp_v1* acpi_rsdp_probe_ptr(struct acpi_rsdp_v1* rsdp) {
    if (!rsdp) return 0;
    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) return 0;
    if (!acpi_checksum_ok(rsdp, sizeof(struct acpi_rsdp_v1))) return 0;
    if (rsdp->revision >= 2) {
        struct acpi_rsdp_v2* rsdp2 = (struct acpi_rsdp_v2*)rsdp;
        if (rsdp2->length < sizeof(struct acpi_rsdp_v2)) return 0;
        if (!acpi_checksum_ok(rsdp2, rsdp2->length)) return 0;
    }
    return rsdp;
}

static struct acpi_rsdp_v1* acpi_rsdp_probe(uintptr_t phys) {
    struct acpi_rsdp_v1* rsdp = (struct acpi_rsdp_v1*)acpi_map_addr(phys);
    return acpi_rsdp_probe_ptr(rsdp);
}

static struct acpi_rsdp_v1* acpi_rsdp_from_limine(uintptr_t addr) {
    if (addr) {
        struct acpi_rsdp_v1* rsdp = (struct acpi_rsdp_v1*)acpi_map_addr(addr);
        if (acpi_rsdp_probe_ptr(rsdp)) return rsdp;
    }
    return acpi_rsdp_probe(addr);
}

static struct acpi_rsdp_v1* acpi_find_rsdp_fallback(void) {
    uint16_t* ebda_seg_ptr = (uint16_t*)acpi_map_addr(0x40E);
    if (ebda_seg_ptr) {
        uintptr_t ebda = (uintptr_t)(*ebda_seg_ptr) * 16u;
        for (uintptr_t addr = ebda; addr < ebda + 1024; addr += 16) {
            struct acpi_rsdp_v1* rsdp = acpi_rsdp_probe(addr);
            if (rsdp) return rsdp;
        }
    }
    for (uintptr_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        struct acpi_rsdp_v1* rsdp = acpi_rsdp_probe(addr);
        if (rsdp) return rsdp;
    }
    return 0;
}

static uint32_t acpi_read_u32(const void* ptr) {
    const uint8_t* p = (const uint8_t*)ptr;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t acpi_read_u64(const void* ptr) {
    const uint8_t* p = (const uint8_t*)ptr;
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static bool aml_parse_pkg_len(const uint8_t* aml, uint32_t len, uint32_t* off, uint32_t* out_len) {
    if (!aml || !off || !out_len || *off >= len) return false;
    uint8_t lead = aml[*off];
    uint8_t bytes = (lead >> 6) & 3;
    uint32_t l = (uint32_t)(lead & 0x3F);
    (*off)++;
    if (bytes == 0) {
        *out_len = l;
        return true;
    }
    if (*off + bytes > len) return false;
    for (uint8_t i = 0; i < bytes; ++i) {
        l |= (uint32_t)aml[*off + i] << (4 + i * 8);
    }
    *off += bytes;
    *out_len = l;
    return true;
}

static bool aml_parse_int8(const uint8_t* aml, uint32_t len, uint32_t* off, uint8_t* out) {
    if (!aml || !off || !out || *off >= len) return false;
    uint8_t op = aml[*off];
    if (op == 0x0A) {
        if (*off + 1 >= len) return false;
        *out = aml[*off + 1];
        *off += 2;
        return true;
    }
    if (op == 0x0B) {
        if (*off + 2 >= len) return false;
        uint16_t v = (uint16_t)aml[*off + 1] | ((uint16_t)aml[*off + 2] << 8);
        *out = (uint8_t)v;
        *off += 3;
        return true;
    }
    if (op == 0x0C) {
        if (*off + 4 >= len) return false;
        uint32_t v = (uint32_t)aml[*off + 1] |
                     ((uint32_t)aml[*off + 2] << 8) |
                     ((uint32_t)aml[*off + 3] << 16) |
                     ((uint32_t)aml[*off + 4] << 24);
        *out = (uint8_t)v;
        *off += 5;
        return true;
    }
    if (op == 0x0E) {
        if (*off + 8 >= len) return false;
        *out = (uint8_t)aml[*off + 1];
        *off += 9;
        return true;
    }
    if (op == 0x00 || op == 0x01) {
        *out = op;
        (*off)++;
        return true;
    }
    if (op >= 0x02 && op <= 0x3F) {
        *out = op;
        (*off)++;
        return true;
    }
    return false;
}

static bool acpi_find_s5_types(const uint8_t* aml, uint32_t aml_len, uint8_t* out_a, uint8_t* out_b) {
    if (!aml || aml_len < 4 || !out_a || !out_b) return false;
    for (uint32_t i = 0; i + 4 < aml_len; ++i) {
        if (memcmp(&aml[i], "_S5_", 4) != 0) continue;

        uint32_t off = i + 4;
        if (off >= aml_len || aml[off] != 0x12) continue;
        off++;

        uint32_t pkg_len = 0;
        if (!aml_parse_pkg_len(aml, aml_len, &off, &pkg_len)) continue;
        if (off >= aml_len) continue;
        off++;

        uint8_t a = 0;
        uint8_t b = 0;
        if (!aml_parse_int8(aml, aml_len, &off, &a)) continue;
        if (!aml_parse_int8(aml, aml_len, &off, &b)) continue;

        *out_a = a & 0x7;
        *out_b = b & 0x7;
        return true;
    }
    return false;
}

static void acpi_try_legacy_pm1_ports(uint8_t sleep_state) {
    uint16_t pm1_addrs[] = {0xB00, 0xB04, PM1_CNT_ADDR};
    for (int i = 0; i < 3; ++i) {
        uint16_t addr = pm1_addrs[i];
        uint16_t ctrl = inw(addr);
        ctrl &= ~PM1_CNT_SLEEP_TYPE_MASK;
        ctrl |= (uint16_t)(sleep_state << PM1_CNT_SLEEP_TYPE_SHIFT);
        ctrl |= PM1_CNT_SLEEP_ENABLE;
        outw(addr, ctrl);
    }
}

bool acpi_init(void) {
    if (acpi_tables_ready) return true;
    if (!g_rsdp_request.response || !g_rsdp_request.response->address) {
        return false;
    }
    struct acpi_rsdp_v1* rsdp = acpi_rsdp_from_limine((uintptr_t)g_rsdp_request.response->address);
    if (!rsdp) {
        return false;
    }
    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0 || !acpi_checksum_ok(rsdp, sizeof(struct acpi_rsdp_v1))) {
        rsdp = acpi_find_rsdp_fallback();
        if (!rsdp) return false;
    }

    acpi_root = 0;
    acpi_root_is_xsdt = false;
    if (rsdp->revision >= 2) {
        struct acpi_rsdp_v2* rsdp2 = (struct acpi_rsdp_v2*)rsdp;
        if (!acpi_checksum_ok(rsdp2, rsdp2->length)) {
            return false;
        }
        if (rsdp2->xsdt_address != 0) {
            acpi_root = (struct acpi_sdt_header*)acpi_map_addr((uintptr_t)rsdp2->xsdt_address);
            acpi_root_is_xsdt = true;
        }
    }
    if (!acpi_root && rsdp->rsdt_address != 0) {
        acpi_root = (struct acpi_sdt_header*)acpi_map_addr((uintptr_t)rsdp->rsdt_address);
        acpi_root_is_xsdt = false;
    }
    if (!acpi_root) return false;
    if (acpi_root->length < sizeof(struct acpi_sdt_header)) return false;
    if (!acpi_checksum_ok(acpi_root, acpi_root->length)) {
        return false;
    }

    struct acpi_sdt_header* fadt = (struct acpi_sdt_header*)acpi_find_table("FACP");
    if (!fadt || fadt->length < 80) {
        kprint("[ACPI] FADT not found\n");
        return false;
    }

    uint8_t* fadt_raw = (uint8_t*)fadt;
    acpi_smi_cmd_port = acpi_read_u32(fadt_raw + 48);
    acpi_acpi_enable = fadt_raw[52];
    acpi_acpi_disable = fadt_raw[53];

    acpi_pm1a_cnt_port = (uint16_t)acpi_read_u32(fadt_raw + 64);
    acpi_pm1b_cnt_port = (uint16_t)acpi_read_u32(fadt_raw + 68);
    acpi_pm1a_cnt_reg = (struct acpi_gas){0};
    acpi_pm1b_cnt_reg = (struct acpi_gas){0};
    if (fadt->length >= 172 + sizeof(struct acpi_gas)) {
        struct acpi_gas* x_pm1a = (struct acpi_gas*)(fadt_raw + 172);
        if (x_pm1a->address != 0 && (x_pm1a->space_id == 0 || x_pm1a->space_id == 1)) {
            acpi_pm1a_cnt_reg = *x_pm1a;
            uint16_t port = 0;
            if (acpi_gas_get_io_port(x_pm1a, &port)) acpi_pm1a_cnt_port = port;
        }
    }
    if (fadt->length >= 184 + sizeof(struct acpi_gas)) {
        struct acpi_gas* x_pm1b = (struct acpi_gas*)(fadt_raw + 184);
        if (x_pm1b->address != 0 && (x_pm1b->space_id == 0 || x_pm1b->space_id == 1)) {
            acpi_pm1b_cnt_reg = *x_pm1b;
            uint16_t port = 0;
            if (acpi_gas_get_io_port(x_pm1b, &port)) acpi_pm1b_cnt_port = port;
        }
    }
    if (acpi_pm1a_cnt_reg.address == 0 && acpi_pm1a_cnt_port != 0) {
        acpi_pm1a_cnt_reg.space_id = 1;
        acpi_pm1a_cnt_reg.bit_width = 16;
        acpi_pm1a_cnt_reg.access_size = 2;
        acpi_pm1a_cnt_reg.address = acpi_pm1a_cnt_port;
    }
    if (acpi_pm1b_cnt_reg.address == 0 && acpi_pm1b_cnt_port != 0) {
        acpi_pm1b_cnt_reg.space_id = 1;
        acpi_pm1b_cnt_reg.bit_width = 16;
        acpi_pm1b_cnt_reg.access_size = 2;
        acpi_pm1b_cnt_reg.address = acpi_pm1b_cnt_port;
    }

    if (fadt->length >= 128) {
        struct acpi_gas* reg = (struct acpi_gas*)(fadt_raw + 116);
        acpi_reset_reg = *reg;
        acpi_reset_value = fadt_raw[128];
        acpi_reset_supported = (acpi_reset_reg.address != 0) &&
                               (acpi_reset_reg.space_id == 0 || acpi_reset_reg.space_id == 1);
    } else {
        acpi_reset_supported = false;
    }

    uint64_t dsdt_addr = (uint64_t)acpi_read_u32(fadt_raw + 40);
    if (fadt->length >= 156) {
        uint64_t x_dsdt = acpi_read_u64(fadt_raw + 140);
        if (x_dsdt != 0) dsdt_addr = x_dsdt;
    }

    acpi_s5_found = false;
    if (dsdt_addr != 0) {
        struct acpi_sdt_header* dsdt = (struct acpi_sdt_header*)acpi_map_addr((uintptr_t)dsdt_addr);
        if (dsdt && dsdt->length > sizeof(struct acpi_sdt_header) && acpi_checksum_ok(dsdt, dsdt->length)) {
            uint8_t* aml = (uint8_t*)dsdt + sizeof(struct acpi_sdt_header);
            uint32_t aml_len = dsdt->length - (uint32_t)sizeof(struct acpi_sdt_header);
            uint8_t s5a = 0, s5b = 0;
            if (acpi_find_s5_types(aml, aml_len, &s5a, &s5b)) {
                acpi_slp_typa = s5a;
                acpi_slp_typb = s5b;
                acpi_s5_found = true;
            }
        }
    }

    acpi_tables_ready = true;
    kprint("[ACPI] Ready. PM1A=0x");
    kprinthex(acpi_pm1a_cnt_port);
    kprint(" PM1B=0x");
    kprinthex(acpi_pm1b_cnt_port);
    kprint(" SLP_TYPA=");
    kprint_uint(acpi_slp_typa);
    kprint(" SLP_TYPB=");
    kprint_uint(acpi_slp_typb);
    kprint("\n");
    return true;
}

bool acpi_find_smm(void) {
    return true;
}

void acpi_enable_sci(void) {
    uint16_t ctrl = 0;
    if (!acpi_pm1_read(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, &ctrl)) return;
    if (ctrl & PM1_CNT_SCI_EN) return;
    if (acpi_smi_cmd_port != 0 && acpi_acpi_enable != 0) {
        outb((uint16_t)acpi_smi_cmd_port, acpi_acpi_enable);
    }
    for (volatile int i = 0; i < 200000; ++i) {
        if (acpi_pm1_read(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, &ctrl)) {
            if (ctrl & PM1_CNT_SCI_EN) return;
        }
        __asm__ volatile("" ::: "memory");
    }
    if (!acpi_pm1_read(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, &ctrl)) return;
    ctrl |= PM1_CNT_SCI_EN;
    (void)acpi_pm1_write(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, ctrl);
}

void acpi_enter_sleep(uint8_t sleep_state) {
    kprint("[ACPI] Entering sleep state S");
    kprint_uint(sleep_state);
    kprint("\n");

    if (!acpi_tables_ready && !acpi_init()) {
        acpi_try_legacy_pm1_ports(sleep_state);
        return;
    }

    if (sleep_state != PM1_SLEEP_TYPE_S5 || !acpi_s5_found) {
        acpi_try_legacy_pm1_ports(sleep_state);
        return;
    }
    acpi_enable_sci();

    uint16_t ctrl_a = 0;
    if (!acpi_pm1_read(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, &ctrl_a)) {
        acpi_try_legacy_pm1_ports(sleep_state);
        return;
    }
    ctrl_a &= ~PM1_CNT_SLEEP_TYPE_MASK;
    ctrl_a |= (uint16_t)(acpi_slp_typa << PM1_CNT_SLEEP_TYPE_SHIFT);
    (void)acpi_pm1_write(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, ctrl_a);

    if (acpi_pm1b_cnt_reg.address != 0 || acpi_pm1b_cnt_port != 0) {
        uint16_t ctrl_b = 0;
        if (acpi_pm1_read(&acpi_pm1b_cnt_reg, acpi_pm1b_cnt_port, &ctrl_b)) {
            ctrl_b &= ~PM1_CNT_SLEEP_TYPE_MASK;
            ctrl_b |= (uint16_t)(acpi_slp_typb << PM1_CNT_SLEEP_TYPE_SHIFT);
            (void)acpi_pm1_write(&acpi_pm1b_cnt_reg, acpi_pm1b_cnt_port, ctrl_b);
        }
    }

    for (volatile int i = 0; i < 100000; ++i) {
        __asm__ volatile("" ::: "memory");
    }

    ctrl_a |= PM1_CNT_SLEEP_ENABLE;
    (void)acpi_pm1_write(&acpi_pm1a_cnt_reg, acpi_pm1a_cnt_port, ctrl_a);

    if (acpi_pm1b_cnt_reg.address != 0 || acpi_pm1b_cnt_port != 0) {
        uint16_t ctrl_b = 0;
        if (acpi_pm1_read(&acpi_pm1b_cnt_reg, acpi_pm1b_cnt_port, &ctrl_b)) {
            ctrl_b |= PM1_CNT_SLEEP_ENABLE;
            (void)acpi_pm1_write(&acpi_pm1b_cnt_reg, acpi_pm1b_cnt_port, ctrl_b);
        }
    }
    for (volatile int i = 0; i < 500000; ++i) {
        __asm__ volatile("" ::: "memory");
    }

    acpi_try_legacy_pm1_ports(sleep_state);
}

bool acpi_try_reset(void) {
    if (!acpi_reset_supported) return false;
    if (!acpi_gas_write(&acpi_reset_reg, acpi_reset_value)) return false;
    for (volatile int i = 0; i < 200000; ++i) {
        __asm__ volatile("" ::: "memory");
    }
    return true;
}

const struct acpi_sdt_header* acpi_find_table(const char* sig4) {
    if (!sig4) return 0;
    if (!acpi_tables_ready && !acpi_init()) return 0;
    if (!acpi_root) return 0;
    if (acpi_root->length < sizeof(struct acpi_sdt_header)) return 0;
    if (acpi_root_is_xsdt) {
        uint32_t entries = (acpi_root->length - sizeof(struct acpi_sdt_header)) / 8;
        uint8_t* p = (uint8_t*)acpi_root + sizeof(struct acpi_sdt_header);
        for (uint32_t i = 0; i < entries; ++i) {
            struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)acpi_map_addr((uintptr_t)acpi_read_u64(p + i * 8));
            if (!hdr) continue;
            if (hdr->length < sizeof(struct acpi_sdt_header)) continue;
            if (memcmp(hdr->signature, sig4, 4) == 0 && acpi_checksum_ok(hdr, hdr->length)) {
                return hdr;
            }
        }
    } else {
        uint32_t entries = (acpi_root->length - sizeof(struct acpi_sdt_header)) / 4;
        uint8_t* p = (uint8_t*)acpi_root + sizeof(struct acpi_sdt_header);
        for (uint32_t i = 0; i < entries; ++i) {
            struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)acpi_map_addr((uintptr_t)acpi_read_u32(p + i * 4));
            if (!hdr) continue;
            if (hdr->length < sizeof(struct acpi_sdt_header)) continue;
            if (memcmp(hdr->signature, sig4, 4) == 0 && acpi_checksum_ok(hdr, hdr->length)) {
                return hdr;
            }
        }
    }
    return 0;
}
