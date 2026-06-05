#include "drivers/pci/pci.h"
#include <arch/x86_64/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_get_address(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset) {
    return (1 << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC);
}

uint32_t pci_read_field(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset, uint32_t size) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);

    uint32_t data = inl(PCI_CONFIG_DATA);
    if (size == 1) {
        return (data >> ((offset & 3) * 8)) & 0xFF;
    } else if (size == 2) {
        return (data >> ((offset & 2) * 8)) & 0xFFFF;
    } else {
        return data;
    }
}

void pci_write_field(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    if (size == 4) {
        outl(PCI_CONFIG_DATA, value);
        return;
    }

    uint32_t data = inl(PCI_CONFIG_DATA);
    if (size == 1) {
        uint32_t shift = (offset & 3u) * 8u;
        data &= ~(0xFFu << shift);
        data |= (value & 0xFFu) << shift;
    } else if (size == 2) {
        uint32_t shift = (offset & 2u) * 8u;
        data &= ~(0xFFFFu << shift);
        data |= (value & 0xFFFFu) << shift;
    } else {
        return;
    }
    outl(PCI_CONFIG_DATA, data);
}


void pci_scan(pci_scan_func_t callback, void *extra) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t device = 0; device < 32; device++) {
            uint16_t vendor_id = pci_read_field(bus, device, 0, PCI_VENDOR_ID, 2);
            if (vendor_id == 0xFFFF) continue; 
            uint16_t device_id = pci_read_field(bus, device, 0, PCI_DEVICE_ID, 2);
            callback(bus, device, vendor_id, device_id, extra);
        }
    }
}

void pci_scan_ex(pci_scan_func_ex_t callback, void* extra) {
    pci_scan_ex_range(0, 255, callback, extra);
}

void pci_scan_ex_range(uint32_t start_bus, uint32_t end_bus, pci_scan_func_ex_t callback, void* extra) {
    if (!callback) return;
    if (end_bus > 255) end_bus = 255;

    for (uint32_t bus = start_bus; bus <= end_bus; ++bus) {
        for (uint32_t device = 0; device < 32; ++device) {
            uint16_t vendor0 = (uint16_t)pci_read_field(bus, device, 0, PCI_VENDOR_ID, 2);
            if (vendor0 == 0xFFFF) continue;

            uint8_t header = (uint8_t)pci_read_field(bus, device, 0, 0x0E, 1);
            uint32_t function_count = (header & 0x80u) ? 8u : 1u;

            for (uint32_t function = 0; function < function_count; ++function) {
                uint16_t vendor = (uint16_t)pci_read_field(bus, device, function, PCI_VENDOR_ID, 2);
                if (vendor == 0xFFFF) continue;
                uint16_t dev_id = (uint16_t)pci_read_field(bus, device, function, PCI_DEVICE_ID, 2);
                callback(bus, device, function, vendor, dev_id, extra);
            }
        }
    }
}

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    pci_device_t* out;
    int found;
} pci_find_ctx_t;

static void pci_find_cb(uint32_t bus, uint32_t device, uint32_t function, uint16_t vendor, uint16_t dev_id, void* extra) {
    pci_find_ctx_t* ctx = (pci_find_ctx_t*)extra;
    if (ctx->found) return;
    if (vendor == ctx->vendor_id && dev_id == ctx->device_id) {
        ctx->out->bus = (uint8_t)bus;
        ctx->out->device = (uint8_t)device;
        ctx->out->function = (uint8_t)function;
        ctx->found = 1;
    }
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out) {
    pci_find_ctx_t ctx;
    ctx.vendor_id = vendor_id;
    ctx.device_id = device_id;
    ctx.out = out;
    ctx.found = 0;
    pci_scan_ex(pci_find_cb, &ctx);
    return ctx.found;
}

uint8_t pci_find_capability(uint32_t bus, uint32_t device, uint32_t function, uint8_t cap_id) {
    uint16_t status = pci_read_field(bus, device, function, 0x06, 2);
    if (!(status & (1 << 4)))
        return 0;

    uint8_t cap_ptr = pci_read_field(bus, device, function, 0x34, 1);
    cap_ptr &= 0xFC;
    if (cap_ptr == 0)
        return 0;

    while (cap_ptr != 0) {
        uint8_t found_id = pci_read_field(bus, device, function, cap_ptr, 1);
        if (found_id == cap_id)
            return cap_ptr;
        cap_ptr = pci_read_field(bus, device, function, cap_ptr + 1, 1);
        cap_ptr &= 0xFC;
    }

    return 0;
}

uint8_t pci_detect_max_bus(void) {
    uint8_t max_bus = 0;
    for (uint32_t device = 0; device < 32; ++device) {
        uint16_t vendor0 = (uint16_t)pci_read_field(0, device, 0, PCI_VENDOR_ID, 2);
        if (vendor0 == 0xFFFF) continue;

        uint8_t header = (uint8_t)pci_read_field(0, device, 0, 0x0E, 1);
        uint32_t function_count = (header & 0x80u) ? 8u : 1u;

        for (uint32_t function = 0; function < function_count; ++function) {
            uint16_t vendor = (uint16_t)pci_read_field(0, device, function, PCI_VENDOR_ID, 2);
            if (vendor == 0xFFFF) continue;
            uint8_t class_code = (uint8_t)pci_read_field(0, device, function, 0x0B, 1);
            uint8_t subclass = (uint8_t)pci_read_field(0, device, function, 0x0A, 1);
            if (class_code == 0x06u && subclass == 0x04u) {
                uint8_t subordinate = (uint8_t)pci_read_field(0, device, function, 0x1A, 1);
                if (subordinate > max_bus) max_bus = subordinate;
            }
        }
    }
    return max_bus;
}
