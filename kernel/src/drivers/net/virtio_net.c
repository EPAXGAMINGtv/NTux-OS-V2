#include <net/net_priv.h>

#include <drivers/net/netdrv.h>
#include <drivers/pci/pci.h>
#include <drivers/framebuffer/kprint.h>
#include <interrupt/timer.h>
#include <arch/x86_64/io.h>
#include <mm/kmalloc.h>
#include <lib/string.h>

#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13
#define VIRTIO_PCI_MAC 0x14

#define PCI_STATUS 0x06
#define PCI_CAP_PTR 0x34
#define PCI_CAP_ID_VNDR 0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define VIRTIO_STATUS_ACK 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FAILED 0x80

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define NET_RX_BUF_COUNT 8
#define NET_RX_BUF_SIZE 2048
#define NET_RX_BUF_ALLOC (NET_RX_BUF_SIZE + (uint32_t)sizeof(virtio_net_hdr_t))
#define NET_TX_BUF_SIZE 2048

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

typedef struct {
    uint16_t qsize;
    uint16_t last_used_idx;
    uint8_t* mem_raw;
    uint8_t* mem;
    uint32_t mem_size;
    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;
} virtq_t;

typedef struct __attribute__((packed)) {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} virtio_pci_common_cfg_t;

typedef struct {
    virtio_pci_common_cfg_t* common;
    volatile uint8_t* notify_base;
    uint32_t notify_off_multiplier;
    volatile uint8_t* isr;
    volatile uint8_t* device;
} virtio_pci_caps_t;

typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} virtio_net_hdr_t;

static uint16_t g_io_base;
static virtq_t g_rxq;
static virtq_t g_txq;
static virtio_pci_caps_t g_vcaps;
static bool g_virtio_modern = false;
static uint16_t g_rx_count = NET_RX_BUF_COUNT;
static uint16_t g_tx_count = 1;
static uint8_t* g_rx_buffers[NET_RX_BUF_COUNT];
static uint8_t* g_tx_buffer = NULL;

static uint64_t pci_read_bar_base(uint32_t bus, uint32_t dev, uint32_t fn, uint8_t bar_index) {
    uint32_t off = 0x10u + (uint32_t)bar_index * 4u;
    uint32_t low = (uint32_t)pci_read_field(bus, dev, fn, off, 4);
    if (low & 0x1u) {
        return (uint64_t)(low & ~0x3u);
    }
    uint32_t type = low & 0x6u;
    if (type == 0x4u) {
        uint32_t high = (uint32_t)pci_read_field(bus, dev, fn, off + 4u, 4);
        return ((uint64_t)high << 32) | (uint64_t)(low & ~0xFu);
    }
    return (uint64_t)(low & ~0xFu);
}

static int virtio_pci_find_caps(uint32_t bus, uint32_t dev, uint32_t fn, virtio_pci_caps_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    uint16_t status = (uint16_t)pci_read_field(bus, dev, fn, PCI_STATUS, 2);
    if ((status & 0x10u) == 0) return -1;
    uint8_t cap = (uint8_t)pci_read_field(bus, dev, fn, PCI_CAP_PTR, 1);
    int guard = 0;
    while (cap && guard++ < 64) {
        uint8_t cap_id = (uint8_t)pci_read_field(bus, dev, fn, cap + 0, 1);
        uint8_t cap_next = (uint8_t)pci_read_field(bus, dev, fn, cap + 1, 1);
        uint8_t cap_len = (uint8_t)pci_read_field(bus, dev, fn, cap + 2, 1);
        uint8_t cfg_type = (uint8_t)pci_read_field(bus, dev, fn, cap + 3, 1);
        if (cap_id == PCI_CAP_ID_VNDR && cap_len >= 16) {
            uint8_t bar = (uint8_t)pci_read_field(bus, dev, fn, cap + 4, 1);
            uint32_t offset = (uint32_t)pci_read_field(bus, dev, fn, cap + 8, 4);
            uint32_t length = (uint32_t)pci_read_field(bus, dev, fn, cap + 12, 4);
            uint64_t base = pci_read_bar_base(bus, dev, fn, bar);
            volatile uint8_t* mmio = (volatile uint8_t*)net_phys_to_virt((uintptr_t)(base + offset));
            if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                out->common = (virtio_pci_common_cfg_t*)(uintptr_t)mmio;
            } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                out->notify_base = mmio;
                if (cap_len >= 20) {
                    out->notify_off_multiplier = (uint32_t)pci_read_field(bus, dev, fn, cap + 16, 4);
                } else {
                    out->notify_off_multiplier = 1;
                }
                (void)length;
            } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                out->isr = mmio;
            } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                out->device = mmio;
            }
        }
        cap = cap_next;
    }
    if (!out->common || !out->notify_base) return -1;
    return 0;
}

static bool virtq_setup_legacy(uint16_t qindex, virtq_t* q) {
    memset(q, 0, sizeof(*q));

    outw(g_io_base + VIRTIO_PCI_QUEUE_SEL, qindex);
    uint16_t qsize = inw(g_io_base + VIRTIO_PCI_QUEUE_NUM);
    if (qsize == 0) return false;

    uint32_t sz_desc = (uint32_t)sizeof(virtq_desc_t) * qsize;
    uint32_t sz_avail = 6u + 2u * qsize;
    uint32_t used_off = net_align_up_u32(sz_desc + sz_avail, 4096u);
    uint32_t sz_used = 6u + 8u * qsize;
    uint32_t total = used_off + sz_used;
    uint32_t total_alloc = total + 4096u;

    uint8_t* raw = (uint8_t*)kmalloc(total_alloc);
    if (!raw) return false;
    memset(raw, 0, total_alloc);

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + 4095u) & ~(uintptr_t)4095u;
    uint8_t* mem = (uint8_t*)aligned;
    memset(mem, 0, total);

    q->qsize = qsize;
    q->mem_raw = raw;
    q->mem = mem;
    q->mem_size = total;
    q->desc = (virtq_desc_t*)mem;
    q->avail = (virtq_avail_t*)(mem + sz_desc);
    q->used = (virtq_used_t*)(mem + used_off);
    q->last_used_idx = 0;

    outl(g_io_base + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(net_virt_to_phys(aligned) >> 12));
    return true;
}

static bool virtq_setup_modern(uint16_t qindex, virtq_t* q) {
    memset(q, 0, sizeof(*q));
    if (!g_vcaps.common) return false;
    g_vcaps.common->queue_select = qindex;
    uint16_t qsize = g_vcaps.common->queue_size;
    if (qsize == 0) return false;

    uint32_t sz_desc = (uint32_t)sizeof(virtq_desc_t) * qsize;
    uint32_t sz_avail = 6u + 2u * qsize;
    uint32_t used_off = net_align_up_u32(sz_desc + sz_avail, 4096u);
    uint32_t sz_used = 6u + 8u * qsize;
    uint32_t total = used_off + sz_used;
    uint32_t total_alloc = total + 4096u;

    uint8_t* raw = (uint8_t*)kmalloc(total_alloc);
    if (!raw) return false;
    memset(raw, 0, total_alloc);

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + 4095u) & ~(uintptr_t)4095u;
    uint8_t* mem = (uint8_t*)aligned;
    memset(mem, 0, total);

    q->qsize = qsize;
    q->mem_raw = raw;
    q->mem = mem;
    q->mem_size = total;
    q->desc = (virtq_desc_t*)mem;
    q->avail = (virtq_avail_t*)(mem + sz_desc);
    q->used = (virtq_used_t*)(mem + used_off);
    q->last_used_idx = 0;

    g_vcaps.common->queue_desc = (uint64_t)net_virt_to_phys((uintptr_t)q->desc);
    g_vcaps.common->queue_avail = (uint64_t)net_virt_to_phys((uintptr_t)q->avail);
    g_vcaps.common->queue_used = (uint64_t)net_virt_to_phys((uintptr_t)q->used);
    g_vcaps.common->queue_enable = 1;
    return true;
}

static void virtq_kick(uint16_t qindex) {
    if (g_virtio_modern && g_vcaps.common && g_vcaps.notify_base) {
        g_vcaps.common->queue_select = qindex;
        uint16_t off = g_vcaps.common->queue_notify_off;
        volatile uint16_t* notify = (volatile uint16_t*)(g_vcaps.notify_base + (uint32_t)off * g_vcaps.notify_off_multiplier);
        *notify = qindex;
        return;
    }
    outw(g_io_base + VIRTIO_PCI_QUEUE_NOTIFY, qindex);
}

static void rx_post_descriptor(uint16_t id) {
    if (id >= g_rx_count) return;
    if (!g_rx_buffers[id]) return;
    g_rxq.desc[id].addr = (uint64_t)net_virt_to_phys((uintptr_t)g_rx_buffers[id]);
    g_rxq.desc[id].len = NET_RX_BUF_ALLOC;
    g_rxq.desc[id].flags = VIRTQ_DESC_F_WRITE;
    g_rxq.desc[id].next = 0;

    uint16_t aidx = g_rxq.avail->idx;
    g_rxq.avail->ring[aidx % g_rxq.qsize] = id;
    __sync_synchronize();
    g_rxq.avail->idx = (uint16_t)(aidx + 1);
}

bool virtio_net_init(void) {
    memset(&g_rxq, 0, sizeof(g_rxq));
    memset(&g_txq, 0, sizeof(g_txq));
    for (uint16_t i = 0; i < NET_RX_BUF_COUNT; ++i) g_rx_buffers[i] = NULL;
    g_tx_buffer = NULL;

    if ((g_net_state.bar0 & 0x1u) == 0) {
        if (virtio_pci_find_caps(g_net_state.bus, g_net_state.device, 0, &g_vcaps) == 0) {
            g_virtio_modern = true;
            uint16_t cmd = (uint16_t)pci_read_field(g_net_state.bus, g_net_state.device, 0, 0x04, 2);
            cmd |= 0x0006;
            pci_write_field(g_net_state.bus, g_net_state.device, 0, 0x04, 2, cmd);

            g_vcaps.common->device_status = 0;
            g_vcaps.common->device_status = VIRTIO_STATUS_ACK;
            g_vcaps.common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;
            g_vcaps.common->driver_feature_select = 0;
            g_vcaps.common->driver_feature = 0;
            g_vcaps.common->device_status |= VIRTIO_STATUS_FEATURES_OK;
            if (!(g_vcaps.common->device_status & VIRTIO_STATUS_FEATURES_OK)) {
                g_vcaps.common->device_status |= VIRTIO_STATUS_FAILED;
                kprint("[NET] VirtIO modern features not accepted\n");
                return false;
            }

            if (!virtq_setup_modern(0, &g_rxq) || !virtq_setup_modern(1, &g_txq)) {
                g_vcaps.common->device_status |= VIRTIO_STATUS_FAILED;
                kprint("[NET] VirtIO modern queue setup failed\n");
                return false;
            }

            if (g_vcaps.device) {
                g_net_state.mac[0] = g_vcaps.device[0];
                g_net_state.mac[1] = g_vcaps.device[1];
                g_net_state.mac[2] = g_vcaps.device[2];
                g_net_state.mac[3] = g_vcaps.device[3];
                g_net_state.mac[4] = g_vcaps.device[4];
                g_net_state.mac[5] = g_vcaps.device[5];
            }
            g_rx_count = (g_rxq.qsize < NET_RX_BUF_COUNT) ? g_rxq.qsize : NET_RX_BUF_COUNT;
            g_tx_count = g_txq.qsize;
        } else {
            kprint("[NET] VirtIO BAR0 not I/O and no modern caps\n");
            return false;
        }
    } else {
        g_io_base = (uint16_t)(g_net_state.bar0 & ~0x3u);

        uint16_t cmd = (uint16_t)pci_read_field(g_net_state.bus, g_net_state.device, 0, 0x04, 2);
        cmd |= 0x0005;
        pci_write_field(g_net_state.bus, g_net_state.device, 0, 0x04, 2, cmd);

        outb(g_io_base + VIRTIO_PCI_STATUS, 0);
        outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
        outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
        outl(g_io_base + VIRTIO_PCI_GUEST_FEATURES, 0);

        if (!virtq_setup_legacy(0, &g_rxq) || !virtq_setup_legacy(1, &g_txq)) {
            outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
            kprint("[NET] VirtIO queue setup failed\n");
            return false;
        }

        for (int i = 0; i < 6; ++i) {
            g_net_state.mac[i] = inb(g_io_base + VIRTIO_PCI_MAC + i);
        }
        g_rx_count = (g_rxq.qsize < NET_RX_BUF_COUNT) ? g_rxq.qsize : NET_RX_BUF_COUNT;
        g_tx_count = g_txq.qsize;
    }

    for (uint16_t i = 0; i < g_rx_count; ++i) {
        g_rx_buffers[i] = (uint8_t*)kmalloc(NET_RX_BUF_ALLOC);
        if (!g_rx_buffers[i]) return false;
        memset(g_rx_buffers[i], 0, NET_RX_BUF_ALLOC);
        rx_post_descriptor(i);
    }
    if (!g_tx_buffer) {
        g_tx_buffer = (uint8_t*)kmalloc(NET_TX_BUF_SIZE);
        if (!g_tx_buffer) return false;
        memset(g_tx_buffer, 0, NET_TX_BUF_SIZE);
    }
    virtq_kick(0);

    if (g_virtio_modern && g_vcaps.common) {
        g_vcaps.common->device_status |= VIRTIO_STATUS_DRIVER_OK;
    } else {
        outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    }

    kprint("[NET] VirtIO-Net ready at PCI ");
    kprint_int(g_net_state.bus);
    kprint(":");
    kprint_int(g_net_state.device);
    kprint(".0 io=0x");
    kprinthex(g_virtio_modern ? 0 : g_io_base);
    kprint(" mac=");
    for (int i = 0; i < 6; ++i) {
        if (i) kprint(":");
        kprinthex(g_net_state.mac[i]);
    }
    kprint("\n");

    return true;
}

int virtio_net_tx_raw(const void* packet, uint16_t packet_len) {
    if (packet_len + sizeof(virtio_net_hdr_t) > NET_TX_BUF_SIZE) return -1;
    if (!g_tx_buffer) return -1;

    virtio_net_hdr_t* hdr = (virtio_net_hdr_t*)g_tx_buffer;
    memset(hdr, 0, sizeof(*hdr));
    memcpy(g_tx_buffer + sizeof(*hdr), packet, packet_len);

    g_txq.desc[0].addr = (uint64_t)net_virt_to_phys((uintptr_t)g_tx_buffer);
    g_txq.desc[0].len = (uint32_t)(packet_len + sizeof(*hdr));
    g_txq.desc[0].flags = 0;
    g_txq.desc[0].next = 0;

    uint16_t aidx = g_txq.avail->idx;
    g_txq.avail->ring[aidx % g_txq.qsize] = 0;
    __sync_synchronize();
    g_txq.avail->idx = (uint16_t)(aidx + 1);
    virtq_kick(1);

    uint64_t start = get_tick_count();
    while ((uint16_t)(g_txq.used->idx - g_txq.last_used_idx) == 0) {
        if ((int64_t)(get_tick_count() - start) > (int64_t)timer_get_hz()) return -1;
    }
    g_txq.last_used_idx++;
    return 0;
}

void virtio_net_poll_rx(void) {
    while ((uint16_t)(g_rxq.used->idx - g_rxq.last_used_idx) != 0) {
        uint16_t uidx = (uint16_t)(g_rxq.last_used_idx % g_rxq.qsize);
        virtq_used_elem_t e = g_rxq.used->ring[uidx];
        g_rxq.last_used_idx++;

        uint16_t id = (uint16_t)e.id;
        uint32_t len = e.len;
        if (id < g_rx_count && g_rx_buffers[id]) {
            if (len > sizeof(virtio_net_hdr_t)) {
                uint8_t* buf = g_rx_buffers[id];
                uint16_t frame_len = (uint16_t)(len - sizeof(virtio_net_hdr_t));
                net_process_rx_frame(buf + sizeof(virtio_net_hdr_t), frame_len);
            }
            rx_post_descriptor(id);
        }
    }
    virtq_kick(0);
}











