//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2008 David Flater
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	System interface for sound, backed by JaszczurHAL DMA PWM audio.
//

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <hal/audio/hal_dma_pwm_audio.h>
#include <hal/serial/hal_serial.h>

#include "deh_str.h"
#include "doom/sounds.h"
#include "doomtype.h"
#include "doom_sound_hal.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#if (DOOM_HAL_AUDIO_BLOCK_SIZE & (DOOM_HAL_AUDIO_BLOCK_SIZE - 1u)) != 0
#error "DOOM_HAL_AUDIO_BLOCK_SIZE must be a power of two for RP2040 DMA ring mode"
#endif

#define ADPCM_BLOCK_SIZE 128
#define ADPCM_SAMPLES_PER_BLOCK_SIZE 249
#define FADE_STEP 8u
#define MIX_MAX_VOLUME 128

typedef struct channel_s channel_t;

typedef struct {
    uint8_t *bytes;
    uint32_t size;
} audio_buffer_storage_t;

struct audio_buffer {
    audio_buffer_storage_t *buffer;
    uint32_t max_sample_count;
    uint32_t sample_count;
};

static volatile enum {
    FS_NONE,
    FS_FADE_OUT,
    FS_FADE_IN,
    FS_SILENT,
} fade_state;

static uint32_t fade_level = 0x10000u;

struct channel_s
{
    const uint8_t *data;
    const uint8_t *data_end;
    uint32_t offset;
    uint32_t step;
    uint8_t left;
    uint8_t right;
    uint8_t decompressed_size;
#if SOUND_LOW_PASS
    uint8_t alpha256;
#endif
    int8_t decompressed[ADPCM_SAMPLES_PER_BLOCK_SIZE];
};

static hal_dma_pwm_audio_t audio_dma;

static uint16_t s_pwm_buffer_a[DOOM_HAL_AUDIO_BLOCK_SIZE]
    __attribute__((aligned(DOOM_HAL_AUDIO_BLOCK_SIZE * sizeof(uint16_t))));
static uint16_t s_pwm_buffer_b[DOOM_HAL_AUDIO_BLOCK_SIZE]
    __attribute__((aligned(DOOM_HAL_AUDIO_BLOCK_SIZE * sizeof(uint16_t))));
static int32_t s_mix_buffer[DOOM_HAL_AUDIO_BLOCK_SIZE * 2u];
static int16_t s_music_pcm_buffer[DOOM_HAL_AUDIO_BLOCK_SIZE * 2u];
static audio_buffer_storage_t s_music_storage = {
    (uint8_t *)s_music_pcm_buffer,
    sizeof(s_music_pcm_buffer),
};
static audio_buffer_t s_music_buffer = {
    &s_music_storage,
    DOOM_HAL_AUDIO_BLOCK_SIZE,
    DOOM_HAL_AUDIO_BLOCK_SIZE,
};

static void (*music_generator)(audio_buffer_t *buffer);

static boolean sound_initialized = false;
static channel_t channels[NUM_SOUND_CHANNELS];

static boolean use_sfx_prefix;

// ====== FROM ADPCM-LIB =====
#define CLIP(data, min, max) \
if ((data) > (max)) data = max; \
else if ((data) < (min)) data = min;

static const uint16_t step_table[89] = {
        7, 8, 9, 10, 11, 12, 13, 14,
        16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
        3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767
};

static const int index_table[] = {
        -1, -1, -1, -1, 2, 4, 6, 8
};
// =============================

static inline boolean is_channel_playing(int channel)
{
    return channels[channel].decompressed_size != 0;
}

static inline void stop_channel(int channel)
{
    channels[channel].decompressed_size = 0;
}

static boolean check_and_init_channel(int channel)
{
    return sound_initialized && channel >= 0 && channel < NUM_SOUND_CHANNELS;
}

int adpcm_decode_block_s8(int8_t *outbuf, const uint8_t *inbuf, int inbufsize)
{
    int samples = 1;
    int chunks;

    if (inbufsize < 4)
    {
        return 0;
    }

    int32_t pcmdata = (int16_t) (inbuf[0] | (inbuf[1] << 8));
    *outbuf++ = pcmdata >> 8u;
    int index = inbuf[2];

    if (index < 0 || index > 88 || inbuf[3])
    {
        return 0;
    }

    inbufsize -= 4;
    inbuf += 4;

    chunks = inbufsize / 4;
    samples += chunks * 8;

    while (chunks--)
    {
        for (int i = 0; i < 4; ++i)
        {
            int step = step_table[index];
            int delta = step >> 3;

            if (*inbuf & 1) delta += (step >> 2);
            if (*inbuf & 2) delta += (step >> 1);
            if (*inbuf & 4) delta += step;
            if (*inbuf & 8) delta = -delta;

            pcmdata += delta;
            index += index_table[*inbuf & 0x7];
            CLIP(index, 0, 88);
            CLIP(pcmdata, -32768, 32767);
            outbuf[i * 2] = pcmdata >> 8u;

            step = step_table[index];
            delta = step >> 3;

            if (*inbuf & 0x10) delta += (step >> 2);
            if (*inbuf & 0x20) delta += (step >> 1);
            if (*inbuf & 0x40) delta += step;
            if (*inbuf & 0x80) delta = -delta;

            pcmdata += delta;
            index += index_table[(*inbuf >> 4) & 0x7];
            CLIP(index, 0, 88);
            CLIP(pcmdata, -32768, 32767);
            outbuf[i * 2 + 1] = pcmdata >> 8u;
            inbuf++;
        }

        outbuf += 8;
    }

    return samples;
}

static void decompress_buffer(channel_t *channel)
{
    if (channel->data == channel->data_end)
    {
        channel->decompressed_size = 0;
    }
    else
    {
        int block_size = MIN(ADPCM_BLOCK_SIZE, channel->data_end - channel->data);
        channel->decompressed_size =
            adpcm_decode_block_s8(channel->decompressed, channel->data, block_size);
        assert(channel->decompressed_size
            && channel->decompressed_size <= sizeof(channel->decompressed));
        channel->data += block_size;
    }
}

static boolean init_channel_for_sfx(channel_t *ch, const sfxinfo_t *sfxinfo,
                                    int pitch)
{
    int lumpnum = sfx_mut(sfxinfo)->lumpnum;
    int lumplen = W_LumpLength(lumpnum);
    const uint8_t *data = W_CacheLumpNum(lumpnum, PU_STATIC);

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x80)
    {
        return false;
    }

    ch->data = data + 8;
    ch->data_end = ch->data + lumplen - 8;

    uint32_t sample_freq = (data[3] << 8) | data[2];
    uint64_t step = (uint64_t)sample_freq * 65536ull;
    if (pitch != NORM_PITCH)
    {
        step = (step * (uint32_t)pitch) / NORM_PITCH;
    }
    ch->step = (uint32_t)(step / PICO_SOUND_SAMPLE_FREQ);
    if (ch->step == 0)
    {
        ch->step = 1;
    }

    ch->offset = 0;

#if SOUND_LOW_PASS
    ch->alpha256 = 256u * 201u * sample_freq
        / (201u * sample_freq + 64u * (uint32_t)PICO_SOUND_SAMPLE_FREQ);
#endif

    decompress_buffer(ch);
    return true;
}

static void GetSfxLumpName(const sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    if (use_sfx_prefix)
    {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

static void I_Pico_PrecacheSounds(should_be_const sfxinfo_t *sounds,
                                  int num_sounds)
{
    (void)sounds;
    (void)num_sounds;
}

static int I_Pico_GetSfxLumpNum(should_be_const sfxinfo_t *sfx)
{
    char namebuf[9];
    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void set_channel_volume(channel_t *channel, int vol, int sep)
{
    int left = ((254 - sep) * vol) / 127;
    int right = (sep * vol) / 127;

    if (left < 0) left = 0;
    else if (left > 255) left = 255;
    if (right < 0) right = 0;
    else if (right > 255) right = 255;

    channel->left = (uint8_t)left;
    channel->right = (uint8_t)right;
}

static void I_Pico_UpdateSoundParams(int handle, int vol, int sep)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_SOUND_CHANNELS)
    {
        return;
    }

    set_channel_volume(&channels[handle], vol, sep);
}

static int I_Pico_StartSound(should_be_const sfxinfo_t *sfxinfo, int channel,
                             int vol, int sep, int pitch)
{
    if (!check_and_init_channel(channel))
    {
        return -1;
    }

    stop_channel(channel);
    channel_t *ch = &channels[channel];
    set_channel_volume(ch, vol, sep);

    if (!init_channel_for_sfx(ch, sfxinfo, pitch))
    {
        stop_channel(channel);
        return -1;
    }

    return channel;
}

static void I_Pico_StopSound(int channel)
{
    if (check_and_init_channel(channel))
    {
        stop_channel(channel);
    }
}

static boolean I_Pico_SoundIsPlaying(int channel)
{
    if (!check_and_init_channel(channel))
    {
        return false;
    }

    return is_channel_playing(channel);
}

static void mix_music(int32_t *samples, uint32_t sample_count)
{
    memset(s_music_pcm_buffer, 0, sizeof(s_music_pcm_buffer));

    if (music_generator != NULL)
    {
        s_music_buffer.max_sample_count = sample_count;
        s_music_buffer.sample_count = sample_count;
        music_generator(&s_music_buffer);

        if (s_music_buffer.sample_count > sample_count)
        {
            s_music_buffer.sample_count = sample_count;
        }
    }
    else
    {
        s_music_buffer.sample_count = sample_count;
    }

    for (uint32_t i = 0; i < s_music_buffer.sample_count * 2u; ++i)
    {
        samples[i] += s_music_pcm_buffer[i];
    }
}

static void mix_sfx(int32_t *samples, uint32_t sample_count)
{
    for (int ch = 0; ch < NUM_SOUND_CHANNELS; ch++)
    {
        if (!is_channel_playing(ch))
        {
            continue;
        }

        channel_t *channel = &channels[ch];
        int voll = channel->left / 2;
        int volr = channel->right / 2;
        uint32_t offset_end = (uint32_t)channel->decompressed_size * 65536u;
        assert(channel->offset < offset_end);

#if SOUND_LOW_PASS
        int alpha256 = channel->alpha256;
        int beta256 = 256 - alpha256;
        int sample = channel->decompressed[channel->offset >> 16];
#endif

        for (uint32_t s = 0; s < sample_count; s++)
        {
#if !SOUND_LOW_PASS
            int sample = channel->decompressed[channel->offset >> 16];
#else
            sample = (beta256 * sample
                + alpha256 * channel->decompressed[channel->offset >> 16]) / 256;
#endif
            samples[s * 2u] += sample * voll;
            samples[s * 2u + 1u] += sample * volr;

            channel->offset += channel->step;
            if (channel->offset >= offset_end)
            {
                channel->offset -= offset_end;
                decompress_buffer(channel);
                offset_end = (uint32_t)channel->decompressed_size * 65536u;
                if (channel->offset >= offset_end)
                {
                    stop_channel(ch);
                    break;
                }
            }
        }
    }
}

static void apply_fade(int32_t *samples, uint32_t sample_count)
{
    if (fade_state == FS_SILENT)
    {
        memset(samples, 0, sample_count * 2u * sizeof(*samples));
        return;
    }

    if (fade_state == FS_NONE)
    {
        return;
    }

    uint32_t level = fade_level;

    for (uint32_t i = 0; i < sample_count; ++i)
    {
        samples[i * 2u] = (samples[i * 2u] * (int32_t)level) >> 16;
        samples[i * 2u + 1u] = (samples[i * 2u + 1u] * (int32_t)level) >> 16;

        if (fade_state == FS_FADE_IN)
        {
            if (level >= 0x10000u - FADE_STEP)
            {
                level = 0x10000u;
                fade_state = FS_NONE;
                break;
            }
            level += FADE_STEP;
        }
        else
        {
            if (level <= FADE_STEP)
            {
                level = 0u;
                fade_state = FS_SILENT;
                for (uint32_t j = i + 1u; j < sample_count; ++j)
                {
                    samples[j * 2u] = 0;
                    samples[j * 2u + 1u] = 0;
                }
                break;
            }
            level -= FADE_STEP;
        }
    }

    fade_level = level;
}

static uint16_t mix_to_pwm_sample(int32_t left, int32_t right)
{
    int32_t mono = (left + right) / 2;

    if (mono < -32768)
    {
        mono = -32768;
    }
    else if (mono > 32767)
    {
        mono = 32767;
    }

    uint32_t biased = (uint32_t)(mono + 32768);
    uint32_t pwm = (biased * (DOOM_HAL_AUDIO_PWM_PERIOD_TICKS - 1u) + 32767u)
        / 65535u;

    if (pwm >= DOOM_HAL_AUDIO_PWM_PERIOD_TICKS)
    {
        pwm = DOOM_HAL_AUDIO_PWM_PERIOD_TICKS - 1u;
    }

    return (uint16_t)pwm;
}

static void fill_pwm_buffer(uint16_t *pwm_buffer)
{
    memset(s_mix_buffer, 0, sizeof(s_mix_buffer));

    if (sound_initialized)
    {
        mix_music(s_mix_buffer, DOOM_HAL_AUDIO_BLOCK_SIZE);
        mix_sfx(s_mix_buffer, DOOM_HAL_AUDIO_BLOCK_SIZE);
        apply_fade(s_mix_buffer, DOOM_HAL_AUDIO_BLOCK_SIZE);
    }

    for (uint32_t i = 0; i < DOOM_HAL_AUDIO_BLOCK_SIZE; ++i)
    {
        pwm_buffer[i] = mix_to_pwm_sample(s_mix_buffer[i * 2u],
                                          s_mix_buffer[i * 2u + 1u]);
    }
}

static void dma_buffer_done(void *user, uint16_t *buffer, uint8_t buffer_index)
{
    (void)user;
    (void)buffer_index;
    fill_pwm_buffer(buffer);
}

static void I_Pico_UpdateSound(void)
{
}

static void I_Pico_ShutdownSound(void)
{
    if (!sound_initialized)
    {
        return;
    }

    sound_initialized = false;

    if (audio_dma != NULL)
    {
        hal_dma_pwm_audio_stop(audio_dma);
        hal_dma_pwm_audio_destroy(audio_dma);
        audio_dma = NULL;
    }

    music_generator = NULL;
    memset(channels, 0, sizeof(channels));
}

static boolean I_Pico_InitSound(boolean _use_sfx_prefix)
{
    use_sfx_prefix = _use_sfx_prefix;

    if (sound_initialized)
    {
        return true;
    }

    if (!hal_dma_pwm_audio_supported())
    {
        hal_deb("[audio] DMA PWM audio unsupported");
        return false;
    }

    memset(channels, 0, sizeof(channels));
    fade_state = FS_NONE;
    fade_level = 0x10000u;
    music_generator = NULL;

    hal_dma_pwm_audio_config_t cfg = {
        .pwm_pin = DOOM_HAL_AUDIO_PWM_PIN,
        .sample_rate_hz = PICO_SOUND_SAMPLE_FREQ,
        .period_ticks = DOOM_HAL_AUDIO_PWM_PERIOD_TICKS,
        .buffer_a = s_pwm_buffer_a,
        .buffer_b = s_pwm_buffer_b,
        .block_size = DOOM_HAL_AUDIO_BLOCK_SIZE,
        .idle_value = DOOM_HAL_AUDIO_PWM_IDLE,
        .adc_pins = NULL,
        .adc_count = 0,
        .adc_buffer = NULL,
        .buffer_done_cb = dma_buffer_done,
        .user = NULL,
    };

    audio_dma = hal_dma_pwm_audio_create(&cfg);
    if (audio_dma == NULL)
    {
        hal_deb("[audio] DMA PWM audio unavailable; continuing without sound");
        return false;
    }

    sound_initialized = true;
    fill_pwm_buffer(s_pwm_buffer_a);
    fill_pwm_buffer(s_pwm_buffer_b);

    if (!hal_dma_pwm_audio_start(audio_dma))
    {
        hal_deb("[audio] DMA PWM audio start failed; continuing without sound");
        I_Pico_ShutdownSound();
        return false;
    }

    return true;
}

static snddevice_t sound_pico_devices[] =
{
    SNDDEVICE_SB,
};

sound_module_t sound_pico_module =
{
    sound_pico_devices,
    arrlen(sound_pico_devices),
    I_Pico_InitSound,
    I_Pico_ShutdownSound,
    I_Pico_GetSfxLumpNum,
    I_Pico_UpdateSound,
    I_Pico_UpdateSoundParams,
    I_Pico_StartSound,
    I_Pico_StopSound,
    I_Pico_SoundIsPlaying,
    I_Pico_PrecacheSounds,
};

bool I_PicoSoundIsInitialized(void)
{
    return sound_initialized;
}

void I_PicoSoundSetMusicGenerator(void (*generator)(audio_buffer_t *buffer))
{
    music_generator = generator;
}

void I_PicoSoundFade(bool in)
{
    fade_state = in ? FS_FADE_IN : FS_FADE_OUT;
    fade_level = in ? FADE_STEP : 0x10000u - FADE_STEP;
}

bool I_PicoSoundFading(void)
{
    return fade_state == FS_FADE_IN || fade_state == FS_FADE_OUT;
}
