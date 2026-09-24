/*
 * xrick/src/rd2/rd2_collide.c
 *
 * Rick Dangerous 2 -- collision probes and hit tests, transliterated from the disassembly
 * ($15f1e-$16461, $14b7a-$14d47, read 2026-09-24; kb2/algo-collision.md). RAM model: the
 * probe cells $15f0e-$15f1c, $161ca/$161cc and $12e16 are RAM, so they persist between
 * callers as on the ST (algo-collision.md §11).
 *
 * The routines save/restore D0-D7/A0-A1 (movem), so each C function only returns what the
 * 68000 code returns: the carry flag (1 = set) for the tests, nothing for the probes.
 * Word arithmetic is done on 16 bits (W()), compares are signed like the bxx used.
 * Row loops enter at their dbf (bra -> dbf), so they run d2 times, not d2+1.
 */

#include "rd2_mem.h"
#include "rd2_collide.h"

#define ATTR  0x65200u   /* tile attribute table */
#define WIN   0x65300u   /* tile window, 32 bytes per row */

#define W(v)  ((S16)(v))

/* (0,a0,d4.w) after moveq #0,d4 ; move.b (a1),d4 */
static U8 attr(U32 a1) { return rd2_rb(ATTR + rd2_rb(a1)); }

/* $1643e compute_tile_map_ptr: in d7 = x offset; out a1, d7 */
static U32
rd2_1643e(S16 *d7)
{
	U32 a1 = WIN;
	S16 d6 = W((rd2_rw(RD2_PY) & 0xfff8) * 4);             /* andi.w #$fff8 ; add.w ×2 */
	a1 += (U32)(S32)d6;                                      /* adda.w */
	*d7 = W(*d7 + rd2_rws(RD2_PX));
	d6 = (S16)(*d7 >> 3);                                    /* asr.w #3 */
	a1 += (U32)(S32)d6;
	return a1;
}

/* $161ce aabb_overlap_test: box (d0,d1,d2,d3) vs box (d4,d5,d6,d7) */
int
rd2_161ce(S16 d0, S16 d1, S16 d2, S16 d3, S16 d4, S16 d5, S16 d6, S16 d7)
{
	if (W(d6 + d4) <= d0) return 0;
	if (W(d2 + d0) <= d4) return 0;
	if (W(d7 + d5) <= d1) return 0;
	if (W(d3 + d1) <= d5) return 0;
	return 1;
}

/* $14c8c point_in_box: point (d6,d7) vs box (d0,d1,d2,d3) */
int
rd2_14c8c(S16 d0, S16 d1, S16 d2, S16 d3, S16 d6, S16 d7)
{
	d6 = W(d6 - d0);
	if (d6 < 0 || d6 > d2) return 0;                        /* bmi ; bgt */
	d7 = W(d7 - d1);
	if (d7 < 0 || d7 > d3) return 0;
	return 1;
}

/* actor part: $160a6-$161c2 (bomb = 0) and $1632a-$16436 (bomb = 1) */
static void
probe_actors(int bomb)
{
	U32 a0;

	if (rd2_rw(RD2_PACT) == 0)
		return;
	for (a0 = 0x16b6au; ; a0 += 0x58) {
		S16 st = rd2_rws(a0);
		S16 d0, d1, d2, d3, d5;
		if (st < 0) return;                                  /* bmi */
		if (st == 0) continue;
		if (!(st & 0x80)) continue;                          /* btst #7,d0 (byte +1) */
		d0 = rd2_rws(a0 + 0x02);
		d1 = rd2_rws(a0 + 0x06);
		d2 = rd2_rws(a0 + 0x26);
		if (st & 0x40) {                                     /* solid */
			d3 = rd2_rws(a0 + 0x28);
			if (!bomb) {
				if (!rd2_161ce(d0, d1, d2, d3, W(rd2_rws(RD2_PX) + 4),
				               W(rd2_rws(RD2_PY) + rd2_rws(RD2_BOXDY)), 0x10, rd2_rws(RD2_BOXH)))
					continue;
			} else if (!rd2_161ce(d0, d1, d2, d3, W(rd2_rws(RD2_PX) + 8), rd2_rws(RD2_PY), 8, 0x10))
				continue;
			rd2_wb(RD2_PRES, rd2_rb(RD2_PRES) | 0x02);       /* bset #1 */
			if (d1 > rd2_rws(RD2_PPLY)) continue;
			d5 = W(rd2_rws(RD2_PY) + (bomb ? 0x0f : 0x14));
			d3 = W(rd2_extb(rd2_rb(RD2_PVY)) + 8);           /* move.b ; ext.w ; addq #8 */
			d3 = W(d3 + d1);
			if (d5 > d3) continue;
			rd2_ww(RD2_PPLY, (U16)d1);
			rd2_ww(RD2_PPDX, 0);
			rd2_ww(RD2_PPDY, 0);
			rd2_wb(RD2_PRES, rd2_rb(RD2_PRES) | 0x40);       /* bset #6 */
		} else {                                             /* one-way platform */
			if (rd2_rws(RD2_PVY) < 0) continue;
			d3 = W(rd2_rb(RD2_PVY) + 8);                     /* moveq #0 ; move.b ; addq #8 */
			if (!rd2_161ce(d0, d1, d2, d3, W(rd2_rws(RD2_PX) + (bomb ? 8 : 4)),
			               W(rd2_rws(RD2_PY) + (bomb ? 0x0f : 0x14)), bomb ? 8 : 0x10, 1))
				continue;
			if (d1 > rd2_rws(RD2_PPLY)) continue;
			rd2_wb(RD2_PRES, rd2_rb(RD2_PRES) | 0x40);
			rd2_ww(RD2_PPLY, (U16)d1);
			rd2_ww(RD2_PPDX, rd2_rw(a0 + 0x3a));
			rd2_ww(RD2_PPDY, rd2_rw(a0 + 0x3c));
		}
	}
}

/* $1608e / $16312: vy < 0 clears bit 2 of the result, then the actor part */
static void
probe_sign_actors(int bomb)
{
	if (rd2_rws(RD2_PVY) < 0)
		rd2_wb(RD2_PRES, rd2_rb(RD2_PRES) & 0xfb);
	probe_actors(bomb);
}

/* $15ffc: unaligned (3 columns); a1 = first body row, d2 = dbf count */
static void
rd2_15ffc(U32 a1, S16 d2)
{
	U8 d0 = 0, d1 = 0, d3;
	while (--d2 != -1) {
		d0 |= attr(a1);
		d1 |= attr(a1 + 1);
		d0 |= attr(a1 + 2);
		a1 += 0x20;
	}
	d3 = (U8)((d0 & 0x22) | (d1 & 0x2a));
	d0 = (U8)(attr(a1) | attr(a1 + 2));
	d1 = attr(a1 + 1);
	d3 |= (U8)((d0 & 0x27) | (d1 & 0x7f));
	rd2_wb(RD2_PRES, d3);
	probe_sign_actors(0);
}

/* $16052: aligned (2 columns) */
static void
rd2_16052(U32 a1, S16 d2)
{
	U8 d0 = 0, d3;
	while (--d2 != -1) {
		d0 |= attr(a1);
		d0 |= attr(a1 + 1);
		a1 += 0x20;
	}
	d3 = (U8)(d0 & 0x2a);
	d0 = (U8)(attr(a1) | attr(a1 + 1));
	d3 |= (U8)(d0 & 0x7f);
	rd2_wb(RD2_PRES, d3);
	probe_sign_actors(0);
}

/* $15fba query_tile_and_actor_collision */
void
rd2_15fba(void)
{
	S16 d2, d7;
	U32 a1;

	rd2_ww(RD2_BOXDY, 0);
	rd2_ww(RD2_BOXH, 0x15);
	rd2_ww(RD2_PPLY, 0x7fff);
	d2 = 3;
	if ((rd2_rws(RD2_PY) & 7) < 4)
		d2 = 2;
	d7 = 4;
	a1 = rd2_1643e(&d7);
	if ((d7 & 7) == 0)
		rd2_16052(a1, d2);
	else
		rd2_15ffc(a1, d2);
}

/* $15f1e crouch probe */
void
rd2_15f1e(void)
{
	S16 d2, d7;
	U32 a1;
	U8 d0;

	rd2_ww(RD2_BOXDY, 5);
	rd2_ww(RD2_BOXH, 0x10);
	rd2_ww(RD2_PPLY, 0x7fff);
	rd2_ww(0x12e16, 0);
	d2 = 2;
	if ((rd2_rws(RD2_PY) & 7) < 4)
		d2 = 1;
	d7 = 4;
	a1 = rd2_1643e(&d7);
	if ((d7 & 7) != 0) {
		d0 = (U8)(attr(a1) | attr(a1 + 1) | attr(a1 + 2));
		a1 += 0x20;                                          /* (a1)+ ×2 ; lea $1e */
		if (d0 & 0x02)
			rd2_ww(0x12e16, 0xffff);
		rd2_15ffc(a1, d2);
	} else {
		d0 = (U8)(attr(a1) | attr(a1 + 1));
		a1 += 0x20;                                          /* (a1)+ ; lea $1f */
		if (d0 & 0x02)
			rd2_ww(0x12e16, 0xffff);
		rd2_16052(a1, d2);
	}
}

/* $16278 8-px probe (bomb) */
void
rd2_16278(void)
{
	S16 d2, d7;
	U32 a1;
	U8 d0, d3;

	rd2_ww(RD2_PPLY, 0x7fff);
	d2 = 1;
	if ((rd2_rws(RD2_PY) & 7) != 0)
		d2 = 2;
	d7 = 8;
	a1 = rd2_1643e(&d7);
	d0 = 0;
	if ((d7 & 7) != 0) {                                     /* 2 columns */
		while (--d2 != -1) {
			d0 |= attr(a1);
			d0 |= attr(a1 + 1);
			a1 += 0x20;
		}
		d3 = (U8)(d0 & 0x23);
		d0 = (U8)(attr(a1) | attr(a1 + 1));
		d3 |= (U8)(d0 & 0x27);
	} else {                                                 /* 1 column */
		while (--d2 != -1) {
			d0 |= attr(a1);
			a1 += 0x20;
		}
		d3 = (U8)(d0 & 0x2b);
		d3 |= (U8)(attr(a1) & 0x27);
	}
	rd2_wb(RD2_PRES, d3);
	probe_sign_actors(1);
}

/* $161fe point probe */
void
rd2_161fe(void)
{
	S16 d7 = 0;
	U32 a0, a1;
	U8 d0;

	a1 = rd2_1643e(&d7);
	d0 = attr(a1);
	rd2_wb(RD2_PRES, d0);
	if (d0 & 0x02)
		return;
	for (a0 = 0x16b6au; ; a0 += 0x58) {
		S16 st = rd2_rws(a0);
		if (st < 0) return;
		if (st == 0) continue;
		if ((rd2_rb(a0 + 1) & 0xc0) != 0xc0) continue;
		if (rd2_14c8c(rd2_rws(a0 + 0x02), rd2_rws(a0 + 0x06), rd2_rws(a0 + 0x26), rd2_rws(a0 + 0x28),
		              rd2_rws(RD2_PX), rd2_rws(RD2_PY))) {
			rd2_wb(RD2_PRES, rd2_rb(RD2_PRES) | 0x02);
			return;
		}
	}
}

/* $14b7a check_box_vs_player */
int
rd2_14b7a(S16 d0, S16 d1, S16 d2, S16 d3)
{
	S16 d7;
	if (rd2_rw(0x12e2a) != 0) return 0;
	d7 = W(rd2_rws(0x1695c) + 0x14);
	if (d0 >= d7) return 0;
	d7 = W(d7 - 0x10);
	if (W(d0 + d2) <= d7) return 0;
	d7 = W(rd2_rws(0x16960) + 0x15);
	if (d1 >= d7) return 0;
	if (rd2_rw(0x12e18) == 0) {
		d7 = rd2_rws(0x16960);
		if (W(d1 + d3) <= d7) return 0;
		return 1;
	}
	d7 = W(d7 - 0x10);
	if (W(d1 + d3) < d7) return 0;                           /* blt: inclusive */
	return 1;
}

/* $14bee laser shot front point */
int
rd2_14bee(S16 d0, S16 d1, S16 d2, S16 d3)
{
	if (rd2_rw(0x16902) == 0) return 0;
	return rd2_14c8c(d0, d1, d2, d3, rd2_rws(0x12efa), rd2_rws(0x12efc));
}

/* $14c20 bomb explosion point */
int
rd2_14c20(S16 d0, S16 d1, S16 d2, S16 d3)
{
	if (rd2_rw(0x16b12) == 0) return 0;
	if (rd2_rw(0x12efe) == 0) return 0;
	return rd2_14c8c(d0, d1, d2, d3, rd2_rws(0x12f02), rd2_rws(0x12f04));
}

/* $14c5a melee point */
int
rd2_14c5a(S16 d0, S16 d1, S16 d2, S16 d3)
{
	if (rd2_rw(0x12ef4) == 0) return 0;
	return rd2_14c8c(d0, d1, d2, d3, rd2_rws(0x12ef6), rd2_rws(0x12ef8));
}

/* $14cb4 box vs object record a2 (fixed 16x21) */
int
rd2_14cb4(S16 d0, S16 d1, S16 d2, S16 d3, U32 a2)
{
	S16 d7;
	if (rd2_rw(a2 + 0x4e) != 0) return 0;
	d7 = W(rd2_rws(a2 + 0x02) + 0x14);
	if (d0 >= d7) return 0;
	d7 = W(d7 - 0x10);
	if (W(d0 + d2) <= d7) return 0;
	d7 = W(rd2_rws(a2 + 0x06) + 0x15);
	if (d1 >= d7) return 0;
	d7 = rd2_rws(a2 + 0x06);
	if (W(d1 + d3) <= d7) return 0;
	return 1;
}

/* $14d04 box vs actor record a2 (its own size) */
int
rd2_14d04(S16 d0, S16 d1, S16 d2, S16 d3, U32 a2)
{
	S16 d7;
	d7 = W(rd2_rws(a2 + 0x02) + rd2_rws(a2 + 0x26));
	if (d0 >= d7) return 0;
	d7 = rd2_rws(a2 + 0x02);
	if (W(d0 + d2) <= d7) return 0;
	d7 = W(rd2_rws(a2 + 0x06) + rd2_rws(a2 + 0x28));
	if (d1 >= d7) return 0;
	d7 = rd2_rws(a2 + 0x06);
	if (W(d1 + d3) <= d7) return 0;
	return 1;
}

/* eof */
