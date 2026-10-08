/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 *
 * T19 / audio-sndh.md: WAV playback replaced by the real ST sound engine, run under
 * AtariAudio's 68000 emulation and driven directly (see src/syssnd.c). A sound_t is
 * no longer a PCM buffer -- it is the ST track number plus the play_music() D1 value
 * the original game used at its own call site for this exact sound (audio-sndh.md
 * S5/S7), a fact established once per sound and baked in as a compile-time constant.
 */

#ifndef _SYSSND_H
#define _SYSSND_H

#include "system.h"

#ifdef ENABLE_SOUND

typedef struct {
	U8 track;	/* index into music_track_table, 0-28 */
	S8 d1;		/* play_music()'s D1 at this sound's original ST call site */
} sound_t;

extern void syssnd_init(void);
extern void syssnd_shutdown(void);
extern void syssnd_vol(S8);
extern void syssnd_toggleMute(void);
extern void syssnd_play(const sound_t *sound);
extern void syssnd_play_track(U8 track, S8 d1);
extern void syssnd_pause(U8, U8);

/* mono, signed 16-bit -- what AtariMachine::ComputeNextSample() produces */
#define SYSSND_FREQ 44100
#define SYSSND_CHANNELS 1
#define SYSSND_MAXVOL 10
/* MIXSAMPLES: kept from the WAV-mixer era -- still the right order of magnitude for
   callback latency vs. per-buffer overhead; unrelated to how many sounds can overlap
   now (the ST engine itself arbitrates its 3 PSG voices, not this buffer size). */
#define SYSSND_MIXSAMPLES 2048

#endif /* ENABLE_SOUND */

#endif /* _SYSSND_H */

/* eof */
