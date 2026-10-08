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
 * Rick Dangerous 2 -- game_main ($10992) and the routines it calls, RAM model
 * (port-rd2.md §7). Game state lives in emulated RAM (rd2_mem.h) at the original
 * addresses; this header names the cells game_main touches and declares every
 * routine by its original address. Routines not yet transliterated are marked TODO
 * with their phase; the game will not LINK until all exist (no stub bodies).
 */

#ifndef _RD2_GAME_H
#define _RD2_GAME_H

#include "system.h"

/* ---- RAM cells (algo-flow.md §1; disassembly of $10992-$10c22) */
#define RD2_MAP_PLAYING   0x1239cu  /* word, 1..5 */
#define RD2_MAP_LOADED    0x1239eu  /* word */
#define RD2_PICKER_ROWS   0x17992u  /* word, pristine 4; game_main writes 5 ($10af2) */
#define RD2_PICKER_CHOICE 0x17994u  /* word */
#define RD2_DEMO          0x3efb6u  /* word, -1 while a demo plays */
#define RD2_DEMO_ENDED    0x3efb8u  /* word */
#define RD2_JOY           0x1a4fbu  /* byte: bit7 fire, 0 up, 1 down, 2 left, 3 right */
#define RD2_KEY           0x1a4fcu  /* byte: last raw IKBD byte (make, or break |$80) */
#define RD2_MAPDONE       0x115e0u  /* word */
#define RD2_SHOT_HIT      0x115dcu  /* word */
#define RD2_RICK_DEAD     0x12e2au  /* word */
#define RD2_RICK_Y        0x16960u  /* word */
#define RD2_LIVES         0x17710u  /* word */
#define RD2_SKEY_LATCH    0x10b5cu  /* word inside the code segment */
#define RD2_ID_REMAP      0x1a5ceu  /* word */

/* ---- routines, by original address */
void rd2_178dc(void);  /* title / attract, rd2_flow.c */
void rd2_17a46(void);  /* level picker, rd2_flow.c */
void rd2_1771c(void);  /* score := 0, lives := 6, rd2_score.c */
void rd2_123a0(void);  /* [$1239c] := [$17994]; $17760, rd2_level.c */
void rd2_19388(void);  /* clear both screens, rd2_render.c */
void rd2_123b0(void);  /* load_map_if_changed, rd2_load.c */
void rd2_123ca(void);  /* loader past its banner, rd2_load.c */
void rd2_194ce(U16 d0); /* banner d0, rd2_render.c */
void rd2_19106(void);  /* install palette $18ee6, rd2_render.c */
void rd2_19116(U32 a0); /* install palette a0, rd2_render.c */
void rd2_19134(void);  /* fade in, rd2_render.c */
void rd2_1919e(void);  /* fade out, rd2_render.c */
void rd2_19316(U16 d0, U16 d1, U32 a0);  /* glyph string into the off-screen picture, rd2_render.c */
void rd2_1925a(U32 a0);  /* record-list text draw, rd2_render.c */
void rd2_17086(void);  /* scene sprites, rd2_render.c */
void rd2_1709e(void);  /* sprites into the off-screen picture, rd2_render.c */
void rd2_18caa(void);  /* off-screen picture to the draw screen, rd2_render.c */
void rd2_17760(void);  /* ammo/bombs := 6, rd2_score.c */
void rd2_142a0(void);  /* level start, rd2_level.c */
void rd2_14222(void);  /* re-arm the demo reader, rd2_level.c */
void rd2_14458(U16 d0, U16 d1);  /* submap loader, rd2_level.c */
void rd2_1300e(void);  /* checkpoint save, rd2_level.c */
void rd2_18516(void);  /* PRNG reseed, rd2_level.c */
void rd2_18538(void);  /* PRNG step, rd2_level.c */
void rd2_16474(void);  /* tile window generation, rd2_render.c */
void rd2_16630(void);  /* reset anim slots + full bitmap render, rd2_render.c */
void rd2_18186(U16 d0); /* run_scene(d0), rd2_flow.c */
void rd2_18abe(U16 d0, U16 d1);  /* left-exit transition, rd2_render.c */
void rd2_18bb2(U16 d0, U16 d1);  /* right-exit transition, rd2_render.c */
void rd2_14594(void);  /* spawn scan, rd2_spawn.c */
void rd2_14542(void);  /* seek the spawn table, rd2_spawn.c */
int  rd2_14962(U32 *a6);  /* free actor slot (carry), rd2_spawn.c */
int  rd2_14970(U32 *a6);  /* free object slot (carry), rd2_spawn.c */
int  rd2_14998(U32 a6);   /* off-screen (carry), rd2_spawn.c */
void rd2_149f0(U32 a6);   /* despawn, forced, rd2_spawn.c */
void rd2_14a12(U32 a6);   /* despawn, rd2_spawn.c */
int  rd2_14a3c(U32 a0, U32 a6);  /* dispatch_spawn_record: trigger boxes (carry), rd2_spawn.c */
void rd2_157be(U32 a0);   /* bonus timer start, rd2_score.c */
void rd2_157f4(U32 a0);   /* bonus timer stop with award, rd2_score.c */
void rd2_15b3c(void);     /* init_actor_group, rd2_group.c */
int  rd2_171bc(U32 a6);   /* animation script step (carry = jump), rd2_actors.c */
int  rd2_172fa(U32 a6, S16 *dx, S16 *dy);  /* movement script step (carry = jump), rd2_actors.c */
int  rd2_1726e(U32 a6, S16 *dx, S16 *dy);  /* group/scene movement step (carry = jump), rd2_actors.c */
void rd2_1704c(U32 a6);   /* clear records from a6 to the sentinel, rd2_actors.c */
void rd2_171a4(S16 d0);   /* y += d0 for every live record, rd2_actors.c */
void rd2_17810(U32 d0);   /* add BCD score, rd2_score.c */
void rd2_1a6aa(U16 d0, U16 d1);  /* play_sound(id, d1), rd2_snd.c */
void rd2_15bc0(void);  /* actor group, rd2_group.c */
void rd2_14d48(void);  /* actors, rd2_actors.c */
void rd2_150a2(void);  /* objects, rd2_objects.c */
void rd2_15826(void);  /* bonus timer tick, rd2_score.c */
void rd2_157b4(void);  /* bonus timer off, rd2_score.c */
void rd2_13096(void);  /* Rick, rd2_player.c */
void rd2_13e14(void);  /* laser shot, rd2_player.c */
void rd2_13e98(void);  /* bomb, rd2_player.c */
void rd2_16658(void);  /* scroll, rd2_render.c */
void rd2_18dac(void);  /* animated tiles, rd2_render.c */
void rd2_18782(void);  /* background to screen, rd2_render.c */
void rd2_170b6(void);  /* sprites to screen, rd2_render.c */
void rd2_177a8(void);  /* HUD, rd2_score.c */
void rd2_19272(U16 d0, U16 d1, U32 a0);  /* glyph string a0 at column d0, row d1, rd2_render.c */
void rd2_19216(void);  /* frame wait + flip, rd2_sys.c */
void rd2_19234(void);  /* page flip, rd2_sys.c */
void rd2_191e6(void);  /* frame wait, rd2_sys.c */
void rd2_14362(void);  /* exit-trigger scan, rd2_level.c */
void rd2_1a5d0(void);  /* stop all sound, rd2_snd.c */
void rd2_149c2(void);  /* despawn objects and actors, rd2_spawn.c */
void rd2_142fc(void);  /* respawn: $13060 then $14300, rd2_level.c */
void rd2_17bf4(void);  /* scene 1 unless demo, rd2_flow.c */
void rd2_17782(void);  /* extra life, rd2_score.c */
void rd2_1789a(void);  /* end-of-game tally, rd2_score.c */
void rd2_17bda(void);  /* ending: sound 9 + scene 2 unless demo, rd2_flow.c */
void rd2_17c06(void);  /* game over, rd2_flow.c */
void rd2_17f22(void);  /* hall-of-fame entry, rd2_flow.c */

/* ---- entry point */
void rd2_game_run(void);   /* boot $10000 + game_main $10992, replaces rd1's game_run() when -game 2 (P6) */

/* rd2_frame_step: one game_main frame ($10a54-$10bec); where game_main goes next */
#define RD2_STEP_FRAME      0  /* the next frame */
#define RD2_STEP_MAPDONE    1
#define RD2_STEP_END_OF_RUN 2
#define RD2_STEP_TITLE      3  /* ESC */
#define RD2_STEP_PICK       4  /* fire during the game's own attract demo */
int  rd2_frame_step(void);

#endif /* _RD2_GAME_H */

/* eof */
