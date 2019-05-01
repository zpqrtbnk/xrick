/*
 * xrick/include/syssnd.h
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#ifndef _SYSSND_H
#define _SYSSND_H

#include "system.h"

#ifdef ENABLE_SOUND

typedef struct {
#ifdef DEBUG
	char* name;
#endif
	U8* buf;
	U32 len;
	U8 dispose;
} sound_t;

extern void syssnd_init(void);
extern void syssnd_shutdown(void);
extern void syssnd_vol(S8);
extern void syssnd_toggleMute(void);
extern S8 syssnd_play(sound_t*, S8);
extern void syssnd_pause(U8, U8);
extern void syssnd_stopchan(S8);
extern void syssnd_stopsound(sound_t*);
extern void syssnd_stopall();
extern int syssnd_isplaying(sound_t*);
extern sound_t* syssnd_load(char* name);
extern void syssnd_free(sound_t*);

/* 8-bit mono at 22050Hz */
#define SYSSND_FREQ 22050
#define SYSSND_CHANNELS 1
#define SYSSND_MAXVOL 10
#define SYSSND_MIXCHANNELS 8
/* MIXSAMPLES: 256 is too low on Windows. 512 means ~20 mix per second at 11025Hz */
/* MIXSAMPLES: ?? at 22050Hz */
#define SYSSND_MIXSAMPLES 1024

typedef struct {
	sound_t *snd;
	U8 *buf;
	U32 len;
	S8 loop;
} channel_t;

#endif /* ENABLE_SOUND */

#endif /* _SYSSND_H */

/* eof */


