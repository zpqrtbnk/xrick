/*
 * xrick/include/rd2/rd2_tileattr.h
 *
 * Rick Dangerous 2 -- tile attribute bits and the collision-probe result byte
 * (algo-actors.md S6: `query_tile_and_actor_collision` $15fba). The raw 256-byte
 * per-map attribute table lives inside dat_rd2_levelimg.c at RD2_LVLIMG_OFF_TILE_ATTR
 * (rd2_record.h's file comment style: this header names bit MEANINGS only, not storage).
 *
 * Two related but distinct byte values:
 *  - the raw per-tile attribute byte (indexed by tile id, one table per map). Observed
 *    values over all 4 maps: 00 02 03 04 08 1c 20 80 82 84 -- bit 7 is present in the
 *    raw table (0x80/0x82/0x84) but EVERY consumer masks it out, so it is never seen
 *    in a combined probe result (algo-actors.md S6).
 *  - the combined probe RESULT byte [$15f14], built by OR-ing several probed tiles'
 *    attribute bytes together under direction-dependent masks (aligned: body rows
 *    "&0x2a", feet row "&0x7f"; unaligned: outer columns "&0x22"/middle "&0x2a" for
 *    body, "&0x27" outer/"&0x7f" middle for feet -- algo-actors.md S6 has the exact
 *    per-row/column masks; NOT reproduced as constants here since they're structural
 *    to the probe routine itself, not a per-tile fact -- P3f/P4a implement the probe
 *    from that prose directly). Bit 6 of the RESULT is added separately by the
 *    actor-platform test and is NEVER set by any raw tile value (none of the 10
 *    observed raw values has bit 6 set) -- so it is documented as probe-only below.
 *
 * 120 live samples (kb2 gap #7, closed 2026-09-22) confirm every observed combined
 * result decomposes exactly into these bits with zero exceptions -- algo-actors.md S6.
 */

#ifndef _RD2_TILEATTR_H
#define _RD2_TILEATTR_H

/* bits present in the raw per-tile attribute byte (and, after OR-combination, in the
   probe result byte too -- same bit positions, algo-actors.md S6 "Attribute bits as
   consumed") */
#define RD2_TILEATTR_SURFACE  0x01  /* per-map surface property: map1 jump -0x200, map2
                                        conveyor, map3 bounce, map4 walk speed 1 (map5:
                                        rebound -- documented but map 5 is never real,
                                        PLAN.md T28; kept for completeness of the source fact) */
#define RD2_TILEATTR_SOLID    0x02  /* blocks sideways movement / head */
#define RD2_TILEATTR_FLOOR    0x04  /* floor, tested on the feet row */
#define RD2_TILEATTR_LADDER   0x08  /* ladder tile present */
#define RD2_TILEATTR_LADDERTOP 0x10 /* ladder-top entry */
#define RD2_TILEATTR_KILL     0x20  /* kills the player / destroys an object */
#define RD2_TILEATTR_BIT7_UNUSED 0x80 /* present in the raw table (0x80/0x82/0x84 seen);
                                          every consumer masks it out -- never seen live */

/* probe-result-only bit, never set by a raw tile attribute value */
#define RD2_PROBE_PLATFORM_ACTOR 0x40  /* standing on / one-way-platformed by a live actor
                                            slot, set by the actor half of the probe, not
                                            by any tile (algo-actors.md S6) */

/* object-code reaction to the combined probe result (FUN_000150c0, algo-actors.md S6
   "Reaction of the object code"): bit5 destroys the object, bit6 = landed on ground/
   platform, bits 1+2 = slopes/walls that reset vertical speed -- these are the SAME
   bit constants above (RD2_TILEATTR_KILL, RD2_PROBE_PLATFORM_ACTOR, RD2_TILEATTR_SOLID
   | RD2_TILEATTR_FLOOR), listed again here only as a cross-reference, not new bits. */

#endif /* _RD2_TILEATTR_H */

/* eof */
