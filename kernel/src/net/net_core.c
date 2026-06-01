#include <net/net_priv.h>

#include <drivers/net/netdrv.h>
#include <drivers/pci/pci.h>
#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_NET_DEV_ID_LEGACY 0x1000
#define VIRTIO_NET_DEV_ID_MODERN 0x1041

#define INTEL_VENDOR_ID 0x8086
#define E1000_DEV_ID_82540EM 0x100E
#define E1000_DEV_ID_82545EM 0x100F
#define E1000_DEV_ID_82541EI 0x1013
#define E1000_DEV_ID_82574L 0x10D3

net_state_t g_net_state = {0};
net_driver_t g_net_driver = NET_DRV_NONE;

static bool is_virtio_net_device(uint16_t vendor_id, uint16_t device_id, uint8_t class_code, uint8_t subclass) {
    if (vendor_id == VIRTIO_VENDOR_ID &&
        (device_id == VIRTIO_NET_DEV_ID_LEGACY || device_id == VIRTIO_NET_DEV_ID_MODERN)) {
        return true;
    }
    return vendor_id == VIRTIO_VENDOR_ID && class_code == 0x02 && subclass == 0x00;
}

static bool is_e1000_device(uint16_t vendor_id, uint16_t device_id, uint8_t class_code, uint8_t subclass) {
    if (vendor_id != INTEL_VENDOR_ID) return false;
    if (class_code != 0x02 || subclass != 0x00) return false;
    return device_id == E1000_DEV_ID_82540EM ||
           device_id == E1000_DEV_ID_82545EM ||
           device_id == E1000_DEV_ID_82541EI ||
           device_id == E1000_DEV_ID_82574L;
}

static void pci_net_scan_cb(uint32_t bus, uint32_t device, uint16_t vendor, uint16_t device_id, void* extra) {
    (void)extra;
    uint8_t class_code = (uint8_t)pci_read_field(bus, device, 0, 0x0B, 1);
    uint8_t subclass = (uint8_t)pci_read_field(bus, device, 0, 0x0A, 1);
    if (g_net_state.present && g_net_state.virtio_net_present) return;

    if (is_virtio_net_device(vendor, device_id, class_code, subclass)) {
        g_net_state.present = true;
        g_net_state.virtio_net_present = true;
        g_net_state.bus = (uint8_t)bus;
        g_net_state.device = (uint8_t)device;
        g_net_state.function = 0;
        g_net_state.vendor_id = vendor;
        g_net_state.device_id = device_id;
        g_net_state.bar0 = pci_read_field(bus, device, 0, 0x10, 4);
        g_net_state.irq_line = (uint8_t)pci_read_field(bus, device, 0, 0x3C, 1);
        return;
    }

    if (!g_net_state.present && is_e1000_device(vendor, device_id, class_code, subclass)) {
        g_net_state.present = true;
        g_net_state.e1000_present = true;
        g_net_state.bus = (uint8_t)bus;
        g_net_state.device = (uint8_t)device;
        g_net_state.function = 0;
        g_net_state.vendor_id = vendor;
        g_net_state.device_id = device_id;
        g_net_state.bar0 = pci_read_field(bus, device, 0, 0x10, 4);
        g_net_state.irq_line = (uint8_t)pci_read_field(bus, device, 0, 0x3C, 1);
        return;
    }
}

int net_driver_tx_raw(const void* packet, uint16_t packet_len) {
    if (!g_net_state.ready) return -1;
    int rc = -1;
    switch (g_net_driver) {
        case NET_DRV_E1000:
            rc = e1000_net_tx_raw(packet, packet_len);
            break;
        case NET_DRV_VIRTIO:
            rc = virtio_net_tx_raw(packet, packet_len);
            break;
        default:
            rc = -1;
            break;
    }
    if (rc == 0) g_net_tx_packets++;
    return rc;
}

void net_driver_poll_rx(void) {
    switch (g_net_driver) {
        case NET_DRV_E1000:
            e1000_net_poll_rx();
            break;
        case NET_DRV_VIRTIO:
            virtio_net_poll_rx();
            break;
        default:
            break;
    }
}

void net_init(void) {
    memset(&g_net_state, 0, sizeof(g_net_state));
    g_net_state.irq_line = 0xFF;
    g_net_driver = NET_DRV_NONE;
    
    net_stack_reset();
    
    pci_scan(pci_net_scan_cb, NULL);
    if (!g_net_state.present) {
        kprint("[NET] No network device found\n");
        return;
    }

    if (g_net_state.e1000_present && !g_net_state.virtio_net_present) {
        if (!e1000_net_init()) return;
        g_net_driver = NET_DRV_E1000;
        g_net_state.ready = true;
        return;
    }

    if (!g_net_state.virtio_net_present) {
        kprint("[NET] No VirtIO-Net PCI device found\n");
        return;
    }

    if (!virtio_net_init()) return;
    g_net_driver = NET_DRV_VIRTIO;
    g_net_state.ready = true;
}










