/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

/*
 * Rick Dangerous 2 -- sound host (rd2_snd.c): the game's own engine on the emulated
 * 68000/YM2149/MFP. play_sound $1a6aa and stop-all $1a5d0 are declared in rd2_game.h.
 */

#ifndef _RD2_SND_H
#define _RD2_SND_H

#include "system.h"

void rd2_snd_init(void);
void rd2_snd_shutdown(void);
void rd2_snd_toggleMute(void);   /* F4 */
void rd2_snd_vol(S8 d);          /* F5 / F6 */
U16  rd2_snd_rw(U32 a);   /* game-side read of an engine cell, e.g. [$1aa08] */

#endif /* _RD2_SND_H */

/* eof */
