#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i_sound.h"
#include "i_system.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomtype.h"
#include "sounds.h"
#include <syscall.h>

#define NUM_CHANNELS 16
#define MIX_RATE 44100
#define MIX_SAMPLES 2048

static boolean g_sound_init = 0;
static int16_t g_mix_buffer[MIX_SAMPLES * 2];

typedef struct {
    boolean active;
    int16_t* data;
    uint32_t length;
    uint32_t pos;
    int volume;
    int sep;
    int sfx_id;
} ntux_channel_t;

static ntux_channel_t g_channels[NUM_CHANNELS];

static void ntux_mix_channel(ntux_channel_t* ch, int16_t* out, int count) {
    if (!ch->active || !ch->data) return;
    for (int i = 0; i < count; ++i) {
        if (ch->pos >= ch->length) {
            ch->active = 0;
            break;
        }
        int32_t s = ch->data[ch->pos++];
        int32_t vol = ch->volume;
        out[i * 2] = (int16_t)(out[i * 2] + (s * vol / 127));
        out[i * 2 + 1] = (int16_t)(out[i * 2 + 1] + (s * vol / 127));
    }
}

void ntux_submit_audio(void) {
    memset(g_mix_buffer, 0, sizeof(g_mix_buffer));
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (g_channels[i].active) {
            ntux_mix_channel(&g_channels[i], g_mix_buffer, MIX_SAMPLES);
        }
    }
    sys_audio_play(g_mix_buffer, MIX_SAMPLES * 2);
}

static boolean I_NTUX_InitSound(boolean use_sfx_prefix) {
    (void)use_sfx_prefix;
    memset(g_channels, 0, sizeof(g_channels));
    g_sound_init = 1;
    return 1;
}

static void I_NTUX_ShutdownSound(void) {
    sys_audio_stop();
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (g_channels[i].data) {
            free(g_channels[i].data);
            g_channels[i].data = 0;
        }
        g_channels[i].active = 0;
    }
    g_sound_init = 0;
}

static int I_NTUX_GetSfxLumpNum(sfxinfo_t* sfxinfo) {
    if (sfxinfo->lumpnum >= 0) return sfxinfo->lumpnum;
    char name[10];
    snprintf(name, sizeof(name), "ds%-.6s", sfxinfo->name);
    if (name[2] == '?' || name[2] == 0) return -1;
    return W_CheckNumForName(name);
}

static int16_t* convert_to_16bit(const uint8_t* data, int len, int* out_samples) {
    if (!data || len < 8) return 0;
    int samples = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    if (samples <= 0 || samples > 65536) return 0;
    const uint8_t* src = data + 8;
    int src_len = len - 8;
    if (src_len < samples) samples = src_len;
    *out_samples = samples;
    int16_t* pcm = (int16_t*)malloc(samples * sizeof(int16_t));
    if (!pcm) return 0;
    for (int i = 0; i < samples; ++i) {
        int32_t sample = (int32_t)(src[i]) - 128;
        pcm[i] = (int16_t)(sample << 8);
    }
    return pcm;
}

static int I_NTUX_StartSound(sfxinfo_t* sfxinfo, int channel, int vol, int sep) {
    if (!g_sound_init || channel < 0 || channel >= NUM_CHANNELS) return -1;
    (void)sep;
    int lumpnum = I_NTUX_GetSfxLumpNum(sfxinfo);
    if (lumpnum < 0) return -1;
    size_t lump_len = 0;
    const uint8_t* lump_data = (const uint8_t*)W_CacheLumpNum(lumpnum, PU_STATIC);
    if (!lump_data) return -1;
    lump_len = W_LumpLength(lumpnum);
    int samples = 0;
    int16_t* pcm = convert_to_16bit(lump_data, (int)lump_len, &samples);
    Z_Free((void*)lump_data);
    if (!pcm || samples == 0) return -1;

    if (g_channels[channel].data) free(g_channels[channel].data);
    g_channels[channel].data = pcm;
    g_channels[channel].length = samples;
    g_channels[channel].pos = 0;
    g_channels[channel].volume = vol;
    g_channels[channel].active = 1;
    g_channels[channel].sfx_id = sfxinfo - S_sfx;

    return channel;
}

static void I_NTUX_StopSound(int channel) {
    if (channel < 0 || channel >= NUM_CHANNELS) return;
    g_channels[channel].active = 0;
}

static boolean I_NTUX_SoundIsPlaying(int channel) {
    if (channel < 0 || channel >= NUM_CHANNELS) return 0;
    return g_channels[channel].active;
}

static void I_NTUX_UpdateSound(void) {
    ntux_submit_audio();
}

static void I_NTUX_UpdateSoundParams(int channel, int vol, int sep) {
    if (channel >= 0 && channel < NUM_CHANNELS) {
        g_channels[channel].volume = vol;
        g_channels[channel].sep = sep;
    }
}

static void I_NTUX_CacheSounds(sfxinfo_t* sounds, int num_sounds) {
    (void)sounds;
    (void)num_sounds;
}

static snddevice_t ntux_sound_devices[] = {
    SNDDEVICE_SB
};

sound_module_t DG_sound_module = {
    ntux_sound_devices,
    1,
    I_NTUX_InitSound,
    I_NTUX_ShutdownSound,
    I_NTUX_GetSfxLumpNum,
    I_NTUX_UpdateSound,
    I_NTUX_UpdateSoundParams,
    I_NTUX_StartSound,
    I_NTUX_StopSound,
    I_NTUX_SoundIsPlaying,
    I_NTUX_CacheSounds
};
