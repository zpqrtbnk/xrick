/*
 * xrick/include/rd2/rd2_record.h
 *
 * Rick Dangerous 2 -- the one 88-byte record format shared by every moving thing:
 * the 4 object slots, Rick's laser shot, Rick himself, 4 debris slots, the bomb, and
 * the 6 actor slots -- 17 records total, in one contiguous chain the original game
 * walks as `$167a2 .. $16d79` (algo-player.md S1, graphics.md S5's 17-record draw
 * list, algo-flow.md S2). This is a REIMPLEMENTATION struct (natural C field order/
 * sizes, like rd1/ents.h's ent_t), not a byte-exact memory overlay of the 68000
 * original -- the port does not need bit-for-bit address compatibility, only the
 * same field MEANINGS, which is what every comment below cites.
 *
 * Field provenance (every offset is a "+0xNN" cited directly from the source doc,
 * not inferred): algo-actors.md S1 (generic actor-table fields, "+N" notation),
 * algo-objects.md "Fields used" (object-table fields, same notation), algo-player.md
 * S1 (Rick's own record -- given as absolute addresses `[$1697e]` etc.; only two of
 * those, `$1697c`/`$1697e`, fall inside Rick's own record range `[$1695a, $169b2)`
 * -- the rest of Rick's named state, e.g. `[$12e10]`, is OUTSIDE any record, in a
 * separate player-state global block: see rd2_player.h, not this file).
 *
 * A few offsets are documented from more than one subsystem and agree with each
 * other (e.g. +0x22 is "animation wait counter" per actors, "animation phase" per
 * objects, and Rick's own walk-cycle counter -- same slot, three names for how each
 * subsystem drives it); those get ONE field here, commented with all three roles.
 * Where only ONE subsystem documents an offset, the field is commented as such and
 * is presumed reserved/unused for the others (not verified either way -- no source
 * claims a value for it there).
 *
 * +0x00's split: the original's "+0 word state" and "+1 byte F" are NOT two
 * independent fields -- +1 is literally the low byte of the +0 word (68000 is
 * big-endian; FUN_00014862 explicitly writes "b0&3 as a word at +0", landing in
 * byte +1). This struct does not need to reproduce that overlap (it's a property
 * of the original's addressing, not of the record's logical content), so `state`
 * and `flags` are ordinary separate fields here -- P3's code writes both whenever
 * the original writes the combined word.
 *
 * Script pointers (+0x16/+0x1e/+0x32/+0x36): the original game stores a raw 68000
 * address there. This port has TWO disjoint script address spaces instead of the
 * original's one flat memory -- level-image-relative offsets for the 420 per-map
 * monster scripts (inside dat_rd2_levelimg.c) and absolute addresses for the 21
 * in-program scripts (dat_rd2_scripts_inprogram.c). Representing "which space + what
 * offset" is a script-VM design question, not a documented game fact -- deferred to
 * P3f (rd2_actors.c), which owns script resolution; this header only reserves the
 * field as an opaque U32 "script reference" (0 = none, matching the original's "if
 * pointer == 0: return" checks in both script formats, algo-actors.md S2).
 */

#ifndef _RD2_RECORD_H
#define _RD2_RECORD_H

#include "system.h"

typedef struct {
	S16 state;      /* +0x00 word: actors: slot state (0=free,>0=in use,<0=table-end sentinel,
	                   bmi/beq tested on the WHOLE word); objects: kind (1,2,3; 0=free);
	                   Rick: active flag. algo-actors.md S1, algo-objects.md, algo-player.md S1 */
	U8  flags;      /* +0x01 byte F: actor-table behaviour flags (bit1=frozen/flipped,
	                   bits2-5 &0x3c=mode, bits2-4 &0x1c=hit-reaction class, bit6=hurts
	                   Rick on contact, bit7=extra shot-object check, bit0=set at
	                   construction). Low byte of the original's +0 word -- see file
	                   comment. algo-actors.md S1 */
	S16 x;          /* +0x02 word: x. All three docs agree */
	S16 y;          /* +0x06 word: y (screen-space: world y - (scroll & ~7) for actors;
	                   integer part of a 16.16 fixed-point y for objects/Rick, whose
	                   fraction is y_frac below). algo-actors.md S1, algo-objects.md,
	                   algo-player.md S1 */
	U16 y_frac;     /* +0x08 word: fractional part of y (objects/Rick only; +0x06 is also
	                   read as a long spanning this word -- algo-objects.md, algo-player.md S1) */
	S16 dx;         /* +0x0a word: horizontal velocity/step (object table only -- FUN_00014862
	                   inits it to 2; algo-objects.md). Actor-table movement is script-driven
	                   dx/dy applied directly, not stored here as a persistent velocity */
	S16 vy;         /* +0x0c word: vertical velocity, signed 8.8 fixed point (objects/Rick
	                   gravity; algo-objects.md, algo-player.md S1) */
	U16 frame;      /* +0x0e word: current sprite frame id ($fe = draw nothing). All three
	                   docs agree; graphics.md S4/S5 (draw routine reads this generically) */
	S16 hitflash;   /* +0x10 word: <0 = skip draw when a screen-flash bit is set, else cleared
	                   normally; >0 = draw as a silhouette (hit flash), then cleared. Read
	                   generically by the draw routine for all 17 records -- graphics.md S4 */
	S16 masked;     /* +0x12 word: != 0 = draw with the masked blitter $1952c (else the plain
	                   blitter). Read generically by the draw routine. algo-actors.md S1,
	                   algo-objects.md, graphics.md S4 */
	S16 draw2;      /* +0x14 word: second-draw-pass flag, cleared once consumed ($170f2).
	                   Read generically by the draw routine. algo-actors.md S1, graphics.md S5 */
	U32 move_script;    /* +0x16: movement-script reference, opaque -- see file comment.
	                        algo-actors.md S1/S2 */
	U16 move_counter;   /* +0x1a word: movement script repeat counter. algo-actors.md S1/S2 */
	U32 anim_script;    /* +0x1e: animation-script reference, opaque -- see file comment.
	                        algo-actors.md S1/S2 */
	U16 anim_counter;   /* +0x22 word: MULTI-ROLE slot -- actor-table byte-code VM's
	                        "wait" counter (algo-actors.md S1/S2); object-table's simple
	                        animation "phase" accumulator, mod 0x20 (algo-objects.md);
	                        Rick's own walk-cycle counter, +1/+2/+4 per frame mod 0x28/
	                        0x20, -1 when idle (algo-player.md S1). Same byte offset in
	                        all three, different drivers per subsystem */
	U16 hitbox_w;   /* +0x26 word: hit-box width (monster descriptor byte 0). algo-actors.md S1 */
	U16 hitbox_h;   /* +0x28 word: hit-box height (monster descriptor byte 1). algo-actors.md S1 */
	U32 spawn_rec;  /* +0x2a: pointer to the spawn record this actor/object came from (its
	                   spawned bit is cleared on despawn). Opaque -- see rd2_tables.h for the
	                   representation. algo-actors.md S1 */
	U8  runtime_s;  /* +0x2e byte S: actor-table runtime state bits (3,4=phase-swap pending,
	                   5=despawn at next loop wrap, 6=swapped-out). algo-actors.md S1/S3 */
	U8  alt_flags;  /* +0x30 byte: alternate F (the "second profile" swapped in via S bits
	                   3/4). algo-actors.md S1/S3 */
	U32 alt_move_script; /* +0x32: alternate movement-script reference. algo-actors.md S1 */
	U32 alt_anim_script; /* +0x36: alternate animation-script reference. algo-actors.md S1 */
	S16 last_dx;    /* +0x3a word: last applied dx (actors: from the movement script;
	                   graphics/collision-probe code also reads this as the platform's dx
	                   when this record is stood on -- algo-actors.md S1/S6) */
	S16 last_dy;    /* +0x3c word: last applied dy. algo-actors.md S1/S6 */
	U16 frame_base;    /* +0x3e word: object-table only -- base frame id added to the computed
	                       animation offset (FUN_00014862 sets this from table $14854).
	                       algo-objects.md */
	S16 on_platform;   /* +0x40 word: object-table only -- "on a moving platform" flag,
	                       carries the platform's dx into +4a. algo-objects.md */
	S16 on_floor;      /* +0x42 word: object-table only -- "landed" flag. algo-objects.md */
	S16 reserved_44;   /* +0x44 word: object-table only -- read/cleared but not otherwise used
	                       by the transcribed code ("unused here" -- algo-objects.md). Kept as
	                       a named reserved field rather than silently dropped */
	S16 slow_marker;    /* +0x46 word: object-table only -- map-2-specific "slow" marker.
	                        algo-objects.md */
	S16 conveyor_marker; /* +0x48 word: object-table only -- map-4-specific "conveyor" marker.
	                        algo-objects.md */
	S16 platform_dx;    /* +0x4a word: object-table only -- inherited dx from a moving-platform
	                        actor stood on. algo-objects.md */
	S16 hit_flag;   /* +0x4c word: SHARED slot -- object-table's "hit by an actor" flag (set by
	                   $15740, algo-actors.md S3's mark_object_slots_touched); "flags used by
	                   the object slots / by static actors" per algo-actors.md S1. Two related
	                   but not proven-identical roles; both docs cite the same offset */
	S16 dying;      /* +0x4e word: SHARED slot -- dying flag, both object and (static) actor
	                   records; != 0 shows the death animation. algo-actors.md S1, algo-objects.md */
	S16 climbing;   /* +0x50 word: object-table only -- "climbing" flag. algo-objects.md */
	U16 osc_counter; /* +0x52 word: object-table only -- oscillation-movement running counter
	                    (kind 1 objects). algo-objects.md */
	U16 osc_period;  /* +0x54 word: SHARED slot -- object-table's oscillation period (kind 1:
	                    ((b3&0x3c)<<1)+8, set at construction); algo-actors.md S1 documents the
	                    identical formula for "type >= 0x75 kind 1", so this is the same field
	                    described from the constructor's side (algo-actors.md) and the runtime
	                    side (algo-objects.md) */
	U16 stun_timer;  /* +0x56 word: object-table only -- stun timer. algo-objects.md */

	/* Rick's own record only (algo-player.md S1); undocumented for object/actor records --
	   no source states a value or role for these offsets there, so treated as reserved/
	   unused for those two subsystems, not verified either way. */
	S16 facing_rick;    /* +0x24 word ([$1697e] = Rick's record base + 0x24): 1 = facing left,
	                        0 = right; also selects the shot direction and +0x0d frame offset.
	                        algo-player.md S1 */
} rd2_record_t;

/* the 17-record chain, in the original's own layout order (algo-player.md S1,
   graphics.md S5): $167a2 objects x4, $16902 laser shot, $1695a Rick, $169b2 debris x4,
   $16b12 bomb, $16b6a actors x6 */
#define RD2_OBJECT_SLOTS 4
#define RD2_DEBRIS_SLOTS 4
#define RD2_ACTOR_SLOTS  6

#endif /* _RD2_RECORD_H */

/* eof */
