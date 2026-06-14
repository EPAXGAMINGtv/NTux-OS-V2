#ifndef INTEL_HDA_H
#define INTEL_HDA_H

#include <stdint.h>
#include <stdbool.h>

#define HDA_PCI_VENDOR_INTEL 0x8086
#define HDA_PCI_DEVICE_ICH6  0x2668
#define HDA_PCI_DEVICE_ICH7  0x27D8
#define HDA_PCI_DEVICE_ICH8  0x293E
#define HDA_PCI_DEVICE_ICH9  0x2930
#define HDA_PCI_DEVICE_ICH10 0x3A3E
#define HDA_PCI_DEVICE_PCH   0x3B56

/* MMIO registers */
#define HDA_GCAP     0x00
#define HDA_VMIN     0x02
#define HDA_VMAJ     0x03
#define HDA_OUTPAY   0x04
#define HDA_INPAY    0x06
#define HDA_GCTL     0x08
#define HDA_WAKEEN   0x0C
#define HDA_STATESTS 0x0E
#define HDA_GSTS     0x10
#define HDA_INTCTL   0x20
#define HDA_INTSTS   0x24
#define HDA_CORBLBASE  0x40
#define HDA_CORBUBASE  0x44
#define HDA_CORBWP     0x48
#define HDA_CORBRP     0x4A
#define HDA_CORBCTL    0x4C
#define HDA_CORBSTS    0x4D
#define HDA_CORBSIZE   0x4E
#define HDA_RIRBLBASE  0x50
#define HDA_RIRBUBASE  0x54
#define HDA_RIRBWP     0x58
#define HDA_RIRBCNT    0x5A
#define HDA_RIRBCTL    0x5C
#define HDA_RIRBSTS    0x5D
#define HDA_RIRBSIZE   0x5E
#define HDA_DPLBASE    0x70
#define HDA_DPUBASE    0x74

/* Stream descriptor offsets (output start at 0x80) */
#define HDA_SD_BASE(n) (0x80 + (n) * 0x20)
#define HDA_SD_CTL     0x00
#define HDA_SD_STS     0x04
#define HDA_SD_LPIB    0x08
#define HDA_SD_CBL     0x0C
#define HDA_SD_LVI     0x10
#define HDA_SD_FIFOW   0x12
#define HDA_SD_FIFOS   0x14
#define HDA_SD_BDLPL   0x18
#define HDA_SD_BDLPU   0x1C

#define HDA_GCTL_RESET  (1u << 0)
#define HDA_GCTL_FCNTRL (1u << 1)
#define HDA_GCTL_UNSOL  (1u << 8)

#define HDA_CORBCTL_RUN (1u << 0)
#define HDA_CORBCTL_CMEIE (1u << 1)
#define HDA_RIRBCTL_RUN (1u << 0)
#define HDA_RIRBCTL_RIRBOIS (1u << 2)

#define HDA_SD_CTL_RUN   (1u << 0)
#define HDA_SD_CTL_SRST  (1u << 1)
#define HDA_SD_CTL_DESE  (1u << 3)
#define HDA_SD_CTL_DEIE  (1u << 4)
#define HDA_SD_CTL_IOCE  (1u << 5)
#define HDA_SD_CTL_STRIPE(n) (((n) & 3u) << 16)
#define HDA_SD_CTL_TP     (1u << 18)
#define HDA_SD_CTL_PAYLOAD(n) (((n) & 7u) << 20)
#define HDA_SD_CTL_DIR_SHIFT 19
#define HDA_SD_CTL_DIR_OUT (0u << 19)

#define HDA_SD_STS_BCIS  (1u << 2)
#define HDA_SD_STS_FIFOE (1u << 3)
#define HDA_SD_STS_DESE  (1u << 4)
#define HDA_SD_STS_FIFORDY (1u << 5)

/* Codec verbs */
#define HDA_VERB_GET_PARAM         0xF00
#define HDA_VERB_SET_POWER_STATE   0x705
#define HDA_VERB_SET_CONV_FMT      0x200
#define HDA_VERB_SET_CONV_STREAM   0x600
#define HDA_VERB_SET_PIN_WIDGET    0x705
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300

#define HDA_PARAM_VENDOR_ID    0x00
#define HDA_PARAM_REVISION     0x02
#define HDA_PARAM_SUBORDINATE  0x04
#define HDA_PARAM_AUDIO_GROUP  0x08
#define HDA_PARAM_AUDIO_WIDGET 0x09
#define HDA_PARAM_PCM          0x0A
#define HDA_PARAM_STREAM       0x0B
#define HDA_PARAM_PIN_CAP      0x0C
#define HDA_PARAM_AMP_IN_CAP   0x0D
#define HDA_PARAM_CONN_LIST    0x0E
#define HDA_PARAM_POWER_STATE  0x0F
#define HDA_PARAM_GPIO_COUNT   0x10
#define HDA_PARAM_VOL_KNB      0x12

#define HDA_POWER_STATE_D0 0x00
#define HDA_POWER_STATE_D1 0x01
#define HDA_POWER_STATE_D2 0x02
#define HDA_POWER_STATE_D3 0x03

/* CORB entry: 4 bytes (verb) */
typedef volatile struct {
    uint32_t verb;
} __attribute__((packed)) hda_corb_entry_t;

/* RIRB entry: 8 bytes (response) */
typedef volatile struct {
    uint32_t response;
    uint32_t flags;
} __attribute__((packed)) hda_rirb_entry_t;

/* BDL entry */
typedef volatile struct {
    uint64_t address;
    uint32_t length;
    uint32_t ioc;
} __attribute__((packed)) hda_bdl_entry_t;

#define HDA_BDL_IOC (1u << 31)

/* Format: 16-bit stereo 44100 Hz */
#define HDA_FMT_BASE_RATE 48000
#define HDA_FMT_DIV_44100 0
#define HDA_FMT_DIV_48000 1
#define HDA_FMT_DIV_32000 2
#define HDA_FMT_DIV_22050 3
#define HDA_FMT_DIV_16000 4
#define HDA_FMT_DIV_11025 5
#define HDA_FMT_DIV_8000  6
#define HDA_FMT_16BIT  (1u << 4)
#define HDA_FMT_STEREO (1u << 6)

#define HDA_MAKE_FMT(bits, chan, div, mult) \
    ((bits) | ((chan) << 4) | ((div) << 8) | ((mult) << 11))

/* BDL and buffer config */
#define HDA_NUM_BDL 4
#define HDA_BDL_BUF_SIZE 8192
#define HDA_TOTAL_BUF_SIZE (HDA_NUM_BDL * HDA_BDL_BUF_SIZE)

/* Audio format config */
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2
#define AUDIO_BITS 16
#define AUDIO_BYTES_PER_FRAME (AUDIO_CHANNELS * AUDIO_BITS / 8)

/* Global audio ring buffer */
#define AUDIO_RING_BYTES (65536 * 4)

bool intel_hda_init(void);
int audio_play(const int16_t* samples, uint32_t count);
int audio_stop(void);
bool audio_is_playing(void);

#endif
