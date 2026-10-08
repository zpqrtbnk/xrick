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
 * Rick Dangerous 2 -- spawn machinery, transliterated from the disassembly ($14542-$14b79,
 * read 2026-09-24; kb2/algo-spawn.md §1-§5, §7). Spawn and box records are read from and
 * written into the level image in RAM at $53400 (runtime bits, algo-spawn.md §9).
 */

#include "rd2_mem.h"
#include "rd2_game.h"
#include "rd2_collide.h"
#include "rd2_sys.h"

#define SCROLL   0x16462u   /* word, scroll y */
#define SPAWNTAB 0x144c4u   /* long, current submap's spawn table */
#define SPAWNPTR 0x144c8u   /* long, scan pointer */
#define FORCED   0x14592u   /* word, forced spawn */
#define GROUP    0x144c2u   /* word, actor group armed */

#define W(v)  ((S16)(v))

/* record length: 4 + 4 * (b3 & 3) (andi.b #3 ; lsl.b #2 ; adda.w ; lea 4) */
static U32 next_rec(U32 a0) { return a0 + (U32)((rd2_rb(a0 + 3) & 3) << 2) + 4; }

/* $14542: seek the spawn table to the first record at or below the scroll */
void
rd2_14542(void)
{
	U32 a0 = rd2_rl(SPAWNTAB);
	if (a0 != 0) {
		while (rd2_rb(a0) != 0) {
			if (W(rd2_rb(a0 + 1) << 3) >= rd2_rws(SCROLL))
				break;
			a0 = next_rec(a0);
		}
	}
	rd2_wl(SPAWNPTR, a0);
}

/* $14962 / $14970: first free slot of the actor / object table (carry = found) */
int
rd2_14962(U32 *a6)
{
	S16 d0;
	*a6 = 0x16b6au;
	for (d0 = 5; ; ) {
		S16 st = rd2_rws(*a6);
		if (st < 0) return 0;
		if (st == 0) return 1;
		*a6 += 0x58;
		if (--d0 == -1) return 0;
	}
}

int
rd2_14970(U32 *a6)
{
	S16 d0;
	*a6 = 0x167a2u;
	for (d0 = 3; ; ) {
		S16 st = rd2_rws(*a6);
		if (st < 0) return 0;
		if (st == 0) return 1;
		*a6 += 0x58;
		if (--d0 == -1) return 0;
	}
}

/* $146a0 monster constructor, a0 = spawn record */
static void
rd2_146a0(U32 a0)
{
	U32 a6, a1;
	U16 d0;
	U8 d1, mode;

	/* d0.w on entry = b0 (moveq #0 ; move.b (a0) in $14636); $14962 preserves d0 */
	d0 = rd2_rb(a0);
	if (!rd2_14962(&a6))
		return;
	rd2_wb(a6 + 0x2e, 0);
	rd2_wl(a6 + 0x2a, a0);
	mode = rd2_rb(a0 + 3) & 0x3c;
	d0 = (U16)((d0 & 0xff00) | mode);                        /* move.b (3,a0),d0 ; andi.b #$3c */
	if (mode == 0x20) {                                      /* $14820 */
		rd2_wl(a6 + 0x1e, 0x1466eu);
		if (rd2_rw(RD2_MAP_PLAYING) == 4)
			rd2_wl(a6 + 0x1e, 0x1468au);
		rd2_ww(a6 + 0x0e, 0x40);
		goto STATIC;
	}
	if (mode == 0x28) {                                      /* $14800 */
		rd2_wl(a6 + 0x1e, 0);
		rd2_ww(a6 + 0x0e, 0x27);
		goto STATIC;
	}
	if (mode == 0x2c) {                                      /* $14810 */
		rd2_wl(a6 + 0x1e, 0);
		rd2_ww(a6 + 0x0e, 0x28);
		goto STATIC;
	}

	d0 = (U16)((U8)((rd2_rb(a0) & 0x7f) - 1));               /* moveq #0 ; andi.b ; subi.b #1 */
	d0 = (U16)(d0 << 1);                                     /* lsl.w #1 */
	a1 = 0x53400u + (U32)rd2_extw(d0);                       /* adda.w */
	a1 += (U32)rd2_extw(rd2_rw(a1));                         /* adda.w (a1),a1 */
	rd2_ww(a6 + 0x26, rd2_rb(a1));
	rd2_ww(a6 + 0x28, rd2_rb(a1 + 1));
	d1 = rd2_rb(a1 + 2) & 0xc0;
	mode = rd2_rb(a0 + 3) & 0x3c;
	if (mode == 0x10) {
		rd2_wb(a6 + 0x2e, 0x60);
		if (rd2_rb(a0 + 3) & 0x40)
			rd2_wl(a6 + 0x2a, 0);
	}
	mode = (U8)(mode | 1 | d1);
	rd2_wb(a6 + 0x01, mode);
	rd2_wb(a6 + 0x30, (U8)((mode & 0x3f) | (U8)(rd2_rb(a1 + 2) << 2)));
	rd2_wl(a6 + 0x16, a1 + (U32)rd2_extw(rd2_rw(a1 + 4)));
	rd2_ww(a6 + 0x1a, 0);
	rd2_wl(a6 + 0x1e, a1 + (U32)rd2_extw(rd2_rw(a1 + 6)));
	rd2_ww(a6 + 0x22, 0);
	rd2_171bc(a6);
	rd2_wl(a6 + 0x1e, a1 + (U32)rd2_extw(rd2_rw(a1 + 6)));  /* d0 kept by $171bc */
	rd2_ww(a6 + 0x22, 0);
	rd2_wl(a6 + 0x32, a1 + (U32)rd2_extw(rd2_rw(a1 + 8)));
	rd2_wl(a6 + 0x36, a1 + (U32)rd2_extw(rd2_rw(a1 + 10)));
	goto COMMON;

STATIC:                                                      /* $14840 */
	rd2_ww(a6 + 0x22, 0);
	rd2_ww(a6 + 0x00, d0);
	rd2_ww(a6 + 0x4e, 0);

COMMON:                                                      /* $14796 */
	d0 = (U16)((rd2_rb(a0 + 2) & 0x1f) << 3);
	if (rd2_rb(a0 + 2) & 0x20)
		d0 = (U16)(d0 + 4);
	rd2_ww(a6 + 0x02, d0);
	d0 = (U16)(rd2_rb(a0 + 1) << 3);
	d0 = (U16)(d0 - (U16)(rd2_rw(SCROLL) & 0xfff8));        /* andi.b #$f8,d1 ; sub.w */
	if (rd2_rb(a0 + 2) & 0x40)
		d0 = (U16)(d0 + 3);
	rd2_ww(a6 + 0x06, d0);
	rd2_ww(a6 + 0x10, 0);
	rd2_ww(a6 + 0x14, 0);
	rd2_ww(a6 + 0x12, 0);
	if (rd2_rb(a0 + 3) & 0x80)
		rd2_ww(a6 + 0x12, 0xffff);
	rd2_wb(a0, rd2_rb(a0) | 0x80);
}

/* $14862 object constructor */
static void
rd2_14862(U32 a0)
{
	U32 a6, a1;
	U16 d0;
	U8 d1;

	if (!rd2_14970(&a6))
		return;
	rd2_ww(a6 + 0x4c, 0);
	rd2_ww(a6 + 0x4e, 0);
	d0 = rd2_rb(a0);
	d1 = (U8)d0;
	d0 &= 0x03;
	rd2_ww(a6 + 0x00, d0);
	d1 = (U8)(((U8)((d1 & 0x0c) - 4)) >> 1);                 /* andi.b ; subi.b ; lsr.b */
	a1 = 0x14854u + d1;                                      /* adda.w, d1.w < $80 */
	rd2_ww(a6 + 0x0e, rd2_rw(a1));
	rd2_ww(a6 + 0x3e, rd2_rw(a1));
	if (d0 == 1) {
		U8 b = (U8)(((rd2_rb(a0 + 3) & 0x3c) << 1) + 8);     /* byte arithmetic */
		rd2_ww(a6 + 0x54, b);
		rd2_ww(a6 + 0x52, 0);
	}
	rd2_ww(a6 + 0x08, 0);
	rd2_ww(a6 + 0x0c, 0x100);
	rd2_ww(a6 + 0x0a, 2);
	rd2_ww(a6 + 0x42, 0);
	rd2_ww(a6 + 0x44, 0);
	rd2_ww(a6 + 0x46, 0);
	rd2_ww(a6 + 0x48, 0);
	rd2_ww(a6 + 0x50, 0);
	rd2_ww(a6 + 0x4a, 0);
	rd2_ww(a6 + 0x56, 0);
	d0 = (U16)((rd2_rb(a0 + 2) & 0x1f) << 3);
	if (rd2_rb(a0 + 2) & 0x20)
		d0 = (U16)(d0 + 4);
	rd2_ww(a6 + 0x02, d0);
	d0 = (U16)(rd2_rb(a0 + 1) << 3);
	d0 = (U16)(d0 - (U16)(rd2_rw(SCROLL) & 0xfff8));
	d0 = (U16)(d0 + 3);
	rd2_ww(a6 + 0x06, d0);
	rd2_ww(a6 + 0x10, 0);
	rd2_ww(a6 + 0x14, 0);
	rd2_ww(a6 + 0x12, 0);
	if (rd2_rb(a0 + 3) & 0x80)
		rd2_ww(a6 + 0x12, 0xffff);
	rd2_wl(a6 + 0x2a, a0);
	rd2_wb(a0, rd2_rb(a0) | 0x80);
}

/* $14636 spawn dispatch */
static void
rd2_14636(U32 a0)
{
	U16 d0 = rd2_rb(a0);
	if (d0 == 0x78)
		rd2_157be(a0);
	else if (d0 == 0x7c)
		rd2_157f4(a0);
	else if ((S16)d0 < 1)
		;
	else if ((S16)d0 <= 0x74)
		rd2_146a0(a0);
	else
		rd2_14862(a0);
}

/* $14594 scan_enemy_spawn_list */
void
rd2_14594(void)
{
	U32 a0;
	S16 d7, d1;
	U8 b0;

	if (rd2_rw(GROUP) != 0) {
		if (rd2_rw(0x16b6a) == 0)
			rd2_15b3c();
		return;
	}
	a0 = rd2_rl(SPAWNPTR);
	if (a0 == 0)
		return;
	d7 = W(rd2_rws(SCROLL) + 0x128);
	for (;; a0 = next_rec(a0)) {
		b0 = rd2_rb(a0);
		if (b0 == 0) return;
		d1 = W(rd2_rb(a0 + 1) << 3);
		if (d1 >= d7) return;
		if (b0 & 0x80) continue;
		if (rd2_rb(a0 + 2) & 0x80) {                         /* actor record */
			if (rd2_rw(FORCED) == 0) {
				d1 = W(d1 - rd2_rws(SCROLL));
				if (d1 >= 0x28 && d1 < 0x110)
					continue;
			}
			rd2_14636(a0);
		} else if (rd2_14a3c(a0, 0))
			rd2_14636(a0);
	}
}

/* $14998 off-screen test (carry) */
int
rd2_14998(U32 a6)
{
	S16 d0 = rd2_rws(a6 + 0x06);
	if (d0 < 0 || d0 >= 0x128) return 1;
	d0 = rd2_rws(a6 + 0x02);
	if (d0 < 0 || d0 > 0xe8) return 1;
	return 0;
}

/* $149f0 despawn (forced) */
void
rd2_149f0(U32 a6)
{
	U32 a0;
	if (rd2_rw(a6) == 0) return;
	rd2_ww(a6, 0);
	a0 = rd2_rl(a6 + 0x2a);
	if (a0 != 0)
		rd2_wb(a0, rd2_rb(a0) & 0x7f);
}

/* $14a12 despawn: the record's spawned bit is cleared only if its b3 bit 6 is clear */
void
rd2_14a12(U32 a6)
{
	U32 a0;
	if (rd2_rw(a6) == 0) return;
	rd2_ww(a6, 0);
	a0 = rd2_rl(a6 + 0x2a);
	if (a0 != 0 && !(rd2_rb(a0 + 3) & 0x40))
		rd2_wb(a0, rd2_rb(a0) & 0x7f);
}

/* $149c2: despawn the 4 objects and the 6 actors */
void
rd2_149c2(void)
{
	U32 a6;
	S16 d7;
	for (a6 = 0x167a2u, d7 = 3; ; a6 += 0x58) {
		rd2_149f0(a6);
		if (--d7 == -1) break;
	}
	for (a6 = 0x16b6au, d7 = 5; ; a6 += 0x58) {
		rd2_149f0(a6);
		if (--d7 == -1) break;
	}
}

/* $14a3c dispatch_spawn_record: trigger boxes of record a0; a6 = calling record or 0 (carry = fired) */
int
rd2_14a3c(U32 a0, U32 a6)
{
	U32 a1, a2;
	S16 d7, d6, d0, d1, d2, d3;

	d7 = rd2_rb(a0 + 3) & 3;
	if (d7 == 0)
		return 0;
	d7 = W(d7 - 1);
	a1 = a0 + 4;
	for (;;) {
		U8 m;
		d3 = W((U8)((rd2_rb(a1 + 2) >> 4) + 1) << 3);
		d1 = W(rd2_rb(a1 + 1) << 3);
		d1 = W(d1 - (rd2_rws(SCROLL) & 0xfff8));
		if (d1 < 0) {                                        /* bpl */
			d3 = W(d3 + d1);
			if (d3 < 0) goto CLEAR;                          /* bmi */
			d1 = 0;
		}
		d2 = W((U8)((rd2_rb(a1 + 2) & 0x0f) + 1) << 3);
		d0 = rd2_rb(a1);
		m = rd2_rb(a1 + 3);
		rd2_sys_box(RD2_BOX_TRIGGER, d0, d1, d2, d3);        /* host: highlight cheat */
		if ((m & 0x01) && rd2_14b7a(d0, d1, d2, d3)) goto HIT;
		if ((m & 0x02) && rd2_14bee(d0, d1, d2, d3)) {
			rd2_ww(RD2_SHOT_HIT, 0xffff);
			goto HIT;
		}
		if ((m & 0x04) && rd2_14c20(d0, d1, d2, d3)) goto HIT;
		if ((m & 0x08) && rd2_14c5a(d0, d1, d2, d3)) goto HIT;
		if (m & 0x10) {
			for (a2 = 0x167a2u, d6 = 3; ; a2 += 0x58) {
				if (rd2_rw(a2) != 0 && a2 != a6 && rd2_14cb4(d0, d1, d2, d3, a2)) goto HIT;
				if (--d6 == -1) break;
			}
		}
		if (rd2_rb(a1 + 3) & 0x20) {
			for (a2 = 0x16b6au, d6 = 5; ; a2 += 0x58) {
				if (rd2_rw(a2) != 0 && a2 != a6 && rd2_14d04(d0, d1, d2, d3, a2)) goto HIT;
				if (--d6 == -1) break;
			}
		}
	CLEAR:                                                   /* $14b28 */
		rd2_wb(a1 + 3, rd2_rb(a1 + 3) & 0x7f);
	NEXT:                                                    /* $14b2e */
		a1 += 4;
		if (--d7 == -1)
			return 0;
		continue;
	HIT:                                                     /* $14b40 */
		if (rd2_rb(a1 + 3) & 0x80) goto NEXT;
		if (rd2_rb(a1 + 3) & 0x40) return 1;
		rd2_wb(a1 + 3, rd2_rb(a1 + 3) | 0x80);
		if (rd2_rb(a1 + 3) & 0x08)
			rd2_1a6aa(0x17, 0);
		return 1;
	}
}

/* eof */
