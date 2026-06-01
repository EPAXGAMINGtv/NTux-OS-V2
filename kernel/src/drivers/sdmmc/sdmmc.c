#include <drivers/sdmmc/sdmmc.h>


#include <drivers/framebuffer/kprint.h>
#include <drivers/pci/pci.h>
#include <lib/string.h>

#define SDHCI_MAX_HOSTS 4
#define SDHCI_TIMEOUT_SPINS 2000000

#define PCI_CLASS_SYSTEM_PERIPHERAL 0x08
#define PCI_SUBCLASS_SD_HOST 0x05

#define PCI_COMMAND_REG 0x04
#define PCI_COMMAND_MEM 0x02
#define PCI_COMMAND_BUS_MASTER 0x04

/* SDHCI registers */
#define SDHCI_DMA_ADDRESS       0x00
#define SDHCI_BLOCK_SIZE        0x04
#define SDHCI_BLOCK_COUNT       0x06
#define SDHCI_ARGUMENT          0x08
#define SDHCI_TRANSFER_MODE     0x0C
#define SDHCI_COMMAND           0x0E
#define SDHCI_RESPONSE0         0x10
#define SDHCI_RESPONSE1         0x14
#define SDHCI_RESPONSE2         0x18
#define SDHCI_RESPONSE3         0x1C
#define SDHCI_BUFFER            0x20
#define SDHCI_PRESENT_STATE     0x24
#define SDHCI_HOST_CONTROL      0x28
#define SDHCI_POWER_CONTROL     0x29
#define SDHCI_BLOCK_GAP_CONTROL 0x2A
#define SDHCI_WAKE_UP_CONTROL   0x2B
#define SDHCI_CLOCK_CONTROL     0x2C
#define SDHCI_TIMEOUT_CONTROL   0x2E
#define SDHCI_SOFTWARE_RESET    0x2F
#define SDHCI_INT_STATUS        0x30
#define SDHCI_INT_ENABLE        0x34
#define SDHCI_SIGNAL_ENABLE     0x38
#define SDHCI_CAPABILITIES      0x40
#define SDHCI_CAPABILITIES_1    0x44
#define SDHCI_HOST_CONTROL2     0x3E

/* Present state bits */
#define SDHCI_CMD_INHIBIT       (1u << 0)
#define SDHCI_DAT_INHIBIT       (1u << 1)
#define SDHCI_CARD_INSERTED     (1u << 16)
#define SDHCI_BUFFER_READ_READY (1u << 11)
#define SDHCI_BUFFER_WRITE_READY (1u << 10)

/* Host control */
#define SDHCI_CTRL_4BIT         (1u << 1)
#define SDHCI_CTRL_HISPD        (1u << 2)

/* Power control */
#define SDHCI_POWER_ON          0x01
#define SDHCI_POWER_330         0x0E
#define SDHCI_POWER_300         0x0C
#define SDHCI_POWER_180         0x0A

/* Clock control */
#define SDHCI_CLOCK_INT_EN      (1u << 0)
#define SDHCI_CLOCK_INT_STABLE  (1u << 1)
#define SDHCI_CLOCK_CARD_EN     (1u << 2)

/* Reset */
#define SDHCI_RESET_ALL         0x01
#define SDHCI_RESET_CMD         0x02
#define SDHCI_RESET_DATA        0x04

/* Interrupts */
#define SDHCI_INT_CMD_COMPLETE  0x00000001u
#define SDHCI_INT_XFER_COMPLETE 0x00000002u
#define SDHCI_INT_BUF_WRITE     0x00000010u
#define SDHCI_INT_BUF_READ      0x00000020u
#define SDHCI_INT_ERROR         0x00008000u

/* Transfer mode */
#define SDHCI_TRNS_DMA          0x0001u
#define SDHCI_TRNS_BLK_CNT_EN   0x0002u
#define SDHCI_TRNS_AUTO_CMD12   0x0004u
#define SDHCI_TRNS_READ         0x0010u
#define SDHCI_TRNS_MULTI        0x0020u

/* Command flags */
#define SDHCI_CMD_RESP_NONE     0x0000u
#define SDHCI_CMD_RESP_LONG     0x0001u
#define SDHCI_CMD_RESP_SHORT    0x0002u
#define SDHCI_CMD_RESP_SHORT_BUSY 0x0003u
#define SDHCI_CMD_CRC           0x0008u
#define SDHCI_CMD_INDEX         0x0010u
#define SDHCI_CMD_DATA          0x0020u

#define SDHCI_HCTL2_UHS_MASK    0x0007u
#define SDHCI_HCTL2_1V8         0x0008u
#define SDHCI_HCTL2_EXEC_TUNING (1u << 6)
#define SDHCI_HCTL2_SAMPLING_CLK (1u << 7)
#define SDHCI_UHS_SDR12         0x0u
#define SDHCI_UHS_SDR25         0x1u
#define SDHCI_UHS_SDR50         0x2u
#define SDHCI_UHS_SDR104        0x3u
#define SDHCI_UHS_DDR50         0x4u

#define SDHCI_CAP1_SDR50        (1u << 0)
#define SDHCI_CAP1_SDR104       (1u << 1)
#define SDHCI_CAP1_DDR50        (1u << 2)

#define MMC_OCR_HCS             (1u << 30)
#define MMC_OCR_READY           (1u << 31)

#define SD_OCR_S18R             (1u << 24)
#define SD_OCR_HCS              (1u << 30)
#define SD_OCR_READY            (1u << 31)

#define SDMMC_BLOCK_SIZE 512u

typedef enum {
    SD_RESP_NONE = 0,
    SD_RESP_R1,
    SD_RESP_R1B,
    SD_RESP_R2,
    SD_RESP_R3,
    SD_RESP_R6,
    SD_RESP_R7
} sd_resp_t;

typedef struct {
    bool present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    volatile uint8_t* regs;
    uint32_t caps;
    uint32_t caps1;
    uint32_t base_clock_khz;
    uint32_t rca;
    bool high_capacity;
    uint64_t sectors;
    char model[48];
} sdhci_host_t;

static sdhci_host_t g_hosts[SDHCI_MAX_HOSTS];
static size_t g_host_count;

static inline uint8_t sdhci_read8(sdhci_host_t* h, uint32_t off) {
    return *(volatile uint8_t*)(h->regs + off);
}

static inline uint16_t sdhci_read16(sdhci_host_t* h, uint32_t off) {
    return *(volatile uint16_t*)(h->regs + off);
}

static inline uint32_t sdhci_read32(sdhci_host_t* h, uint32_t off) {
    return *(volatile uint32_t*)(h->regs + off);
}

static inline void sdhci_write8(sdhci_host_t* h, uint32_t off, uint8_t val) {
    *(volatile uint8_t*)(h->regs + off) = val;
}

static inline void sdhci_write16(sdhci_host_t* h, uint32_t off, uint16_t val) {
    *(volatile uint16_t*)(h->regs + off) = val;
}

static inline void sdhci_write32(sdhci_host_t* h, uint32_t off, uint32_t val) {
    *(volatile uint32_t*)(h->regs + off) = val;
}

static int sdhci_wait_mask32(sdhci_host_t* h, uint32_t off, uint32_t mask, uint32_t value) {
    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        if ((sdhci_read32(h, off) & mask) == value) return 0;
    }
    return -1;
}

static int sdhci_wait_mask8(sdhci_host_t* h, uint32_t off, uint8_t mask, uint8_t value) {
    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        if ((sdhci_read8(h, off) & mask) == value) return 0;
    }
    return -1;
}

static void sdhci_reset(sdhci_host_t* h, uint8_t mask) {
    sdhci_write8(h, SDHCI_SOFTWARE_RESET, mask);
    (void)sdhci_wait_mask8(h, SDHCI_SOFTWARE_RESET, mask, 0);
}

static void sdhci_set_power(sdhci_host_t* h) {
    uint32_t caps = h->caps;
    uint8_t pwr = 0;
    if (caps & (1u << 26)) pwr = SDHCI_POWER_330;
    else if (caps & (1u << 25)) pwr = SDHCI_POWER_300;
    else if (caps & (1u << 24)) pwr = SDHCI_POWER_180;
    if (pwr) sdhci_write8(h, SDHCI_POWER_CONTROL, pwr | SDHCI_POWER_ON);
}

static void sdhci_set_clock(sdhci_host_t* h, uint32_t target_khz) {
    if (h->base_clock_khz == 0 || target_khz == 0) return;

    uint32_t base = h->base_clock_khz;
    uint32_t div = 1u;
    if (target_khz < base) {
        div = (base + (target_khz * 2u - 1u)) / (target_khz * 2u);
        if (div == 0) div = 1u;
        if (div > 0x3FFu) div = 0x3FFu;
    }

    uint16_t clk = 0;
    clk |= SDHCI_CLOCK_INT_EN;
    clk |= (uint16_t)((div & 0xFFu) << 8);
    clk |= (uint16_t)((div & 0x300u) >> 2);

    sdhci_write16(h, SDHCI_CLOCK_CONTROL, 0);
    sdhci_write16(h, SDHCI_CLOCK_CONTROL, clk);
    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        if (sdhci_read16(h, SDHCI_CLOCK_CONTROL) & SDHCI_CLOCK_INT_STABLE) break;
    }
    sdhci_write16(h, SDHCI_CLOCK_CONTROL, clk | SDHCI_CLOCK_CARD_EN);
}

static uint16_t sdhci_cmd_flags(sd_resp_t resp, int data_present) {
    uint16_t flags = 0;
    switch (resp) {
        case SD_RESP_NONE:
            flags |= SDHCI_CMD_RESP_NONE;
            break;
        case SD_RESP_R2:
            flags |= SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
            break;
        case SD_RESP_R3:
            flags |= SDHCI_CMD_RESP_SHORT;
            break;
        case SD_RESP_R1B:
            flags |= SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
            break;
        case SD_RESP_R1:
        case SD_RESP_R6:
        case SD_RESP_R7:
        default:
            flags |= SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
            break;
    }
    if (data_present) flags |= SDHCI_CMD_DATA;
    return flags;
}

static int sdhci_send_cmd(sdhci_host_t* h, uint8_t cmd, uint32_t arg, sd_resp_t resp, int data_present, uint32_t resp_out[4]) {
    if (!h) return -1;
    if (sdhci_wait_mask32(h, SDHCI_PRESENT_STATE, SDHCI_CMD_INHIBIT, 0) != 0) return -2;
    if (data_present) {
        if (sdhci_wait_mask32(h, SDHCI_PRESENT_STATE, SDHCI_DAT_INHIBIT, 0) != 0) return -3;
    }

    sdhci_write32(h, SDHCI_INT_STATUS, 0xFFFFFFFFu);
    sdhci_write32(h, SDHCI_ARGUMENT, arg);

    uint16_t cmd_flags = sdhci_cmd_flags(resp, data_present);
    uint16_t cmd_reg = (uint16_t)((uint16_t)cmd << 8) | cmd_flags;
    sdhci_write16(h, SDHCI_COMMAND, cmd_reg);

    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        uint32_t status = sdhci_read32(h, SDHCI_INT_STATUS);
        if (status & SDHCI_INT_ERROR) return -4;
        if (status & SDHCI_INT_CMD_COMPLETE) break;
    }

    if (resp_out) {
        resp_out[0] = sdhci_read32(h, SDHCI_RESPONSE0);
        resp_out[1] = sdhci_read32(h, SDHCI_RESPONSE1);
        resp_out[2] = sdhci_read32(h, SDHCI_RESPONSE2);
        resp_out[3] = sdhci_read32(h, SDHCI_RESPONSE3);
    }
    sdhci_write32(h, SDHCI_INT_STATUS, SDHCI_INT_CMD_COMPLETE);
    return 0;
}

static uint32_t sdhci_resp_bits(const uint32_t resp[4], int msb, int lsb) {
    uint32_t val = 0;
    for (int bit = msb; bit >= lsb; --bit) {
        int idx = bit / 32;
        int off = bit % 32;
        uint32_t word = resp[idx];
        uint32_t b = (word >> off) & 1u;
        val = (val << 1) | b;
    }
    return val;
}

static uint64_t sdhci_calc_capacity(const uint32_t resp[4], bool* out_high_capacity) {
    uint32_t csd_structure = sdhci_resp_bits(resp, 127, 126);
    if (csd_structure == 1) {
        uint32_t c_size = sdhci_resp_bits(resp, 69, 48);
        uint64_t capacity = ((uint64_t)(c_size + 1u)) * 512ull * 1024ull;
        if (out_high_capacity) *out_high_capacity = true;
        return capacity;
    }

    uint32_t read_bl_len = sdhci_resp_bits(resp, 83, 80);
    uint32_t c_size = sdhci_resp_bits(resp, 73, 62);
    uint32_t c_size_mult = sdhci_resp_bits(resp, 49, 47);
    uint64_t block_len = 1ull << read_bl_len;
    uint64_t mult = 1ull << (c_size_mult + 2u);
    uint64_t capacity = (uint64_t)(c_size + 1u) * mult * block_len;
    if (out_high_capacity) *out_high_capacity = false;
    return capacity;
}

static uint64_t mmc_calc_capacity_bytes(const uint32_t resp[4]) {
    uint32_t read_bl_len = sdhci_resp_bits(resp, 83, 80);
    uint32_t c_size = sdhci_resp_bits(resp, 73, 62);
    uint32_t c_size_mult = sdhci_resp_bits(resp, 49, 47);
    uint64_t block_len = 1ull << read_bl_len;
    uint64_t mult = 1ull << (c_size_mult + 2u);
    return (uint64_t)(c_size + 1u) * mult * block_len;
}

static int sdhci_switch_mmc(sdhci_host_t* h, uint8_t index, uint8_t value) {
    if (!h) return -1;
    uint32_t arg = (0x3u << 24) | ((uint32_t)index << 16) | ((uint32_t)value << 8);
    uint32_t resp[4];
    return sdhci_send_cmd(h, 6, arg, SD_RESP_R1B, 0, resp);
}

static int sdhci_try_uhs_sdr50(sdhci_host_t* h, uint32_t ocr) {
    if (!h) return -1;
    if (!(h->caps1 & SDHCI_CAP1_SDR50)) return -1;
    if (!(ocr & SD_OCR_S18R)) return -1;

    uint32_t resp[4];
    if (sdhci_send_cmd(h, 11, 0, SD_RESP_R1, 0, resp) != 0) return -1;

    uint16_t hctl2 = sdhci_read16(h, SDHCI_HOST_CONTROL2);
    hctl2 &= ~SDHCI_HCTL2_UHS_MASK;
    hctl2 |= SDHCI_UHS_SDR50;
    hctl2 |= SDHCI_HCTL2_1V8;
    sdhci_write16(h, SDHCI_HOST_CONTROL2, hctl2);

    sdhci_write8(h, SDHCI_POWER_CONTROL, SDHCI_POWER_180 | SDHCI_POWER_ON);
    sdhci_set_clock(h, 50000);
    return 0;
}

static int sdhci_try_uhs_ddr50(sdhci_host_t* h, uint32_t ocr) {
    if (!h) return -1;
    if (!(h->caps1 & SDHCI_CAP1_DDR50)) return -1;
    if (!(ocr & SD_OCR_S18R)) return -1;

    uint16_t hctl2 = sdhci_read16(h, SDHCI_HOST_CONTROL2);
    hctl2 &= ~SDHCI_HCTL2_UHS_MASK;
    hctl2 |= SDHCI_UHS_DDR50;
    hctl2 |= SDHCI_HCTL2_1V8;
    sdhci_write16(h, SDHCI_HOST_CONTROL2, hctl2);

    sdhci_write8(h, SDHCI_POWER_CONTROL, SDHCI_POWER_180 | SDHCI_POWER_ON);
    sdhci_set_clock(h, 50000);
    return 0;
}

static int sdhci_tuning_cmd19(sdhci_host_t* h);

static int sdhci_try_uhs_sdr104(sdhci_host_t* h, uint32_t ocr) {
    if (!h) return -1;
    if (!(h->caps1 & SDHCI_CAP1_SDR104)) return -1;
    if (!(ocr & SD_OCR_S18R)) return -1;

    uint16_t hctl2 = sdhci_read16(h, SDHCI_HOST_CONTROL2);
    hctl2 &= ~SDHCI_HCTL2_UHS_MASK;
    hctl2 |= SDHCI_UHS_SDR104;
    hctl2 |= SDHCI_HCTL2_1V8;
    sdhci_write16(h, SDHCI_HOST_CONTROL2, hctl2);

    sdhci_write8(h, SDHCI_POWER_CONTROL, SDHCI_POWER_180 | SDHCI_POWER_ON);
    sdhci_set_clock(h, 100000);
    if (sdhci_tuning_cmd19(h) != 0) return -2;
    return 0;
}

static int sdhci_wait_data_ready(sdhci_host_t* h, uint32_t mask) {
    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        uint32_t status = sdhci_read32(h, SDHCI_PRESENT_STATE);
        if (status & mask) return 0;
    }
    return -1;
}

static int sdhci_read_data_cmd(sdhci_host_t* h, uint8_t cmd, uint32_t arg, void* out) {
    if (!h || !out) return -1;
    sdhci_write16(h, SDHCI_BLOCK_SIZE, SDMMC_BLOCK_SIZE);
    sdhci_write16(h, SDHCI_BLOCK_COUNT, 1u);
    sdhci_write16(h, SDHCI_TRANSFER_MODE, SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);
    uint32_t resp[4];
    if (sdhci_send_cmd(h, cmd, arg, SD_RESP_R1, 1, resp) != 0) return -2;
    if (sdhci_wait_data_ready(h, SDHCI_BUFFER_READ_READY) != 0) return -3;
    uint32_t* out32 = (uint32_t*)out;
    for (uint32_t i = 0; i < (SDMMC_BLOCK_SIZE / 4u); ++i) {
        out32[i] = sdhci_read32(h, SDHCI_BUFFER);
    }
    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        uint32_t status = sdhci_read32(h, SDHCI_INT_STATUS);
        if (status & SDHCI_INT_ERROR) return -4;
        if (status & SDHCI_INT_XFER_COMPLETE) break;
    }
    sdhci_write32(h, SDHCI_INT_STATUS, SDHCI_INT_XFER_COMPLETE);
    return 0;
}

static int sdhci_tuning_cmd19(sdhci_host_t* h) {
    if (!h) return -1;
    uint8_t buf[64];
    uint16_t hctl2 = sdhci_read16(h, SDHCI_HOST_CONTROL2);
    hctl2 |= SDHCI_HCTL2_EXEC_TUNING;
    sdhci_write16(h, SDHCI_HOST_CONTROL2, hctl2);

    for (int i = 0; i < 40; ++i) {
        sdhci_write16(h, SDHCI_BLOCK_SIZE, 64u);
        sdhci_write16(h, SDHCI_BLOCK_COUNT, 1u);
        sdhci_write16(h, SDHCI_TRANSFER_MODE, SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);
        uint32_t resp[4];
        if (sdhci_send_cmd(h, 19, 0, SD_RESP_R1, 1, resp) != 0) continue;
        if (sdhci_wait_data_ready(h, SDHCI_BUFFER_READ_READY) == 0) {
            uint32_t* out32 = (uint32_t*)buf;
            for (int w = 0; w < 64 / 4; ++w) {
                out32[w] = sdhci_read32(h, SDHCI_BUFFER);
            }
        }
        uint16_t st = sdhci_read16(h, SDHCI_HOST_CONTROL2);
        if (!(st & SDHCI_HCTL2_EXEC_TUNING)) {
            if (st & SDHCI_HCTL2_SAMPLING_CLK) return 0;
            return -2;
        }
    }
    return -3;
}

static int sdhci_read_block(sdhci_host_t* h, uint64_t lba, void* out) {
    if (!h || !out) return -1;
    uint32_t arg = h->high_capacity ? (uint32_t)lba : (uint32_t)(lba * SDMMC_BLOCK_SIZE);

    sdhci_write16(h, SDHCI_BLOCK_SIZE, SDMMC_BLOCK_SIZE);
    sdhci_write16(h, SDHCI_BLOCK_COUNT, 1u);
    sdhci_write16(h, SDHCI_TRANSFER_MODE, SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);

    uint32_t resp[4];
    if (sdhci_send_cmd(h, 17, arg, SD_RESP_R1, 1, resp) != 0) return -2;

    if (sdhci_wait_data_ready(h, SDHCI_BUFFER_READ_READY) != 0) return -3;
    uint32_t* out32 = (uint32_t*)out;
    for (size_t i = 0; i < (SDMMC_BLOCK_SIZE / 4u); ++i) {
        out32[i] = sdhci_read32(h, SDHCI_BUFFER);
    }

    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        uint32_t status = sdhci_read32(h, SDHCI_INT_STATUS);
        if (status & SDHCI_INT_ERROR) return -4;
        if (status & SDHCI_INT_XFER_COMPLETE) break;
    }
    sdhci_write32(h, SDHCI_INT_STATUS, SDHCI_INT_XFER_COMPLETE);
    return 0;
}

static int sdhci_write_block(sdhci_host_t* h, uint64_t lba, const void* in) {
    if (!h || !in) return -1;
    uint32_t arg = h->high_capacity ? (uint32_t)lba : (uint32_t)(lba * SDMMC_BLOCK_SIZE);

    sdhci_write16(h, SDHCI_BLOCK_SIZE, SDMMC_BLOCK_SIZE);
    sdhci_write16(h, SDHCI_BLOCK_COUNT, 1u);
    sdhci_write16(h, SDHCI_TRANSFER_MODE, SDHCI_TRNS_BLK_CNT_EN);

    uint32_t resp[4];
    if (sdhci_send_cmd(h, 24, arg, SD_RESP_R1, 1, resp) != 0) return -2;

    if (sdhci_wait_data_ready(h, SDHCI_BUFFER_WRITE_READY) != 0) return -3;
    const uint32_t* in32 = (const uint32_t*)in;
    for (size_t i = 0; i < (SDMMC_BLOCK_SIZE / 4u); ++i) {
        sdhci_write32(h, SDHCI_BUFFER, in32[i]);
    }

    for (int i = 0; i < SDHCI_TIMEOUT_SPINS; ++i) {
        uint32_t status = sdhci_read32(h, SDHCI_INT_STATUS);
        if (status & SDHCI_INT_ERROR) return -4;
        if (status & SDHCI_INT_XFER_COMPLETE) break;
    }
    sdhci_write32(h, SDHCI_INT_STATUS, SDHCI_INT_XFER_COMPLETE);
    return 0;
}

static int sdhci_app_cmd(sdhci_host_t* h, uint32_t rca) {
    uint32_t resp[4];
    return sdhci_send_cmd(h, 55, rca << 16, SD_RESP_R1, 0, resp);
}

static int sdhci_card_init_sd(sdhci_host_t* h) {
    if (!h) return -1;
    if (!(sdhci_read32(h, SDHCI_PRESENT_STATE) & SDHCI_CARD_INSERTED)) return -1;

    sdhci_reset(h, SDHCI_RESET_ALL);
    sdhci_set_power(h);
    sdhci_write8(h, SDHCI_TIMEOUT_CONTROL, 0x0E);
    sdhci_set_clock(h, 400);

    (void)sdhci_send_cmd(h, 0, 0, SD_RESP_NONE, 0, NULL);

    uint32_t resp[4] = {0};
    int v2 = 0;
    if (sdhci_send_cmd(h, 8, 0x1AA, SD_RESP_R7, 0, resp) == 0) {
        if ((resp[0] & 0xFFFu) == 0x1AAu) v2 = 1;
    }

    uint32_t ocr = 0;
    uint32_t ocr_arg = (v2 ? (SD_OCR_HCS | SD_OCR_S18R) : 0) | 0x00300000u;
    for (int i = 0; i < 1000; ++i) {
        (void)sdhci_app_cmd(h, 0);
        if (sdhci_send_cmd(h, 41, ocr_arg, SD_RESP_R3, 0, resp) != 0) continue;
        ocr = resp[0];
        if (ocr & SD_OCR_READY) break;
    }
    if (!(ocr & SD_OCR_READY)) return -2;

    h->high_capacity = (ocr & SD_OCR_HCS) ? true : false;

    if (sdhci_send_cmd(h, 2, 0, SD_RESP_R2, 0, resp) != 0) return -3;
    if (sdhci_send_cmd(h, 3, 0, SD_RESP_R6, 0, resp) != 0) return -4;
    h->rca = (resp[0] >> 16) & 0xFFFFu;

    if (sdhci_send_cmd(h, 9, h->rca << 16, SD_RESP_R2, 0, resp) != 0) return -5;
    bool csd_hc = h->high_capacity;
    uint64_t capacity = sdhci_calc_capacity(resp, &csd_hc);
    h->high_capacity = csd_hc;
    h->sectors = capacity / SDMMC_BLOCK_SIZE;

    if (sdhci_send_cmd(h, 7, h->rca << 16, SD_RESP_R1B, 0, resp) != 0) return -6;

    (void)sdhci_app_cmd(h, h->rca);
    (void)sdhci_send_cmd(h, 6, 2, SD_RESP_R1, 0, resp);

    uint8_t hc = sdhci_read8(h, SDHCI_HOST_CONTROL);
    hc |= SDHCI_CTRL_4BIT;
    hc |= SDHCI_CTRL_HISPD;
    sdhci_write8(h, SDHCI_HOST_CONTROL, hc);

    sdhci_set_clock(h, 25000);

    if (!h->high_capacity) {
        (void)sdhci_send_cmd(h, 16, SDMMC_BLOCK_SIZE, SD_RESP_R1, 0, resp);
    }

    strncpy(h->model, h->high_capacity ? "SDHC/SDXC" : "SDSC", sizeof(h->model) - 1);
    h->model[sizeof(h->model) - 1] = '\0';
    if (sdhci_try_uhs_sdr104(h, ocr) != 0) {
        if (sdhci_try_uhs_ddr50(h, ocr) != 0) {
            (void)sdhci_try_uhs_sdr50(h, ocr);
        }
    }
    return 0;
}

static int sdhci_card_init_mmc(sdhci_host_t* h) {
    if (!h) return -1;
    if (!(sdhci_read32(h, SDHCI_PRESENT_STATE) & SDHCI_CARD_INSERTED)) return -1;

    sdhci_reset(h, SDHCI_RESET_ALL);
    sdhci_set_power(h);
    sdhci_write8(h, SDHCI_TIMEOUT_CONTROL, 0x0E);
    sdhci_set_clock(h, 400);

    (void)sdhci_send_cmd(h, 0, 0, SD_RESP_NONE, 0, NULL);

    uint32_t resp[4] = {0};
    uint32_t ocr = 0;
    for (int i = 0; i < 1000; ++i) {
        if (sdhci_send_cmd(h, 1, MMC_OCR_HCS | 0x00FF8000u, SD_RESP_R3, 0, resp) != 0) continue;
        ocr = resp[0];
        if (ocr & MMC_OCR_READY) break;
    }
    if (!(ocr & MMC_OCR_READY)) return -2;

    h->high_capacity = (ocr & MMC_OCR_HCS) ? true : false;

    if (sdhci_send_cmd(h, 2, 0, SD_RESP_R2, 0, resp) != 0) return -3;

    uint32_t rca = 1u;
    if (sdhci_send_cmd(h, 3, rca << 16, SD_RESP_R1, 0, resp) != 0) return -4;
    h->rca = rca;

    if (sdhci_send_cmd(h, 9, h->rca << 16, SD_RESP_R2, 0, resp) != 0) return -5;

    uint64_t capacity = 0;
    if (h->high_capacity) {
        uint8_t ext_csd[512];
        if (sdhci_read_data_cmd(h, 8, 0, ext_csd) == 0) {
            uint32_t sec = (uint32_t)ext_csd[212] |
                           ((uint32_t)ext_csd[213] << 8) |
                           ((uint32_t)ext_csd[214] << 16) |
                           ((uint32_t)ext_csd[215] << 24);
            capacity = (uint64_t)sec * 512ull;
        }
    }
    if (capacity == 0) {
        capacity = mmc_calc_capacity_bytes(resp);
    }
    h->sectors = capacity / SDMMC_BLOCK_SIZE;

    if (sdhci_send_cmd(h, 7, h->rca << 16, SD_RESP_R1B, 0, resp) != 0) return -6;

    (void)sdhci_switch_mmc(h, 185, 1);
    (void)sdhci_switch_mmc(h, 183, 1);

    uint8_t hc = sdhci_read8(h, SDHCI_HOST_CONTROL);
    hc |= SDHCI_CTRL_4BIT;
    hc |= SDHCI_CTRL_HISPD;
    sdhci_write8(h, SDHCI_HOST_CONTROL, hc);
    sdhci_set_clock(h, 50000);

    if (!h->high_capacity) {
        (void)sdhci_send_cmd(h, 16, SDMMC_BLOCK_SIZE, SD_RESP_R1, 0, resp);
    }

    strncpy(h->model, h->high_capacity ? "MMC/eMMC HC" : "MMC/eMMC", sizeof(h->model) - 1);
    h->model[sizeof(h->model) - 1] = '\0';
    return 0;
}

static void sdhci_probe_device(uint32_t bus, uint32_t device, uint32_t function, uint16_t vendor, uint16_t device_id, void* extra) {
    (void)vendor;
    (void)device_id;
    (void)extra;

    if (g_host_count >= SDHCI_MAX_HOSTS) return;

    uint8_t class_code = (uint8_t)pci_read_field(bus, device, function, 0x0B, 1);
    uint8_t subclass = (uint8_t)pci_read_field(bus, device, function, 0x0A, 1);
    if (class_code != PCI_CLASS_SYSTEM_PERIPHERAL || subclass != PCI_SUBCLASS_SD_HOST) return;

    uint32_t bar0 = pci_read_field(bus, device, function, PCI_BAR0, 4);
    if (bar0 & 0x01u) return; /* IO space */
    uintptr_t mmio_phys = (uintptr_t)(bar0 & 0xFFFFFFF0u);
    if (!mmio_phys) return;

    uint32_t cmd = pci_read_field(bus, device, function, PCI_COMMAND_REG, 2);
    cmd |= (PCI_COMMAND_MEM | PCI_COMMAND_BUS_MASTER);
    pci_write_field(bus, device, function, PCI_COMMAND_REG, 2, cmd);

    sdhci_host_t* h = &g_hosts[g_host_count];
    memset(h, 0, sizeof(*h));
    h->bus = (uint8_t)bus;
    h->dev = (uint8_t)device;
    h->func = (uint8_t)function;
    h->regs = (volatile uint8_t*)mmio_phys;
    h->caps = sdhci_read32(h, SDHCI_CAPABILITIES);
    h->caps1 = sdhci_read32(h, SDHCI_CAPABILITIES_1);

    uint32_t base_mhz = (h->caps >> 8) & 0xFFu;
    h->base_clock_khz = base_mhz ? base_mhz * 1000u : 0u;

    if (sdhci_card_init_sd(h) != 0) {
        if (sdhci_card_init_mmc(h) != 0) return;
    }

    h->present = true;
    g_host_count++;
}

void sdmmc_rescan(bool verbose) {
    memset(g_hosts, 0, sizeof(g_hosts));
    g_host_count = 0;

    pci_scan_ex(sdhci_probe_device, NULL);

    static const int g_sdmmc_log_enabled = 0;
    if (verbose && g_sdmmc_log_enabled) {
        kprint("[SDMMC] devices: ");
        kprint_int((int)g_host_count);
        kprint("\n");
        for (size_t i = 0; i < g_host_count; ++i) {
            sdhci_host_t* h = &g_hosts[i];
            if (!h->present) continue;
            kprint("[SDMMC] sd#");
            kprint_int((int)i);
            kprint(" model=");
            kprint(h->model[0] ? h->model : "SD Card");
            kprint(" sectors=");
            kprint_uint((uint32_t)h->sectors);
            kprint("\n");
        }
    }
}

void sdmmc_init(void) {
    sdmmc_rescan(true);
}

size_t sdmmc_device_count(void) {
    return g_host_count;
}

int sdmmc_get_info(size_t index, sdmmc_info_t* out) {
    if (!out || index >= g_host_count) return -1;
    sdhci_host_t* h = &g_hosts[index];
    if (!h->present) return -1;
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->bus = h->bus;
    out->device = h->dev;
    out->function = h->func;
    out->high_capacity = h->high_capacity ? 1u : 0u;
    out->rca = h->rca;
    out->sectors = h->sectors;
    strncpy(out->model, h->model[0] ? h->model : "SD Card", sizeof(out->model) - 1);
    out->model[sizeof(out->model) - 1] = '\0';
    return 0;
}

int sdmmc_read_sectors(size_t index, uint64_t lba, uint32_t count, void* out) {
    if (!out || count == 0) return -1;
    if (index >= g_host_count) return -1;
    sdhci_host_t* h = &g_hosts[index];
    if (!h->present || h->sectors == 0) return -1;
    if (lba >= h->sectors || lba + count > h->sectors) return -1;

    uint8_t* outb = (uint8_t*)out;
    for (uint32_t i = 0; i < count; ++i) {
        if (sdhci_read_block(h, lba + i, outb + (size_t)i * SDMMC_BLOCK_SIZE) != 0) return -2;
    }
    return 0;
}

int sdmmc_write_sectors(size_t index, uint64_t lba, uint32_t count, const void* in) {
    if (!in || count == 0) return -1;
    if (index >= g_host_count) return -1;
    sdhci_host_t* h = &g_hosts[index];
    if (!h->present || h->sectors == 0) return -1;
    if (lba >= h->sectors || lba + count > h->sectors) return -1;

    const uint8_t* inb = (const uint8_t*)in;
    for (uint32_t i = 0; i < count; ++i) {
        if (sdhci_write_block(h, lba + i, inb + (size_t)i * SDMMC_BLOCK_SIZE) != 0) return -2;
    }
    return 0;
}
