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
 * T19 / audio-sndh.md: every WAV_* is now a compile-time { track, d1 } descriptor
 * (audio-sndh.md S7's census) instead of a WAV file loaded at runtime -- the same
 * shape the port's own pre-#021212 wav_*.e files used for embedded PCM (see e.g.
 * src/wav_bullet.e, now dead code, audio-sndh.md S6). There is nothing left to load
 * or free: syssnd_init() already uploaded the engine and armed it (S2), so
 * sounds_load()/sounds_free() are kept only so game.c's call sites need no change.
 *
 * sounds_setMusic() lost its own dedicated "stop" step: the original engine's
 * play_music() always lets a new type-0 (song) track take over immediately
 * (re/algo-music.md S3), so there is nothing to stop first.
 */

#include "sounds.h"

#ifdef ENABLE_SOUND

/*
 * WAV_* track descriptors. Track numbers and D1 values are cited evidence, not
 * guesses -- see audio-sndh.md S7 for the source of each row. D1 is irrelevant for
 * the three PCM sample tracks (8, 10, 19): play_music()'s type-2 branch ignores it
 * entirely, so 0 is used there without loss of fidelity.
 */
static const sound_t WAV_GAMEOVER_OBJ  = { 6,  0 };	/* game-over jingle */
static const sound_t WAV_SBONUS2_OBJ   = { 7,  0 };	/* effect_stop_timer_award_bonus */
static const sound_t WAV_BULLET_OBJ    = { 8,  0 };	/* PCM gunshot */
static const sound_t WAV_BOMBSHHT_OBJ  = { 9,  1 };	/* empty-click / dynamite fuse tick */
static const sound_t WAV_EXPLODE_OBJ   = { 10, 0 };	/* PCM explosion */
static const sound_t WAV_STICK_OBJ     = { 11, 1 };	/* stick jab */
static const sound_t WAV_WALK_OBJ      = { 12, 1 };	/* footstep (also climb, ST-side) */
static const sound_t WAV_CRAWL_OBJ     = { 13, 1 };	/* crawl */
static const sound_t WAV_JUMP_OBJ      = { 14, 1 };	/* jump / ladder-exit */
static const sound_t WAV_PAD_OBJ       = { 15, 1 };	/* landing thud */
static const sound_t WAV_BOX_OBJ       = { 16, 0 };	/* crate collected (refill) */
static const sound_t WAV_BONUS_OBJ     = { 17, 0 };	/* treasure pickup */
static const sound_t WAV_SBONUS1_OBJ   = { 18, 0 };	/* effect_start_escape_timer */
static const sound_t WAV_DIE_OBJ       = { 19, 0 };	/* PCM death "waaaaa" */

/* wTriggerSound is 0x13-0x1C (19-28) directly -- audio-sndh.md S7. WAV_ENTITY[0] and
   WAV_DIE share track 19: not a bug, the original reuses the death sample as an entity
   trigger sound too.
   All 10 slots are now populated -- pm-baty.md G8 (c) noted slot 9 (track 28 / 0x1C,
   one entity, map 4 only) "stays NULL until ent9.wav ships" because there was no WAV
   to rip a tenth entity sound from. That constraint is gone: the engine already
   contains track 28 like every other track, so this rewrite closes G8 (c) as a side
   effect -- see e_them.c:925 for the (still-needed) trigsnd==0 guard, unrelated to
   this array's size. */
static const sound_t WAV_ENTITY_OBJ[10] = {
	{ 19, 0 }, { 20, 0 }, { 21, 0 }, { 22, 0 }, { 23, 0 },
	{ 24, 0 }, { 25, 0 }, { 26, 0 }, { 27, 0 }, { 28, 0 },
};

sound_t *WAV_GAMEOVER = (sound_t *)&WAV_GAMEOVER_OBJ;
sound_t *WAV_SBONUS2  = (sound_t *)&WAV_SBONUS2_OBJ;
sound_t *WAV_BULLET   = (sound_t *)&WAV_BULLET_OBJ;
sound_t *WAV_BOMBSHHT = (sound_t *)&WAV_BOMBSHHT_OBJ;
sound_t *WAV_EXPLODE  = (sound_t *)&WAV_EXPLODE_OBJ;
sound_t *WAV_STICK    = (sound_t *)&WAV_STICK_OBJ;
sound_t *WAV_WALK     = (sound_t *)&WAV_WALK_OBJ;
sound_t *WAV_CRAWL    = (sound_t *)&WAV_CRAWL_OBJ;
sound_t *WAV_JUMP     = (sound_t *)&WAV_JUMP_OBJ;
sound_t *WAV_PAD      = (sound_t *)&WAV_PAD_OBJ;
sound_t *WAV_BOX      = (sound_t *)&WAV_BOX_OBJ;
sound_t *WAV_BONUS    = (sound_t *)&WAV_BONUS_OBJ;
sound_t *WAV_SBONUS1  = (sound_t *)&WAV_SBONUS1_OBJ;
sound_t *WAV_DIE      = (sound_t *)&WAV_DIE_OBJ;
sound_t *WAV_ENTITY[10] = {
	(sound_t *)&WAV_ENTITY_OBJ[0], (sound_t *)&WAV_ENTITY_OBJ[1],
	(sound_t *)&WAV_ENTITY_OBJ[2], (sound_t *)&WAV_ENTITY_OBJ[3],
	(sound_t *)&WAV_ENTITY_OBJ[4], (sound_t *)&WAV_ENTITY_OBJ[5],
	(sound_t *)&WAV_ENTITY_OBJ[6], (sound_t *)&WAV_ENTITY_OBJ[7],
	(sound_t *)&WAV_ENTITY_OBJ[8], (sound_t *)&WAV_ENTITY_OBJ[9],
};

void sounds_load(void)
{
	/* Nothing to cache -- syssnd_init() already uploaded and armed the engine. */
}

void sounds_free(void)
{
	/* Nothing was allocated per-sound; syssnd_shutdown() tears down the engine. */
}

/*
 * sounds_setMusic
 *
 * Starts a type-0 (song) track: level themes (SND_TRACK_LEVEL0-4, d1=0), the
 * attract-mode title music (SND_TRACK_ATTRACT, d1=1 -- loops), the game-over jingle
 * (SND_TRACK_GAMEOVER, d1=0). A new type-0 track always takes over immediately, so
 * there is no separate "stop the old one" step (see file header).
 */
void sounds_setMusic(U8 track, S8 d1)
{
	syssnd_play_track(track, d1);
}

#endif /* ENABLE_SOUND */

/* eof */
