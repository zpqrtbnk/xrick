/*
 * xrick/include/rd2/rd2_script.h
 *
 * Rick Dangerous 2 -- the two enemy byte-code script formats (algo-actors.md S2,
 * kb2/decode_scripts.py's docstring). Big-endian words, read directly from the
 * embedded blobs (dat_rd2_levelimg.c for the 420 per-map monster scripts,
 * dat_rd2_scripts_inprogram.c for the 21 in-program ones) -- no struct, same
 * reasoning as rd2_tables.h: these are variable-length record streams, not
 * fixed-layout records.
 *
 * MOVEMENT script (record +0x16 pointer, +0x1a repeat counter -- rd2_record.h):
 *   word n != 0, -1 : record (n.w, dx.b, dy.b)   apply (dx,dy) for n calls, then +4
 *   word 0          : record (0.w, off.w)        jump: pointer += signed off; carry=1
 *   word -1 (0xffff): record (-1.w, sound.w)      play sound if y in [0x23,0x148]; +4, continue
 *
 * ANIMATION script (record +0x1e pointer, +0x22 wait counter):
 *   word f >= 0     : record (f.w)                show frame f, +2 (no wait)
 *   word -1 (0xffff): record (-1.w, off.w)         jump relative to this record; continue, carry=1
 *   word -3 (0xfffd): record (-3.w, sound.w)       sound as above; +4, continue
 *   any other < 0   : record (m.w, ticks.w, f.w)   show frame f for `ticks` calls (canonically
 *                                                   m == 0xfffe -- the "show" marker is always
 *                                                   0xfffe over all 442 checked records)
 */

#ifndef _RD2_SCRIPT_H
#define _RD2_SCRIPT_H

#include "system.h"

#define RD2_SCRIPT_JUMP_WORD_MOVE   0x0000
#define RD2_SCRIPT_SOUND_WORD_MOVE  0xffff
#define RD2_SCRIPT_JUMP_WORD_ANIM   0xffff
#define RD2_SCRIPT_SOUND_WORD_ANIM  0xfffd
#define RD2_SCRIPT_SHOW_MARKER_ANIM 0xfffe  /* every "any other < 0" record observed uses exactly this */

#define RD2_SCRIPT_SOUND_Y_MIN 0x23  /* sound plays only while y in [0x23, 0x148] */
#define RD2_SCRIPT_SOUND_Y_MAX 0x148

#endif /* _RD2_SCRIPT_H */

/* eof */
