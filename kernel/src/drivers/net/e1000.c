#include <net/net_priv.h>

#include <drivers/net/netdrv.h>
#include <drivers/pci/pci.h>
#include <drivers/framebuffer/kprint.h>
#include <interrupt/timer.h>
#include <arch/x86_64/io.h>
#include <mm/kmalloc.h>
#include <lib/string.h>

#define E1000_REG_CTRL 0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_RCTL 0x0100
#define E1000_REG_TCTL 0x0400
#define E1000_REG_TIPG 0x0410
#define E1000_REG_RDBAL 0x2800
#define E1000_REG_RDBAH 0x2804
#define E1000_REG_RDLEN 0x2808
#define E1000_REG_RDH 0x2810
#define E1000_REG_RDT 0x2818
#define E1000_REG_TDBAL 0x3800
#define E1000_REG_TDBAH 0x3804
#define E1000_REG_TDLEN 0x3808
#define E1000_REG_TDH 0x3810
#define E1000_REG_TDT 0x3818
#define E1000_REG_RAL0 0x5400
#define E1000_REG_RAH0 0x5404

#define E1000_CTRL_RST 0x04000000u
#define E1000_CTRL_SLU 0x00000040u
#define E1000_RCTL_EN 0x00000002u
#define E1000_RCTL_BAM 0x00008000u
#define E1000_RCTL_SECRC 0x04000000u
#define E1000_TCTL_EN 0x00000002u
#define E1000_TCTL_PSP 0x00000008u
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12

#define E1000_TXD_CMD_EOP 0x01
#define E1000_TXD_CMD_IFCS 0x02
#define E1000_TXD_CMD_RS 0x08
#define E1000_TXD_STAT_DD 0x01
#define E1000_RXD_STAT_DD 0x01

#define E1000_RX_DESC_COUNT 16
#define E1000_TX_DESC_COUNT 16
#define E1000_RX_BUF_SIZE 2048

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

static volatile uint8_t* g_e1000_mmio = NULL;
static e1000_rx_desc_t* g_e1000_rx_desc = NULL;
static e1000_tx_desc_t* g_e1000_tx_desc = NULL;
static void* g_e1000_rx_desc_base = NULL;
static void* g_e1000_tx_desc_base = NULL;
static uint8_t* g_e1000_rx_bufs[E1000_RX_DESC_COUNT];
static uint8_t* g_e1000_tx_bufs[E1000_TX_DESC_COUNT];
static uint16_t g_e1000_rx_cur = 0;
static uint16_t g_e1000_tx_cur = 0;

static inline uint32_t e1000_read32(uint32_t reg) {
    volatile uint32_t* p = (volatile uint32_t*)(g_e1000_mmio + reg);
    return *p;
}

static inline void e1000_write32(uint32_t reg, uint32_t val) {
    volatile uint32_t* p = (volatile uint32_t*)(g_e1000_mmio + reg);
    *p = val;
}

bool e1000_net_init(void) {
    if ((g_net_state.bar0 & 0x1u) != 0) {
        kprint("[NET] E1000 BAR0 is I/O space (MMIO required)\n");
        return false;
    }
    uintptr_t mmio_phys = (uintptr_t)(g_net_state.bar0 & ~0xFu);
    if (!mmio_phys) {
        kprint("[NET] E1000 MMIO base invalid\n");
        return false;
    }
    g_e1000_mmio = (volatile uint8_t*)net_phys_to_virt(mmio_phys);

    uint16_t cmd = (uint16_t)pci_read_field(g_net_state.bus, g_net_state.device, 0, 0x04, 2);
    cmd |= 0x0006;
    pci_write_field(g_net_state.bus, g_net_state.device, 0, 0x04, 2, cmd);

    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_RST);
    sleep(2);
    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_SLU);

    uint32_t ral = e1000_read32(E1000_REG_RAL0);
    uint32_t rah = e1000_read32(E1000_REG_RAH0);
    g_net_state.mac[0] = (uint8_t)(ral & 0xFF);
    g_net_state.mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    g_net_state.mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    g_net_state.mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    g_net_state.mac[4] = (uint8_t)(rah & 0xFF);
    g_net_state.mac[5] = (uint8_t)((rah >> 8) & 0xFF);
    if ((g_net_state.mac[0] | g_net_state.mac[1] | g_net_state.mac[2] |
         g_net_state.mac[3] | g_net_state.mac[4] | g_net_state.mac[5]) == 0) {
        g_net_state.mac[0] = 0x52;
        g_net_state.mac[1] = 0x54;
        g_net_state.mac[2] = 0x00;
        g_net_state.mac[3] = 0x12;
        g_net_state.mac[4] = 0x34;
        g_net_state.mac[5] = 0x56;
    }
    ral = (uint32_t)g_net_state.mac[0] |
          ((uint32_t)g_net_state.mac[1] << 8) |
          ((uint32_t)g_net_state.mac[2] << 16) |
          ((uint32_t)g_net_state.mac[3] << 24);
    rah = (uint32_t)g_net_state.mac[4] |
          ((uint32_t)g_net_state.mac[5] << 8) |
          0x80000000u;
    e1000_write32(E1000_REG_RAL0, ral);
    e1000_write32(E1000_REG_RAH0, rah);

    uint32_t rx_desc_bytes = (uint32_t)(sizeof(e1000_rx_desc_t) * E1000_RX_DESC_COUNT);
    uint32_t tx_desc_bytes = (uint32_t)(sizeof(e1000_tx_desc_t) * E1000_TX_DESC_COUNT);
    g_e1000_rx_desc_base = kmalloc(rx_desc_bytes + 16);
    g_e1000_tx_desc_base = kmalloc(tx_desc_bytes + 16);
    if (!g_e1000_rx_desc_base || !g_e1000_tx_desc_base) return false;
    g_e1000_rx_desc = (e1000_rx_desc_t*)(((uintptr_t)g_e1000_rx_desc_base + 15u) & ~(uintptr_t)15u);
    g_e1000_tx_desc = (e1000_tx_desc_t*)(((uintptr_t)g_e1000_tx_desc_base + 15u) & ~(uintptr_t)15u);
    memset(g_e1000_rx_desc, 0, rx_desc_bytes);
    memset(g_e1000_tx_desc, 0, tx_desc_bytes);

    for (uint16_t i = 0; i < E1000_RX_DESC_COUNT; ++i) {
        g_e1000_rx_bufs[i] = (uint8_t*)kmalloc(E1000_RX_BUF_SIZE);
        if (!g_e1000_rx_bufs[i]) return false;
        memset(g_e1000_rx_bufs[i], 0, E1000_RX_BUF_SIZE);
        g_e1000_rx_desc[i].addr = (uint64_t)net_virt_to_phys((uintptr_t)g_e1000_rx_bufs[i]);
        g_e1000_rx_desc[i].status = 0;
    }

    for (uint16_t i = 0; i < E1000_TX_DESC_COUNT; ++i) {
        g_e1000_tx_bufs[i] = (uint8_t*)kmalloc(E1000_RX_BUF_SIZE);
        if (!g_e1000_tx_bufs[i]) return false;
        memset(g_e1000_tx_bufs[i], 0, E1000_RX_BUF_SIZE);
        g_e1000_tx_desc[i].addr = (uint64_t)net_virt_to_phys((uintptr_t)g_e1000_tx_bufs[i]);
        g_e1000_tx_desc[i].status = E1000_TXD_STAT_DD;
    }

    e1000_write32(E1000_REG_RDBAL, (uint32_t)net_virt_to_phys((uintptr_t)g_e1000_rx_desc));
    e1000_write32(E1000_REG_RDBAH, (uint32_t)(net_virt_to_phys((uintptr_t)g_e1000_rx_desc) >> 32));
    e1000_write32(E1000_REG_RDLEN, rx_desc_bytes);
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);
    g_e1000_rx_cur = 0;

    e1000_write32(E1000_REG_TDBAL, (uint32_t)net_virt_to_phys((uintptr_t)g_e1000_tx_desc));
    e1000_write32(E1000_REG_TDBAH, (uint32_t)(net_virt_to_phys((uintptr_t)g_e1000_tx_desc) >> 32));
    e1000_write32(E1000_REG_TDLEN, tx_desc_bytes);
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    g_e1000_tx_cur = 0;

    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    e1000_write32(E1000_REG_RCTL, rctl);

    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << E1000_TCTL_CT_SHIFT) | (0x40u << E1000_TCTL_COLD_SHIFT);
    e1000_write32(E1000_REG_TCTL, tctl);
    e1000_write32(E1000_REG_TIPG, 0x0060200Au);

    kprint("[NET] E1000 ready mac=");
    for (int i = 0; i < 6; ++i) {
        if (i) kprint(":");
        kprinthex(g_net_state.mac[i]);
    }
    kprint("\n");
    return true;
}

int e1000_net_tx_raw(const void* packet, uint16_t packet_len) {
    if (packet_len > E1000_RX_BUF_SIZE) return -1;
    uint16_t idx = g_e1000_tx_cur;
    e1000_tx_desc_t* desc = &g_e1000_tx_desc[idx];
    uint8_t* buf = g_e1000_tx_bufs[idx];
    if (!buf) return -1;
    memcpy(buf, packet, packet_len);
    desc->length = packet_len;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    __sync_synchronize();
    g_e1000_tx_cur = (uint16_t)((idx + 1) % E1000_TX_DESC_COUNT);
    e1000_write32(E1000_REG_TDT, g_e1000_tx_cur);
    uint64_t start = get_tick_count();
    while (!(desc->status & E1000_TXD_STAT_DD)) {
        if ((int64_t)(get_tick_count() - start) > (int64_t)timer_get_hz()) return -1;
    }
    return 0;
}

void e1000_net_poll_rx(void) {
    if (!g_e1000_mmio || !g_e1000_rx_desc) return;
    for (;;) {
        e1000_rx_desc_t* desc = &g_e1000_rx_desc[g_e1000_rx_cur];
        if (!(desc->status & E1000_RXD_STAT_DD)) break;
        uint16_t len = desc->length;
        if (len > 0 && g_e1000_rx_bufs[g_e1000_rx_cur]) {
            net_process_rx_frame(g_e1000_rx_bufs[g_e1000_rx_cur], len);
        }
        desc->status = 0;
        e1000_write32(E1000_REG_RDT, g_e1000_rx_cur);
        g_e1000_rx_cur = (uint16_t)((g_e1000_rx_cur + 1) % E1000_RX_DESC_COUNT);
    }
}
