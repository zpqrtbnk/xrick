/*
 * xrick/include/rd2/rd2_snd.h
 *
 * Rick Dangerous 2 -- sound host (rd2_snd.c): the game's own engine on the emulated
 * 68000/YM2149/MFP. play_sound $1a6aa and stop-all $1a5d0 are declared in rd2_game.h.
 */

#ifndef _RD2_SND_H
#define _RD2_SND_H

#include "system.h"

void rd2_snd_init(void);
void rd2_snd_shutdown(void);
U16  rd2_snd_rw(U32 a);    /* game-side read of an engine cell, e.g. [$1aa08] */

#endif /* _RD2_SND_H */

/* eof */
