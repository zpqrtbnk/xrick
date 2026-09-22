/*
 * xrick/include/rd2/rd2_tables.h
 *
 * Rick Dangerous 2 -- trigger table, spawn table and trigger-box record layouts
 * (level-tables.md SS2-3, algo-actors.md S4). These records are read directly out
 * of the embedded level-image blobs (dat_rd2_levelimg.c, RD2_LVLIMG_OFF_SUBMAP_HEADERS
 * onward -- the trigger/spawn tables have no fixed offset of their own, they're
 * chained right after the per-submap header, level-tables.md S1) at their raw byte
 * offsets, so this header gives byte-index constants and bit masks, NOT a C struct --
 * a struct of single-byte fields has no real padding risk on any real compiler, but
 * avoiding one entirely means zero struct-layout assumptions of any kind (matching
 * this port's "never assume" rule for its own code, not just for game facts).
 *
 * Every record starts with a header, is variable-length (spawn) or fixed (trigger,
 * trigger-box), and both tables are terminated by a single 0x00 byte (level-tables.md
 * S1/S2/S3) -- the port's own table-walking code (P3f) must check for that terminator
 * exactly as the original does, not assume a record count.
 */

#ifndef _RD2_TABLES_H
#define _RD2_TABLES_H

#include "system.h"

/* ---- trigger table record: 4 bytes (level-tables.md S2, check_submap_exit_triggers $14362) */
#define RD2_TRIG_SIZE 4
#define RD2_TRIG_B0(r)            ((r)[0])
#define RD2_TRIG_SIDE(r)          ((r)[0] & 3)      /* must equal the player's clamp side, [$14360] */
#define RD2_TRIG_B0_ARM_GROUP     0x20               /* bit5: [$12e14] = -1 */
#define RD2_TRIG_B0_ARM_5ACTORS   0x40               /* bit6: [$144c2] = 1, arms the 5-actor group */
#define RD2_TRIG_B0_MAPDONE_BIT7  0x80
#define RD2_TRIG_B0_MAPDONE_BIT4  0x10
#define RD2_TRIG_Y_TILE(r)        ((r)[1])
#define RD2_TRIG_TARGET_SUBMAP(r) ((r)[2])
#define RD2_TRIG_TARGET_ROW(r)    ((r)[3])

/* ---- spawn table record: 4-byte header + 4*(b3&3) detail bytes (level-tables.md S3,
   scan_enemy_spawn_list $14594) */
#define RD2_SPAWN_HDR_SIZE 4
#define RD2_SPAWN_TYPE(r)         ((r)[0] & 0x7f)
#define RD2_SPAWN_ALREADY(r)      ((r)[0] & 0x80)      /* runtime-only "already spawned" bit; 0 in the data files */
#define RD2_SPAWN_Y_TILE(r)       ((r)[1])
#define RD2_SPAWN_IS_ACTOR(r)     ((r)[2] & 0x80)       /* 1 = spawn an actor now; 0 = trigger/effect record via dispatch_spawn_record */
#define RD2_SPAWN_X_TILE(r)       ((r)[2] & 0x1f)
#define RD2_SPAWN_X_PLUS4(r)      ((r)[2] & 0x20)
#define RD2_SPAWN_MONSTER_PLUS3Y(r) ((r)[2] & 0x40)
#define RD2_SPAWN_NDETAILS(r)     ((r)[3] & 3)
#define RD2_SPAWN_MASKED(r)       ((r)[3] & 0x80)       /* actor +0x12 = -1, selects the masked blitter */
#define RD2_SPAWN_MODE(r)         ((r)[3] & 0x3c)
#define RD2_SPAWN_MODE_FIXED_ANIM 0x20   /* fixed animation script $1466e/$1468a, 500-pt pickup */
#define RD2_SPAWN_MODE_PICKUP_A   0x28   /* frame 0x27, sets HUD slot A (ammo) count to 6 */
#define RD2_SPAWN_MODE_PICKUP_B   0x2c   /* frame 0x28, sets HUD slot B (bombs) count to 6 */
#define RD2_SPAWN_TYPE_BONUS_START 0x78  /* dispatched via $14636, not FUN_00014862/146a0 */
#define RD2_SPAWN_TYPE_BONUS_STOP  0x7c

/* ---- trigger box (spawn record detail block): 4 bytes (algo-actors.md S4, dispatch_spawn_record $14a3c) */
#define RD2_BOX_SIZE 4
#define RD2_BOX_X(r)     ((r)[0])                 /* pixels, raw */
#define RD2_BOX_Y_TILE(r) ((r)[1])                /* y = tile*8 - (scroll & ~7) */
#define RD2_BOX_W8(r)    ((r)[2] & 0x0f)          /* width = (n+1)*8 px */
#define RD2_BOX_H8(r)    (((r)[2] >> 4) & 0x0f)   /* height = (n+1)*8 px */
#define RD2_BOX_TESTMASK(r) ((r)[3] & 0x3f)
#define RD2_BOX_TEST_PLAYER  0x01  /* bit0: check_box_vs_player */
#define RD2_BOX_TEST_SHOT    0x02  /* bit1: laser shot point, only while [$16902] != 0; sets [$115dc]=-1, shot consumed */
#define RD2_BOX_TEST_BOMB    0x04  /* bit2: bomb point, while [$16b12] != 0 && [$12efe] != 0 */
#define RD2_BOX_TEST_MELEE   0x08  /* bit3: melee point, while [$12ef4] != 0; firing plays sound $17 */
#define RD2_BOX_TEST_OBJECT  0x10  /* bit4: any of the 4 object slots (except caller) overlaps */
#define RD2_BOX_TEST_ACTOR   0x20  /* bit5: any live actor slot (except caller) overlaps */
#define RD2_BOX_REPEATABLE(r) ((r)[3] & 0x40)     /* bit6: not latched */
#define RD2_BOX_LATCHED(r)    ((r)[3] & 0x80)     /* bit7: runtime latch state, not in the data files */

/* ---- monster type descriptor: 8 or 12 bytes (level-tables.md S4, FUN_000146a0 $146a0) */
#define RD2_MONDESC_B0(r) ((r)[0])    /* -> actor +0x26 (hitbox_w) */
#define RD2_MONDESC_B1(r) ((r)[1])    /* -> actor +0x28 (hitbox_h) */
#define RD2_MONDESC_B2_FLAGBITS(r) ((r)[2] & 0xc0)  /* -> actor +1 (flags), high 2 bits */
#define RD2_MONDESC_B2(r) ((r)[2])    /* also -> actor +0x30 (alt_flags) whole byte */
/* words at +4/+6/+8/+10 are signed self-relative offsets (descriptor address + word =
   target); +4 = movement script, +6 = animation script, +8/+10 = the alternate pair.
   Read with the level image's big-endian S16 accessor, not a struct field (endianness). */

#endif /* _RD2_TABLES_H */

/* eof */
