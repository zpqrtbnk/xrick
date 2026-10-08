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
 * Rick Dangerous 2 -- level start, respawn, submap loader, exit triggers, PRNG,
 * transliterated from the disassembly ($123a0, $12f08-$13094, $14222-$144c0,
 * $18516-$18560; read 2026-09-24; kb2/algo-flow.md §5, §9).
 *
 * $14300/$14458/$14434 take their inputs in D0-D7; here they are C parameters.
 */

#include "rd2_mem.h"
#include "rd2_game.h"

#define W(v)  ((S16)(v))

/* $123a0: [$1239c] := [$17994] ; $17760 */
void
rd2_123a0(void)
{
	rd2_ww(RD2_MAP_PLAYING, rd2_rw(RD2_PICKER_CHOICE));
	rd2_17760();
}

/* $14222: re-arm the demo reader (demo only) */
void
rd2_14222(void)
{
	if (rd2_rw(RD2_DEMO) == 0)
		return;
	rd2_ww(0x3efb8, 0);
	rd2_wl(0x3efba, 0x3efc0u);
	rd2_wb(0x3efbe, 0);
	rd2_wb(0x3efbf, 0);
}

/* $18516: PRNG reseed */
void
rd2_18516(void)
{
	U32 d7 = 0x16051966u, d6 = 0x09121967u;
	d6 <<= 8;                                                /* asl.l #8 */
	d7 = (d7 & 0xffff0000u) | (U16)(d6 - 7);                 /* move.w d6,d7 ; subq.w #7 */
	d6 = (d6 & 0xffff0000u) | (U16)(d6 ^ d7);                /* eor.w d7,d6 */
	rd2_wl(0x18562, d6);
	rd2_wl(0x18566, d7);
}

/* $18538: PRNG step (state only; d6/d7 restored) */
void
rd2_18538(void)
{
	U32 d6 = rd2_rl(0x18566), d7 = rd2_rl(0x18562);         /* move ×2 ; exg */
	d7 = (d7 << 3) | (d7 >> 29);                             /* rol.l #3 */
	d7 = (d7 & 0xffff0000u) | (U16)(d7 - 7);                 /* subq.w #7 */
	d7 = (d7 & 0xffff0000u) | (U16)(d7 ^ d6);                /* eor.w d6,d7 */
	rd2_wl(0x18562, d6);
	rd2_wl(0x18566, d7);
}

/* $12f08 player reset */
static void
rd2_12f08(void)
{
	U32 a;
	S16 d7;
	for (a = 0x12e14u; a <= 0x12e2eu; a += 2)
		rd2_ww(a, 0);
	rd2_ww(0x14360, 0);
	rd2_ww(0x1695a, 1);
	rd2_ww(0x1696c, 1);
	rd2_ww(0x1696a, 0);
	rd2_ww(0x1696e, 0);
	rd2_ww(0x16962, 0);
	rd2_ww(0x1697c, 0);
	rd2_ww(0x1697e, 0);
	rd2_ww(0x12ef4, 0);
	rd2_wb(0x12ef2, 0);
	rd2_ww(0x16b12, 0);
	rd2_ww(0x16b5e, 0);
	rd2_ww(0x16902, 0);
	rd2_ww(0x12e30, 0);
	rd2_ww(0x16966, 0x100);
	for (a = 0x169b2u, d7 = 3; ; a += 0x58) {
		rd2_ww(a, 0);
		if (--d7 == -1) break;
	}
}

/* $1300e checkpoint save */
void
rd2_1300e(void)
{
	rd2_ww(0x12e32, rd2_rw(0x1697e));
	rd2_ww(0x12e34, rd2_rw(0x12e18));
	rd2_ww(0x12e36, rd2_rw(0x12e1a));
	rd2_ww(0x12e38, rd2_rw(0x12e14));
	rd2_ww(0x12e3a, rd2_rw(0x1695c));
	rd2_ww(0x12e3c, rd2_rw(0x16960));
	rd2_ww(0x12e3e, rd2_rw(0x16464));
	rd2_ww(0x12e40, rd2_rw(0x16462));
}

/* $14458 submap loader: d0 = submap, d1 = scroll y */
void
rd2_14458(U16 d0, U16 d1)
{
	U32 a0;

	rd2_ww(0x16464, d0);
	a0 = 0x54c00u + (U32)rd2_extw((U16)(d0 << 3));
	rd2_wl(0x1646a, (U32)rd2_rw(a0) + 0x56400u);
	rd2_ww(0x16466, 0);
	rd2_ww(0x16468, (U16)(rd2_rw(a0 + 2) << 3));
	rd2_wl(0x1435c, (U32)rd2_rw(a0 + 4) + 0x54c00u);
	rd2_wl(0x144c4, (U32)rd2_rw(a0 + 6) + 0x54c00u);
	rd2_ww(0x16462, d1);
	rd2_1300e();
	rd2_18516();
}

/* $14300: d0 submap, d1 scroll, d2 x, d3 y, d4 [$1697e], d5 [$12e18], d6 [$12e1a], d7 [$12e14] */
static void
rd2_14300(U16 d0, U16 d1, U16 d2, U16 d3, U16 d4, U16 d5, U16 d6, U16 d7)
{
	rd2_1704c(0x167a2u);
	rd2_12f08();
	rd2_ww(0x1695c, d2);
	rd2_ww(0x16960, d3);
	rd2_ww(0x1697e, d4);
	rd2_ww(0x12e18, d5);
	rd2_ww(0x12e1a, d6);
	rd2_ww(0x12e14, d7);
	rd2_14458(d0, d1);
	rd2_16474();
	rd2_16630();
	rd2_157b4();
	rd2_14542();
	rd2_ww(0x14592, 0xffff);
	rd2_14594();
	rd2_ww(0x14592, 0);
}

/* $142a0 level start */
void
rd2_142a0(void)
{
	U32 a0;

	rd2_ww(0x144c2, 0);
	if (rd2_rw(RD2_DEMO) == 0)
		rd2_1a6aa((U16)(rd2_rw(RD2_MAP_PLAYING) + 3), 0);
	rd2_18186(0);
	a0 = 0x14250u + (U32)rd2_extw((U16)((rd2_rw(RD2_MAP_PLAYING) - 1) * 4));
	a0 = rd2_rl(a0);
	rd2_14300(rd2_rw(a0), rd2_rw(a0 + 2), rd2_rw(a0 + 4), rd2_rw(a0 + 6), rd2_rw(a0 + 8),
	          0, 0, rd2_rw(a0 + 10));
	rd2_14222();
}

/* $142fc respawn: $13060 (ammo/bombs := 6, checkpoint into d0-d7) then $14300 */
void
rd2_142fc(void)
{
	rd2_17760();
	rd2_14300(rd2_rw(0x12e3e), rd2_rw(0x12e40), rd2_rw(0x12e3a), rd2_rw(0x12e3c),
	          rd2_rw(0x12e32), rd2_rw(0x12e34), rd2_rw(0x12e36), rd2_rw(0x12e38));
}

/* $14434: d0 submap, d1 scroll -> slide transition */
static void
rd2_14434(U16 d0, U16 d1)
{
	if (rd2_rw(0x1695c) == 0) {
		rd2_ww(0x1695c, 0xe8);
		rd2_18abe(d0, d1);
	} else {
		rd2_ww(0x1695c, 0);
		rd2_18bb2(d0, d1);
	}
}

/* $14362 exit-trigger scan */
void
rd2_14362(void)
{
	U32 a0;
	U16 d0, d1, d2;
	U8 b0;

	if (rd2_rw(0x14360) == 0)
		return;
	for (a0 = rd2_rl(0x1435c); ; a0 += 4) {
		b0 = rd2_rb(a0);
		if (b0 == 0)
			return;
		if ((U16)(b0 & 3) != rd2_rw(0x14360))
			continue;
		d2 = (U16)((rd2_rw(0x16462) & 0xfff8) + rd2_rw(0x16960) + 0x14);
		d2 = (U16)(d2 >> 3);                                 /* lsr.w */
		if ((U8)d2 == rd2_rb(a0 + 1))
			break;
	}
	rd2_ww(0x144c2, 0);
	if (b0 & 0x40)
		rd2_ww(0x144c2, 1);
	if (rd2_rw(0x17990) != 0) {
		if (b0 & 0x80) {
			rd2_ww(RD2_MAPDONE, 0xffff);
			return;
		}
	} else if ((b0 & 0x90) == 0x90) {
		rd2_ww(RD2_MAPDONE, 0xffff);
		return;
	}
	rd2_ww(0x12e14, 0);
	if (b0 & 0x20)
		rd2_ww(0x12e14, 0xffff);
	d0 = rd2_rb(a0 + 2);
	d1 = (U16)(rd2_rb(a0 + 3) << 3);
	d2 = (U16)((rd2_rw(0x16960) + 0x14) & 0xfff8);
	d1 = (U16)(d1 - d2);
	d2 = (U16)(rd2_rw(0x16462) & 7);
	d1 = (U16)((d1 & 0xfff8) | d2);
	rd2_14434(d0, d1);
}

/* eof */
