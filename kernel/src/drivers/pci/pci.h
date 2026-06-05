#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_BAR0 0x10

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} pci_device_t;

typedef void (*pci_scan_func_t)(uint32_t bus, uint32_t device, uint16_t vendor, uint16_t device_id, void *extra);
typedef void (*pci_scan_func_ex_t)(uint32_t bus, uint32_t device, uint32_t function, uint16_t vendor, uint16_t device_id, void* extra);

uint32_t pci_read_field(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset, uint32_t size);
void pci_write_field(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset, uint32_t size, uint32_t value);
void pci_scan(pci_scan_func_t callback, void *extra);
void pci_scan_ex(pci_scan_func_ex_t callback, void* extra);
void pci_scan_ex_range(uint32_t start_bus, uint32_t end_bus, pci_scan_func_ex_t callback, void* extra);
uint8_t pci_detect_max_bus(void);

static inline uint32_t pci_read_config(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset) {
    return pci_read_field(bus, device, function, offset, 4);
}

static inline void pci_write_config(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset, uint32_t value) {
    pci_write_field(bus, device, function, offset, 4, value);
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);
uint8_t pci_find_capability(uint32_t bus, uint32_t device, uint32_t function, uint8_t cap_id);

#endif
