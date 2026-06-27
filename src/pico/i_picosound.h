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
//	System interface for sound.

#ifndef __I_PICO_SOUND__
#define __I_PICO_SOUND__

#include <stdbool.h>
#include <stdint.h>

typedef struct audio_buffer audio_buffer_t;

#ifndef DOOM_HAL_AUDIO_PWM_PIN
#define DOOM_HAL_AUDIO_PWM_PIN 6u
#endif

#ifndef DOOM_HAL_AUDIO_PWM_BITS
#define DOOM_HAL_AUDIO_PWM_BITS 12u
#endif

#ifndef DOOM_HAL_AUDIO_PWM_PERIOD_TICKS
#define DOOM_HAL_AUDIO_PWM_PERIOD_TICKS (1u << DOOM_HAL_AUDIO_PWM_BITS)
#endif

#ifndef DOOM_HAL_AUDIO_PWM_IDLE
#define DOOM_HAL_AUDIO_PWM_IDLE (DOOM_HAL_AUDIO_PWM_PERIOD_TICKS / 2u)
#endif

#ifndef DOOM_HAL_AUDIO_BLOCK_SIZE
#define DOOM_HAL_AUDIO_BLOCK_SIZE 1024u
#endif

#ifndef PICO_SOUND_SAMPLE_FREQ
#ifdef F_CPU
#define PICO_SOUND_SAMPLE_FREQ ((uint32_t)(F_CPU / DOOM_HAL_AUDIO_PWM_PERIOD_TICKS))
#else
#define PICO_SOUND_SAMPLE_FREQ 48828u
#endif
#endif

#ifndef NUM_SOUND_CHANNELS
// This is the default in-game channel count, not 16.
#define NUM_SOUND_CHANNELS 8
#endif

void I_PicoSoundSetMusicGenerator(void (*generator)(audio_buffer_t *buffer));
bool I_PicoSoundIsInitialized(void);
void I_PicoSoundFade(bool in);
bool I_PicoSoundFading(void);
#endif
