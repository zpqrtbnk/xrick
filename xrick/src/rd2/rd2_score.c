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
 * Rick Dangerous 2 -- score, lives, ammo, bombs, HUD counters and the bonus timer,
 * transliterated from the disassembly ($1771c-$178da, $157b4-$15868, read 2026-09-24;
 * kb2/algo-flow.md §7, algo-spawn.md §8).
 *
 * abcd/sbcd: decimal add/subtract of one packed-BCD byte with the X flag. Written for valid
 * BCD operands only; every operand here is (score and bonus bytes start at valid BCD and only
 * change through these instructions; the added/subtracted constants are valid BCD).
 */

#include "rd2_mem.h"
#include "rd2_game.h"

static U8
abcd(U8 dst, U8 src, int *x)
{
	int lo = (dst & 0x0f) + (src & 0x0f) + *x;
	int hi = (dst >> 4) + (src >> 4);
	if (lo > 9) { lo -= 10; hi += 1; }
	*x = hi > 9;
	if (*x) hi -= 10;
	return (U8)((hi << 4) | lo);
}

static U8
sbcd(U8 dst, U8 src, int *x)
{
	int lo = (dst & 0x0f) - (src & 0x0f) - *x;
	int hi = (dst >> 4) - (src >> 4);
	if (lo < 0) { lo += 10; hi -= 1; }
	*x = hi < 0;
	if (*x) hi += 10;
	return (U8)((hi << 4) | lo);
}

/* $1771c new game: score := 0, lives := 6 */
void
rd2_1771c(void)
{
	rd2_wb(0x176e4, 0);
	rd2_wb(0x176e5, 0);
	rd2_wb(0x176e6, 0);
	rd2_wl(0x176e8, 0);
	rd2_ww(0x176ec, 0);
	rd2_ww(0x176f0, 0xffff);
	rd2_ww(0x17710, 6);
	rd2_ww(0x1770e, 0xffff);
}

/* $17760 ammo := 6, bombs := 6 */
void
rd2_17760(void)
{
	rd2_ww(0x176f4, 6);
	rd2_ww(0x176f2, 0xffff);
	rd2_ww(0x17702, 6);
	rd2_ww(0x17700, 0xffff);
}

/* $17782 extra life, at most 6 */
void
rd2_17782(void)
{
	S16 d0 = (S16)(rd2_rws(0x17710) + 1);
	if (d0 > 6)
		d0 = 6;
	rd2_ww(0x17710, (U16)d0);
	rd2_ww(0x1770e, 0xffff);
}

/* $177a8 HUD: score digits, then the 3 counter slots at $176f2/$17700/$1770e (14 bytes each:
   +0 dirty, +2 count, +4 column, +5 icon glyph, +6 six-byte string) */
void
rd2_177a8(void)
{
	U32 a6, a0;
	S16 d7, d0;

	if (rd2_rw(0x176f0) != 0)
		rd2_19272(4, 0, 0x176e8u);
	for (a6 = 0x176f2u, d7 = 2; ; a6 += 0x0e) {
		if (rd2_rw(a6) != 0) {
			a0 = a6 + 6;
			rd2_wl(a0, 0x20202020u);
			rd2_ww(a0 + 4, 0x2020);
			d0 = rd2_rws(a6 + 2);
			if (d0 != 0)
				while (--d0 != -1)                           /* bra -> dbf */
					rd2_wb(a0++, rd2_rb(a6 + 5));
			rd2_19272(rd2_rb(a6 + 4), 0, a6 + 6);
			rd2_ww(a6, 0);
		}
		if (--d7 == -1) break;
	}
}

/* $17810 add score, d0 = BCD long (low 3 bytes added) */
void
rd2_17810(U32 d0)
{
	int x = 0;
	U8 b;

	if (rd2_rw(RD2_DEMO) == 0) {
		rd2_wl(0x17896, d0);
		rd2_wb(0x176e6, abcd(rd2_rb(0x176e6), rd2_rb(0x17899), &x));   /* andi #$ee,ccr ; abcd ×3 */
		rd2_wb(0x176e5, abcd(rd2_rb(0x176e5), rd2_rb(0x17898), &x));
		rd2_wb(0x176e4, abcd(rd2_rb(0x176e4), rd2_rb(0x17897), &x));
		b = rd2_rb(0x176e6);
		rd2_wb(0x176ed, b & 0x0f);
		rd2_wb(0x176ec, b >> 4);
		b = rd2_rb(0x176e5);
		rd2_wb(0x176eb, b & 0x0f);
		rd2_wb(0x176ea, b >> 4);
		b = rd2_rb(0x176e4);
		rd2_wb(0x176e9, b & 0x0f);
		rd2_wb(0x176e8, b >> 4);
	}
	rd2_ww(0x176f0, 0xffff);
}

/* $1789a end-of-game tally */
void
rd2_1789a(void)
{
	S16 d1;

	d1 = rd2_rws(0x17710);
	do                                                       /* body first: lives + 1 times */
		rd2_17810(0x100000u);
	while (--d1 != -1);
	d1 = rd2_rws(0x176f4);
	while (--d1 != -1)
		rd2_17810(0x5000u);
	d1 = rd2_rws(0x17702);
	while (--d1 != -1)
		rd2_17810(0x10000u);
}

/* ---- bonus timer ---- */

/* $157b4 stop (silent) */
void
rd2_157b4(void)
{
	rd2_ww(0x157ac, 0);
}

/* $157be start */
void
rd2_157be(U32 a0)
{
	(void)a0;
	if (rd2_rw(0x157ac) != 0)
		return;
	rd2_ww(0x157ac, 0xffff);
	rd2_wl(0x157b0, 0x2000);
	rd2_ww(0x157ae, 0x19);
	rd2_1a6aa(0x11, 0);
}

/* $157f4 stop with award; latches the spawn record */
void
rd2_157f4(U32 a0)
{
	if (rd2_rw(0x157ac) == 0)
		return;
	rd2_ww(0x157ac, 0);
	rd2_17810(rd2_rl(0x157b0));
	rd2_1a6aa(1, 0);
	rd2_wb(a0, rd2_rb(a0) | 0x80);
}

/* $15826 per frame: -10 every 25 frames (two low BCD bytes only) */
void
rd2_15826(void)
{
	int x = 0;

	if (rd2_rw(0x157ac) == 0)
		return;
	rd2_ww(0x157ae, (U16)(rd2_rw(0x157ae) - 1));
	if (rd2_rw(0x157ae) != 0)
		return;
	rd2_ww(0x157ae, 0x19);
	rd2_wb(0x157b3, sbcd(rd2_rb(0x157b3), rd2_rb(0x1586d), &x));       /* andi #$ee,ccr ; sbcd ×2 */
	rd2_wb(0x157b2, sbcd(rd2_rb(0x157b2), rd2_rb(0x1586c), &x));
	if (rd2_rl(0x157b0) == 0)
		rd2_ww(0x157ac, 0);
}

/* eof */
