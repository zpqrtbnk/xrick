/*
 * xrick/include/rd2/rd2_sound.h
 *
 * Rick Dangerous 2 -- sound dispatch ids (sound-ref.md S2 dispatch table, S7 "id ->
 * game event"). Only the ids sound-ref.md S7 actually names get a symbolic constant
 * here; the rest of the 0-91 range is used as a bare number by P3/P5 with a comment
 * citing its call site, exactly as sound-ref.md itself leaves them ("three call sites
 * take the id from actor script data rather than an immediate, so a few more ids are
 * reachable in-game than are listed" -- S7). Do not invent a name for an unlisted id.
 *
 * Engine entry points, state cells and the dispatch table's own address/stride are in
 * dat_rd2_sndh_engine.h (generated), not here -- this header is the higher-level
 * "which id means what" layer sound-ref.md S7 documents, which the generator script
 * has no reason to know about.
 */

#ifndef _RD2_SOUND_H
#define _RD2_SOUND_H

#define RD2_SND_LEVEL_START_FANFARE 0   /* only site passing D1=1 */
#define RD2_SND_TIMED_EFFECT_END    1
#define RD2_SND_LEVEL_TRANSITION    2
#define RD2_SND_CHECKPOINT          3
/* 4-8: per-map level-start music, id = [$1239c]+3 (map index 0-based), D1=0.
   algo-flow.md S5. Not individually named -- computed, not a fixed id. */
#define RD2_SND_HUD_TICK            9
#define RD2_SND_PLAYER_DEATH        10
#define RD2_SND_RESPAWN_BOUNCE      11
#define RD2_SND_SCROLL_DONE_OR_LASER 16  /* dual role: scroll/transition complete AND the
                                             laser-fire sound (update_player_rick $13d58,
                                             algo-player.md S4) */
#define RD2_SND_TIMED_EFFECT_START  17
#define RD2_SND_GENERIC_IMPACT      19
#define RD2_SND_ACTOR_AI_TRIGGER_A  20
#define RD2_SND_ACTOR_AI_TRIGGER_B  21
#define RD2_SND_SPAWN_APPEAR        23
#define RD2_SND_OBJECT_COLLISION    24
#define RD2_SND_HARD_LANDING_MAP3   25
#define RD2_SND_HARD_LANDING_MAP5   26  /* dead code -- no map 5 exists, hnk-system.md S7 */
#define RD2_SND_COUNTDOWN_EXPIRY    34
#define RD2_SND_JUMP                45
#define RD2_SND_WALL_CONTACT        48
#define RD2_SND_FOOTSTEP            49
#define RD2_SND_CLIMB_OR_FALL       50

#endif /* _RD2_SOUND_H */

/* eof */
