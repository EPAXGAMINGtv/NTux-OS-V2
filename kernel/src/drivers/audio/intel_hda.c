#include <drivers/audio/intel_hda.h>
#include <drivers/framebuffer/kprint.h>
#include <drivers/pci/pci.h>
#include <mm/pmm.h>
#include <mm/hhdm.h>
#include <mm/paging.h>
#include <lib/string.h>
#include <sched/thread.h>
#include <interrupt/irq.h>
#include <interrupt/pic.h>

static volatile uint8_t* g_hda_mmio = 0;
static uint8_t g_hda_irq = 0;
static int g_hda_stream = -1;
static volatile int g_audio_playing = 0;

/* DMA buffers */
static uint8_t* g_dma_buf_virt[HDA_NUM_BDL];
static uintptr_t g_dma_buf_phys[HDA_NUM_BDL];
static hda_bdl_entry_t* g_bdl_virt = 0;
static uintptr_t g_bdl_phys = 0;
static volatile int g_current_bdl = 0;

/* Ring buffer for userspace audio data */
static int16_t g_audio_ring[AUDIO_RING_BYTES / 2];
static volatile uint32_t g_ring_write = 0;
static volatile uint32_t g_ring_read = 0;
static volatile uint32_t g_ring_count = 0;

static inline uint32_t hda_read32(uint16_t reg) {
    return *(volatile uint32_t*)(g_hda_mmio + reg);
}
static inline uint16_t hda_read16(uint16_t reg) {
    return *(volatile uint16_t*)(g_hda_mmio + reg);
}
static inline uint8_t hda_read8(uint16_t reg) {
    return *(volatile uint8_t*)(g_hda_mmio + reg);
}
static inline void hda_write32(uint16_t reg, uint32_t v) {
    *(volatile uint32_t*)(g_hda_mmio + reg) = v;
}
static inline void hda_write16(uint16_t reg, uint16_t v) {
    *(volatile uint16_t*)(g_hda_mmio + reg) = v;
}
static inline void hda_write8(uint16_t reg, uint8_t v) {
    *(volatile uint8_t*)(g_hda_mmio + reg) = v;
}

static inline uintptr_t hda_read_base32(uint16_t low_reg, uint16_t high_reg) {
    uint64_t low  = (uint64_t)(uint32_t)hda_read32(low_reg);
    uint64_t high = (uint64_t)(uint32_t)hda_read32(high_reg);
    return (uintptr_t)((high << 32) | (low & 0xFFFFFF80u));
}

static uint32_t hda_transfer_verb(uint16_t nid, uint32_t verb, uint32_t payload) {
    uint32_t corb_word = ((uint32_t)(nid & 0xFF) << 20) | ((verb & 0xFFF) << 8) | (payload & 0xFF);
    /* read current CORB write position */
    uint16_t wp = hda_read16(HDA_CORBWP);
    uint16_t next_wp = (uint16_t)((wp + 1) & 0xFF);
    uintptr_t corb_base = hda_read_base32(HDA_CORBLBASE, HDA_CORBUBASE);
    hda_corb_entry_t* corb = (hda_corb_entry_t*)(uintptr_t)(hhdm_offset_get() + corb_base);
    corb[wp].verb = corb_word;
    __sync_synchronize();
    hda_write16(HDA_CORBWP, next_wp);

    /* We send one verb at a time; RIRB WP advances by 1 per response.
       Track our read position with a static. */
    static uint8_t rirb_read_pos = 0;
    uint8_t expected_rp = (uint8_t)((rirb_read_pos + 1) & 0xFF);
    uintptr_t rirb_base = hda_read_base32(HDA_RIRBLBASE, HDA_RIRBUBASE);
    hda_rirb_entry_t* rirb = (hda_rirb_entry_t*)(uintptr_t)(hhdm_offset_get() + rirb_base);
    for (int timeout = 0; timeout < 300000; ++timeout) {
        uint8_t rp = (uint8_t)hda_read16(HDA_RIRBWP);
        if (rp == expected_rp) {
            uint32_t resp = rirb[rirb_read_pos].response;
            rirb_read_pos = expected_rp;
            return resp;
        }
        for (volatile int d = 0; d < 10; ++d);
    }
    return 0;
}

static int hda_stream_reset(int sid) {
    uint16_t sdoff = HDA_SD_BASE(sid);
    hda_write32(sdoff + HDA_SD_CTL, hda_read32(sdoff + HDA_SD_CTL) | HDA_SD_CTL_SRST);
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (hda_read32(sdoff + HDA_SD_CTL) & HDA_SD_CTL_SRST) break;
    }
    hda_write32(sdoff + HDA_SD_CTL, hda_read32(sdoff + HDA_SD_CTL) & ~HDA_SD_CTL_SRST);
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (!(hda_read32(sdoff + HDA_SD_CTL) & HDA_SD_CTL_SRST)) break;
    }
    return 0;
}

static void hda_setup_bdl(void) {
    for (int i = 0; i < HDA_NUM_BDL; ++i) {
        g_bdl_virt[i].address = g_dma_buf_phys[i];
        g_bdl_virt[i].length = HDA_BDL_BUF_SIZE;
        g_bdl_virt[i].ioc = HDA_BDL_IOC;
    }
}

static void hda_refill_bdl(int idx) {
    uint8_t* dst = g_dma_buf_virt[idx];
    int samples_per_buf = HDA_BDL_BUF_SIZE / 2;
    for (int i = 0; i < samples_per_buf; ++i) {
        if (g_ring_count > 0) {
            dst[i * 2] = (uint8_t)(g_audio_ring[g_ring_read] & 0xFF);
            dst[i * 2 + 1] = (uint8_t)((g_audio_ring[g_ring_read] >> 8) & 0xFF);
            g_ring_read = (g_ring_read + 1) % (AUDIO_RING_BYTES / 2);
            __atomic_fetch_sub(&g_ring_count, 1, __ATOMIC_RELAXED);
        } else {
            dst[i * 2] = 0;
            dst[i * 2 + 1] = 0;
        }
    }
}

static void hda_irq_handler(void) {
    uint32_t intsts = hda_read32(HDA_INTSTS);
    if (g_hda_stream >= 0) {
        uint16_t sdoff = HDA_SD_BASE(g_hda_stream);
        uint32_t sts = hda_read32(sdoff + HDA_SD_STS);
        if (sts & HDA_SD_STS_BCIS) {
            int completed = g_current_bdl;
            g_current_bdl = (g_current_bdl + 1) % HDA_NUM_BDL;
            hda_refill_bdl(completed);
            hda_write32(sdoff + HDA_SD_STS, sts);
        }
    }
    hda_write32(HDA_INTSTS, intsts);
}

int hda_playback_start(void) {
    if (g_hda_stream < 0 || !g_hda_mmio) return -1;
    int sid = g_hda_stream;
    uint16_t sdoff = HDA_SD_BASE(sid);

    hda_stream_reset(sid);

    hda_setup_bdl();

    for (int i = 0; i < HDA_NUM_BDL; ++i) {
        hda_refill_bdl(i);
    }
    g_current_bdl = 0;

    hda_write32(sdoff + HDA_SD_CBL, HDA_TOTAL_BUF_SIZE);
    hda_write16(sdoff + HDA_SD_LVI, HDA_NUM_BDL - 1);
    hda_write16(sdoff + HDA_SD_FIFOW, 0);
    hda_write32(sdoff + HDA_SD_BDLPL, (uint32_t)(g_bdl_phys & 0xFFFFFFFFu));
    hda_write32(sdoff + HDA_SD_BDLPU, (uint32_t)((g_bdl_phys >> 32) & 0xFFFFFFFFu));

    uint32_t fmt = HDA_MAKE_FMT(HDA_FMT_16BIT | HDA_FMT_STEREO, 0, HDA_FMT_DIV_44100, 0);
    hda_write32(sdoff + HDA_SD_CTL,
        (fmt << 4) | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN);

    __atomic_store_n(&g_audio_playing, 1, __ATOMIC_RELEASE);
    return 0;
}

void hda_playback_stop(void) {
    __atomic_store_n(&g_audio_playing, 0, __ATOMIC_RELEASE);
    if (g_hda_stream < 0 || !g_hda_mmio) return;
    uint16_t sdoff = HDA_SD_BASE(g_hda_stream);
    hda_write32(sdoff + HDA_SD_CTL, hda_read32(sdoff + HDA_SD_CTL) & ~HDA_SD_CTL_RUN);
    hda_stream_reset(g_hda_stream);
}

static int hda_init_codec(void) {
    uint16_t sts = hda_read16(HDA_STATESTS);
    kprint("[HDA] codec state ");
    kprint_hex64(sts);
    kprint("\n");
    if (sts == 0) return -1;

    int codec_addr = 0;
    for (int i = 0; i < 15; ++i) {
        if (sts & (1u << i)) { codec_addr = i; break; }
    }

    uint32_t vendor = hda_transfer_verb(codec_addr, HDA_VERB_GET_PARAM, HDA_PARAM_VENDOR_ID);
    kprint("[HDA] codec vendor=0x");
    kprint_hex64(vendor);
    kprint("\n");

    uint32_t afg = hda_transfer_verb(codec_addr, HDA_VERB_GET_PARAM, HDA_PARAM_AUDIO_GROUP);
    kprint("[HDA] audio group=0x");
    kprint_hex64(afg);
    kprint("\n");
    /* Set power state D0 on AFG */
    hda_transfer_verb(codec_addr, HDA_VERB_SET_POWER_STATE, HDA_POWER_STATE_D0);

    uint32_t widget = hda_transfer_verb(codec_addr, HDA_VERB_GET_PARAM, HDA_PARAM_AUDIO_WIDGET);
    int num_widgets = (int)(widget & 0xFF);
    kprint("[HDA] widgets=");
    kprint_int(num_widgets);
    kprint("\n");

    int output_nid = 0;
    for (int nid = 1; nid <= num_widgets; ++nid) {
        uint32_t wcap = hda_transfer_verb((uint16_t)nid, HDA_VERB_GET_PARAM, HDA_PARAM_AUDIO_WIDGET);
        uint8_t type = (uint8_t)((wcap >> 20) & 0x0F);
        if (type == 0) {
            output_nid = nid;
            kprint("[HDA] found audio output nid=");
            kprint_int(nid);
            kprint("\n");
        }
    }

    if (!output_nid) {
        /* Try common NID for output on QEMU's hda-output */
        output_nid = 2;
    }

    /* Set converter format: 16-bit stereo 44100 */
    hda_transfer_verb((uint16_t)output_nid, HDA_VERB_SET_CONV_FMT,
        HDA_MAKE_FMT(HDA_FMT_16BIT | HDA_FMT_STEREO, 0, HDA_FMT_DIV_44100, 0));

    /* Set stream tag and channel (stream 1, channel 0) */
    hda_transfer_verb((uint16_t)output_nid, HDA_VERB_SET_CONV_STREAM,
        (uint32_t)((uint32_t)(g_hda_stream + 1) << 4));

    /* Set pin widget control to enable output */
    hda_transfer_verb((uint16_t)output_nid, 0x70Cu, 0x40);

    return 0;
}

static void hda_scan_cb(uint32_t bus, uint32_t device, uint32_t function,
                         uint16_t vendor, uint16_t device_id, void* extra) {
    (void)vendor; (void)device_id; (void)extra;
    uint8_t class_code = (uint8_t)pci_read_field(bus, device, function, 0x0B, 1);
    uint8_t subclass = (uint8_t)pci_read_field(bus, device, function, 0x0A, 1);
    if (class_code != 0x04 || subclass != 0x03) return;

    kprint("[HDA] Intel HDA found at ");
    kprint_int((int)bus);
    kprint(":");
    kprint_int((int)device);
    kprint(".");
    kprint_int((int)function);
    kprint("\n");

    uint16_t cmd = (uint16_t)pci_read_field(bus, device, function, 0x04, 2);
    cmd |= (1u << 1) | (1u << 2) | (1u << 0);
    pci_write_field(bus, device, function, 0x04, 2, cmd);

    g_hda_irq = (uint8_t)pci_read_field(bus, device, function, 0x3C, 1);
    kprint("[HDA] IRQ=");
    kprint_int((int)g_hda_irq);
    kprint("\n");

    uint32_t bar0_lo = pci_read_field(bus, device, function, 0x10, 4);
    uint32_t bar0_hi = pci_read_field(bus, device, function, 0x14, 4);
    uintptr_t mmio_phys = (uintptr_t)bar0_lo & ~0xFu;
    if (bar0_lo & 4) {
        mmio_phys |= ((uintptr_t)bar0_hi << 32);
    }
    kprint("[HDA] MMIO phys=0x");
    kprint_hex64((uint64_t)mmio_phys);
    kprint("\n");

    g_hda_mmio = (volatile uint8_t*)(uintptr_t)(mmio_phys + hhdm_offset_get());
}

static int hda_init_controller(void) {
    /* Reset controller */
    hda_write32(HDA_GCTL, hda_read32(HDA_GCTL) | HDA_GCTL_RESET);
    for (int timeout = 0; timeout < 10000; ++timeout) {
        if (!(hda_read32(HDA_GCTL) & HDA_GCTL_RESET)) break;
    }

    /* Take out of reset */
    hda_write32(HDA_GCTL, hda_read32(HDA_GCTL) & ~HDA_GCTL_RESET);
    for (int timeout = 0; timeout < 10000; ++timeout) {
        if (hda_read32(HDA_GCTL) & HDA_GCTL_RESET) break;
    }

    uint16_t gcap = hda_read16(HDA_GCAP);
    int num_out = (gcap >> 12) & 0x0F;
    int num_in = (gcap >> 8) & 0x0F;
    int num_bidir = (gcap >> 3) & 0x1F;
    kprint("[HDA] out=");
    kprint_int(num_out);
    kprint(" in=");
    kprint_int(num_in);
    kprint(" bidir=");
    kprint_int(num_bidir);
    kprint("\n");

    if (num_out > 0) g_hda_stream = 0;
    else g_hda_stream = -1;

    /* Allocate DMA buffers */
    for (int i = 0; i < HDA_NUM_BDL; ++i) {
        uintptr_t p = (uintptr_t)pmm_alloc_page();
        if (!p) return -1;
        g_dma_buf_phys[i] = p;
        g_dma_buf_virt[i] = (uint8_t*)(uintptr_t)(p + hhdm_offset_get());
        memset(g_dma_buf_virt[i], 0, 4096);
    }

    uintptr_t bdl_p = (uintptr_t)pmm_alloc_page();
    if (!bdl_p) return -1;
    g_bdl_phys = bdl_p;
    g_bdl_virt = (hda_bdl_entry_t*)(uintptr_t)(bdl_p + hhdm_offset_get());
    memset((void*)g_bdl_virt, 0, 4096);

    /* Allocate CORB (1 page = 256 entries of 4 bytes) */
    uintptr_t corb_p = (uintptr_t)pmm_alloc_page();
    if (!corb_p) return -1;
    memset((void*)(uintptr_t)(corb_p + hhdm_offset_get()), 0, 4096);
    hda_write32(HDA_CORBLBASE, (uint32_t)(corb_p & 0xFFFFFF80u));
    hda_write32(HDA_CORBUBASE, (uint32_t)((uint64_t)corb_p >> 32));
    hda_write16(HDA_CORBSIZE, 0x0002);

    /* Reset CORB: set bit 15, then clear */
    hda_write16(HDA_CORBRP, 0x8000);
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (hda_read16(HDA_CORBRP) & 0x8000) break;
    }
    hda_write16(HDA_CORBRP, 0x0000);
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (!(hda_read16(HDA_CORBRP) & 0x8000)) break;
    }
    hda_write8(HDA_CORBCTL, HDA_CORBCTL_RUN);

    /* Allocate RIRB (1 page = 256 entries of 8 bytes) */
    uintptr_t rirb_p = (uintptr_t)pmm_alloc_page();
    if (!rirb_p) return -1;
    memset((void*)(uintptr_t)(rirb_p + hhdm_offset_get()), 0, 4096);
    hda_write32(HDA_RIRBLBASE, (uint32_t)(rirb_p & 0xFFFFFF80u));
    hda_write32(HDA_RIRBUBASE, (uint32_t)((uint64_t)rirb_p >> 32));
    hda_write16(HDA_RIRBSIZE, 0x0002);
    hda_write16(HDA_RIRBCNT, 1);

    /* Reset RIRB: set bit 15, then clear */
    hda_write16(HDA_RIRBWP, 0x8000);
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (hda_read16(HDA_RIRBWP) & 0x8000) break;
    }
    hda_write16(HDA_RIRBWP, 0x0000);
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (!(hda_read16(HDA_RIRBWP) & 0x8000)) break;
    }
    hda_write8(HDA_RIRBCTL, HDA_RIRBCTL_RUN);

    /* Enable interrupt */
    hda_write32(HDA_INTCTL, (1u << 31) | (1u << g_hda_stream));

    return 0;
}

bool intel_hda_init(void) {
    kprint("[HDA] scanning for Intel HDA...\n");
    g_hda_mmio = 0;
    g_hda_stream = -1;
    pci_scan_ex(hda_scan_cb, NULL);

    if (!g_hda_mmio) {
        kprint("[HDA] no HDA controller found\n");
        return false;
    }

    if (hda_init_controller() != 0) {
        kprint("[HDA] controller init failed\n");
        return false;
    }

    if (hda_init_codec() != 0) {
        kprint("[HDA] codec init failed\n");
        return false;
    }

    if (g_hda_irq < 16) {
        irq_register_handler(g_hda_irq, hda_irq_handler);
        pic_clear_mask(g_hda_irq);
    }

    kprint_ok("HDA audio initialized\n");
    return true;
}

int audio_play(const int16_t* samples, uint32_t count) {
    if (!samples || count == 0) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t w = g_ring_write;
        uint32_t next_w = (w + 1) % (AUDIO_RING_BYTES / 2);
        if (next_w == g_ring_read) break;
        g_audio_ring[w] = samples[i];
        g_ring_write = next_w;
        __atomic_fetch_add(&g_ring_count, 1, __ATOMIC_RELAXED);
    }

    if (!__atomic_load_n(&g_audio_playing, __ATOMIC_ACQUIRE)) {
        hda_playback_start();
    }
    return (int)__atomic_load_n(&g_ring_count, __ATOMIC_RELAXED);
}

int audio_stop(void) {
    hda_playback_stop();
    __atomic_store_n(&g_ring_count, 0, __ATOMIC_RELAXED);
    g_ring_write = 0;
    g_ring_read = 0;
    return 0;
}

bool audio_is_playing(void) {
    return __atomic_load_n(&g_audio_playing, __ATOMIC_ACQUIRE) != 0;
}
