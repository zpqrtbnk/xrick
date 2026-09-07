/*
 * xrick/include/ents.h
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

#ifndef _ENTS_H
#define _ENTS_H

#include "system.h"

#include "rects.h"

extern void ents_paintAll();

#define ENT_XRICK ent_ents[1]

#define ENT_NBR_ENTDATA 0x4a
#define ENT_NBR_SPRSEQ 0x88
#define ENT_NBR_MVSTEP 0x310

#define ENT_ENTSNUM 0x0c

/*
 * Vertical bound for entity despawn / out-of-range tests. review-log.md R3.2.
 *
 * ST  0x142 -- render_sprites `cmp.w #0x142,D2w` @ 0x4B0C8, and the identical test in
 *              scripted_trap_update @ 0x4D352.
 * PC  0x140 -- `CMP AX,0x140` @ 0x10E3 and @ 0x2742; no 0x142 compare exists anywhere in
 *              ibmpc_cs.bin.
 *
 * A genuine two-unit divergence. Note the ARCHITECTURE also differs: the ST despawns
 * centrally in render_sprites for every entity, while the PC and the port test per entity
 * inside each action handler. Only the constant is switched here.
 */
/*
 * Submap re-entry X. xref.md 'Submap re-entry X'.
 * PC  0xE2 (enter from the right) / 0x04 (enter from the left)
 *     MOV word[SI+2],0x00E2 @0x19B9 ; MOV word[SI+2],0x0004 @0x19C9
 * ST  0xE6 / 0x02  -- process_level_transition_point, algo-level.md reposition
 */
#ifdef PLATFORM_ST
#define SUBMAP_REENTRY_RIGHT 0xE6
#define SUBMAP_REENTRY_LEFT  0x02
#else
#define SUBMAP_REENTRY_RIGHT 0xE2
#define SUBMAP_REENTRY_LEFT  0x04
#endif

/*
 * Player trigger-box probe, X offset. xref.md 'Player trigger-box probe X'.
 * PC  0x0C -- ADD AL,0x0C @ 0x1536 / 0x229B / 0x22CF / 0x27B1
 * ST  0x0B -- nPosX + 0x0B @ 0x4D19A
 */
#ifdef PLATFORM_ST
#define RICK_PROBE_DX 0x0B
#else
#define RICK_PROBE_DX 0x0C
#endif

/*
 * Stick-jab stun, in frames. review-log.md R3.12 / xref.md.
 * ST  0x19 (25) -- move.b #0x19,(0x4a,A0) @ 0x4D574
 * PC  0x14 (20) -- MOV byte[SI+0x2E],0x14 @ 0x237E and 0x28D6
 */
#ifdef PLATFORM_ST
#define ENT_STUN 0x19
#else
#define ENT_STUN 0x14
#endif

#ifdef PLATFORM_ST
#define ENT_YMAX 0x0142
#else
#define ENT_YMAX 0x0140
#endif

/*
 * Entity Y despawn test -- review-log.md R4.7.
 *
 *   ST 0x4D34A  cmp.w #0,D1 / bge / cmp.w #0x142,D1 / ble ok
 *                 -> dead when  y < 0 || y > 0x142
 *   PC 0x2976    cmp bh,0 / jz ok / cmp bh,1 / jnz dead / cmp bl,0x40 / jnc dead
 *                 -> dead when  y >= 0x140  (a negative y has bh = 0xff, so it is
 *                    caught by the same test; this is exact for all 16-bit y)
 *
 * Note the port used `y > ENT_YMAX` with ENT_YMAX = 0x140 on the PC side, which kept
 * y == 0x140 alive where the PC kills it -- off by one.
 *
 * Deliberately NOT applied to the other six ENT_YMAX sites: `CMP r8,0x40` occurs at
 * exactly TWO addresses in the whole PC segment (0x298B, 0x2A0E), i.e. only the two
 * sites below, so the PC encodes its other bound checks differently and those remain
 * unverified.
 */
#ifdef PLATFORM_ST
#define ENT_YDEAD(y) ((y) < 0 || (y) > 0x0142)
#else
#define ENT_YDEAD(y) ((y) < 0 || (y) >= 0x0140)
#endif

/*
 * flags for ent_ents[e].n  ("yes" when set)
 *
 * ENT_LETHAL: is entity lethal?
 */
#define ENT_LETHAL 0x80

/*
 * flags for ent_ents[e].flag  ("yes" when set)
 *
 * ENT_FLG_ONCE: should the entity run once only?
 * ENT_FLG_STOPRICK: does the entity stops rick (and goes to slot zero)?
 * ENT_FLG_LETHALR: is entity lethal when restarting?
 * ENT_FLG_LETHALI: is entity initially lethal?
 * ENT_FLG_TRIGBOMB: can entity be triggered by a bomb?
 * ENT_FLG_TRIGBULLET: can entity be triggered by a bullet?
 * ENT_FLG_TRIGSTOP: can entity be triggered by rick stop?
 * ENT_FLG_TRIGRICK: can entity be triggered by rick?
 */
#define ENT_FLG_ONCE 0x01
#define ENT_FLG_STOPRICK 0x02
#define ENT_FLG_LETHALR 0x04
#define ENT_FLG_LETHALI 0x08
#define ENT_FLG_TRIGBOMB 0x10
#define ENT_FLG_TRIGBULLET 0x20
#define ENT_FLG_TRIGSTOP 0x40
#define ENT_FLG_TRIGRICK 0x80

typedef struct {
  U8 n;          /* b00 */
  /*U8 b01;*/    /* b01 in ASM code but never used */
  /* review-log.md R3.1: SIGNED. The ST holds position as a signed word -- render_sprites
     despawns on `cmp.w #-0x8,D1w / bge` (a signed compare against -8), the main loop
     transitions on `player.nPosX <= 0` (0x4DD2E), and the velocity is sign-extended
     (`ext.l D7`). Declared U16 these fields make six `< 0` tests dead code. The `b02`/`w04`
     comments are the port authors' own PC notes and are NOT evidence (see
     xrick/re/provenance.md) -- the ST accesses both as words at every site. */
  S16 x;         /* position */
  S16 y;         /* position */
  U8 sprite;     /* b08 - sprite number */
  /*U16 w0C;*/   /* w0C in ASM code but never used */
  U8 w;          /* b0E - width */
  U8 h;          /* b10 - height */
  U16 mark;      /* w12 - number of the mark that created the entity */
  U8 flags;      /* b14 */
  U16 trig_x;    /* b16 - position of trigger box */
  U16 trig_y;    /* w18 - position of trigger box */
  U16 xsave;     /* b1C */
  U16 ysave;     /* w1E */
  U16 sprbase;   /* w20 */
  U16 step_no_i; /* w22 */
  U16 step_no;   /* w24 */
  S16 c1;        /* b26 */
  S16 c2;        /* b28 */
  U8 ylow;       /* b2A */
  S16 offsy;     /* w2C */
  U8 latency;    /* b2E */
  U8 prev_n;     /* new */
  U16 prev_x;    /* new */
  U16 prev_y;    /* new */
  U8 prev_s;     /* new */
  U8 front;      /* new */
  U8 trigsnd;    /* new */
} ent_t;

typedef struct {
  U8 w, h;
  U16 spr, sni;
  U8 trig_w, trig_h;
  U8 snd;
} entdata_t;

typedef struct {
  U8 count;
  S8 dx, dy;
} mvstep_t;

extern ent_t ent_ents[ENT_ENTSNUM + 1];
extern entdata_t ent_entdata[ENT_NBR_ENTDATA];
extern rect_t *ent_rects;
extern U8 ent_sprseq[ENT_NBR_SPRSEQ];
extern mvstep_t ent_mvstep[ENT_NBR_MVSTEP];

extern void ent_reset(void);
extern void ent_actvis(U8, U8);
extern void ent_draw(void);
extern void ent_clprev(void);
extern void ent_action(void);

#endif

/* eof */
