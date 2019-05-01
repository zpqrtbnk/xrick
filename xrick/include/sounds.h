/*
 * xrick/include/sounds.h
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

#ifndef _SOUNDS_H
#define _SOUNDS_H

#include "syssnd.h"

#ifdef ENABLE_SOUND

extern sound_t* WAV_GAMEOVER;
extern sound_t* WAV_SBONUS2;
extern sound_t* WAV_BULLET;
extern sound_t* WAV_BOMBSHHT;
extern sound_t* WAV_EXPLODE;
extern sound_t* WAV_STICK;
extern sound_t* WAV_WALK;
extern sound_t* WAV_CRAWL;
extern sound_t* WAV_JUMP;
extern sound_t* WAV_PAD;
extern sound_t* WAV_BOX;
extern sound_t* WAV_BONUS;
extern sound_t* WAV_SBONUS1;
extern sound_t* WAV_DIE;
extern sound_t* WAV_ENTITY[];

extern void sounds_load();
extern void sounds_free();
extern void sounds_stopMusic(void);
extern void sounds_setMusic(char* name, U8 loop);

#endif /* ENABLE_SOUND */

#endif /* _SOUNDS_H */

/* eof */


