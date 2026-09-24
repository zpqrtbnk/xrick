/*
 * xrick/include/rd2/rd2_record.h
 *
 * Rick Dangerous 2 -- the 88-byte ($58) record shared by every moving thing, as byte
 * OFFSETS into emulated RAM (RAM-model port, port-rd2.md §7). Accessed with
 * rd2_rw(rec + RD2_R_X) etc., so word/byte overlaps (e.g. +0 word / +1 byte F) behave as
 * on the 68000.
 *
 * The 17-record chain (algo-player.md §1, graphics.md §5): $167a2 objects x4, $16902
 * laser shot, $1695a Rick, $169b2 debris x4, $16b12 bomb, $16b6a actors x6.
 *
 * Field meanings: algo-actors.md §1, algo-objects.md "Fields used", algo-player.md §1.
 */

#ifndef _RD2_RECORD_H
#define _RD2_RECORD_H

#define RD2_REC_SIZE      0x58

#define RD2_REC_OBJECTS   0x167a2u  /* 4 object slots */
#define RD2_REC_SHOT      0x16902u  /* Rick's laser shot */
#define RD2_REC_RICK      0x1695au
#define RD2_REC_DEBRIS    0x169b2u  /* 4 debris slots */
#define RD2_REC_BOMB      0x16b12u
#define RD2_REC_ACTORS    0x16b6au  /* 6 actor slots */
#define RD2_OBJECT_SLOTS  4
#define RD2_DEBRIS_SLOTS  4
#define RD2_ACTOR_SLOTS   6

#define RD2_R_STATE     0x00  /* word: actors 0=free >0=used <0=end sentinel; objects kind 1..3; Rick active */
#define RD2_R_F         0x01  /* byte F = low byte of +0: bit0 set at construction, bit1 frozen/flipped,
                                 &$3c mode, &$1c hit-reaction class, bit6 hurts Rick on contact,
                                 bit7 detonates a falling bomb it touches ($15776, algo-spawn.md §7) */
#define RD2_R_X         0x02  /* word */
#define RD2_R_Y         0x06  /* word (long with +8 = 16.16 y for objects/Rick) */
#define RD2_R_YFRAC     0x08  /* word */
#define RD2_R_DX        0x0a  /* word: object dx */
#define RD2_R_VY        0x0c  /* word: 8.8 vertical velocity (objects/Rick) */
#define RD2_R_FRAME     0x0e  /* word: sprite frame, $fe = none */
#define RD2_R_HITFLASH  0x10  /* word: <0 flicker, >0 silhouette once (graphics.md §4) */
#define RD2_R_MASKED    0x12  /* word: !=0 masked blitter $1952c */
#define RD2_R_DRAW2     0x14  /* word: second-draw-pass flag ($170f2) */
#define RD2_R_MOVESCR   0x16  /* long: movement script pointer */
#define RD2_R_MOVECNT   0x1a  /* word */
#define RD2_R_ANIMSCR   0x1e  /* long: animation script pointer */
#define RD2_R_ANIMCNT   0x22  /* word: actor VM wait / object anim phase / Rick walk cycle */
#define RD2_R_FACING    0x24  /* word: Rick only ([$1697e]), 1 = facing left */
#define RD2_R_HBW       0x26  /* word: hit-box width */
#define RD2_R_HBH       0x28  /* word: hit-box height */
#define RD2_R_SPAWNREC  0x2a  /* long: originating spawn record */
#define RD2_R_S         0x2e  /* byte S: bits 3,4 phase-swap pending, 5 despawn at wrap, 6 swapped-out */
#define RD2_R_ALTF      0x30  /* byte: alternate F */
#define RD2_R_ALTMOVE   0x32  /* long */
#define RD2_R_ALTANIM   0x36  /* long */
#define RD2_R_LASTDX    0x3a  /* word */
#define RD2_R_LASTDY    0x3c  /* word */
#define RD2_R_FRAMEBASE 0x3e  /* word: objects */
#define RD2_R_ONPLAT    0x40  /* word: objects */
#define RD2_R_ONFLOOR   0x42  /* word: objects */
#define RD2_R_44        0x44  /* word: objects, cleared, otherwise unused (algo-objects.md) */
#define RD2_R_SLOW      0x46  /* word: objects, map-2 marker */
#define RD2_R_CONVEYOR  0x48  /* word: objects, map-4 marker */
#define RD2_R_PLATDX    0x4a  /* word: objects */
#define RD2_R_HIT       0x4c  /* word: objects hit flag ($15740) / static actors */
#define RD2_R_DYING     0x4e  /* word */
#define RD2_R_CLIMB     0x50  /* word: objects */
#define RD2_R_OSCCNT    0x52  /* word: objects */
#define RD2_R_OSCPER    0x54  /* word */
#define RD2_R_STUN      0x56  /* word: objects */

#endif /* _RD2_RECORD_H */

/* eof */
