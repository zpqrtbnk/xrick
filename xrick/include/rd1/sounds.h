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
 * T19 / audio-sndh.md S7: every WAV_* below is now a { ST track, play_music() D1 }
 * descriptor, not a loaded PCM buffer -- see syssnd.h. Track numbers and their D1
 * values are cited evidence, not guesses; see audio-sndh.md S7 for the source of each.
 */

#ifndef _SOUNDS_H
#define _SOUNDS_H

#include "syssnd.h"

#ifdef ENABLE_SOUND

extern sound_t *WAV_GAMEOVER;
extern sound_t *WAV_SBONUS2;
extern sound_t *WAV_BULLET;
extern sound_t *WAV_BOMBSHHT;
extern sound_t *WAV_EXPLODE;
extern sound_t *WAV_STICK;
extern sound_t *WAV_WALK;
extern sound_t *WAV_CRAWL;
extern sound_t *WAV_JUMP;
extern sound_t *WAV_PAD;
extern sound_t *WAV_BOX;
extern sound_t *WAV_BONUS;
extern sound_t *WAV_SBONUS1;
extern sound_t *WAV_DIE;
extern sound_t *WAV_ENTITY[10];

/* Level-theme track numbers (audio-sndh.md S7: play_music(D0=level_index, D1=0)) --
   dat_maps.c's map_t.tune field holds one of these directly. */
#define SND_TRACK_LEVEL0	0
#define SND_TRACK_LEVEL1	1
#define SND_TRACK_LEVEL2	2
#define SND_TRACK_LEVEL3	3
#define SND_TRACK_LEVEL4	4
#define SND_TRACK_ATTRACT	5	/* title/attract music, D1=1 (loops) */
#define SND_TRACK_GAMEOVER	6	/* D1=0 */

extern void sounds_load(void);
extern void sounds_free(void);
extern void sounds_setMusic(U8 track, S8 d1);

#endif /* ENABLE_SOUND */

#endif /* _SOUNDS_H */

/* eof */
