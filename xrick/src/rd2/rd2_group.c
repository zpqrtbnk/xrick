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
 * Rick Dangerous 2 -- the 5-actor group (mini-boss) and its shot, transliterated from the
 * disassembly ($15b3c-$15f0c, read 2026-09-24; kb2/algo-flow.md §10). The group uses actor
 * slots 0-4 ($16b6a..); slot 5 ($16d22) is its shot. The hit tests swap two boxes with
 * exg d0-d3/d4-d7; r[] = D0-D3 and s[] = D4-D7 keep that exact order.
 */

#include "rd2_mem.h"
#include "rd2_game.h"
#include "rd2_collide.h"

#define W(v)  ((S16)(v))

static void
exg(S16 *r, S16 *s)
{
	int i;
	for (i = 0; i < 4; i++) {
		S16 t = r[i];
		r[i] = s[i];
		s[i] = t;
	}
}

/* $15b3c init_actor_group */
void
rd2_15b3c(void)
{
	U32 a6, a0;
	S16 d7;

	rd2_ww(0x15b38, 0x14);
	a0 = 0x1586eu;
	for (a6 = 0x16b6au, d7 = 4; ; a6 += 0x58) {
		rd2_ww(a6 + 0x00, 1);
		rd2_ww(a6 + 0x02, rd2_rw(a0)); a0 += 2;
		rd2_ww(a6 + 0x06, rd2_rw(a0)); a0 += 2;
		rd2_ww(a6 + 0x12, 1);
		rd2_ww(a6 + 0x10, 0);
		rd2_wl(a6 + 0x1e, rd2_rl(a0)); a0 += 4;
		rd2_ww(a6 + 0x22, 0);
		rd2_171bc(a6);
		rd2_ww(a6 + 0x22, 0);
		rd2_wl(a6 + 0x16, rd2_rl(a0)); a0 += 4;
		rd2_ww(a6 + 0x1a, 0);
		rd2_ww(a6 + 0x1c, 0);
		if (--d7 == -1) break;
	}
	rd2_ww(0x16d22, 0);
	rd2_ww(0x16d32, 0);
	rd2_ww(0x16d34, 1);
	rd2_ww(0x15b3a, 0);
}

/* $15eca group hit: flash, one hit point less (carry = defeated) */
static int
rd2_15eca(void)
{
	U32 a6;
	S16 d7;
	rd2_1a6aa(0x18, 0);
	for (a6 = 0x16b6au, d7 = 4; ; a6 += 0x58) {
		rd2_ww(a6 + 0x10, 1);
		if (--d7 == -1) break;
	}
	rd2_ww(0x15b38, (U16)(rd2_rw(0x15b38) - 1));
	return rd2_rw(0x15b38) == 0;
}

/* $15d84 shot timer: fire at Rick */
static void
rd2_15d84(void)
{
	S16 d0, d1, d2, d3;

	if (rd2_rw(0x15b3a) != 0) {
		rd2_ww(0x15b3a, (U16)(rd2_rw(0x15b3a) - 1));
		return;
	}
	rd2_1a6aa(0x22, 0);
	rd2_ww(0x15b3a, 0x32);
	rd2_ww(0x16d22, 1);
	rd2_ww(0x16d30, 0x6c);
	d0 = W(rd2_rws(0x1695c) + 0x0c);
	d1 = W(rd2_rws(0x16960) + 0x0a);
	d2 = W(rd2_rws(0x16b6c) - 0x1c);
	rd2_ww(0x16d24, (U16)d2);
	d3 = W(rd2_rws(0x16b70) + 0x2c);
	rd2_ww(0x16d28, (U16)d3);
	d0 = W(d0 - d2);
	d2 = d0;
	d1 = W(d1 - d3);
	d3 = d1;
	for (;;) {
		if (!(d0 > 4 || d0 < -4 || d1 > 4 || d1 < -4))
			break;
		d0 = (S16)(d0 >> 1);                                 /* asr.w */
		d1 = (S16)(d1 >> 1);
		if (d0 != 0 || d1 != 0)
			continue;
		d0 = 4;
		d1 = 4;
		if (d2 < 0) d0 = W(-d0);
		if (d3 < 0) d1 = W(-d1);
		break;
	}
	rd2_ww(0x16d2c, (U16)d0);
	rd2_ww(0x16d2e, (U16)d1);
}

/* $15e48 move the group's shot */
static void
rd2_15e48(void)
{
	S16 d0, d1;

	rd2_ww(0x15b3a, (U16)(rd2_rw(0x15b3a) - 1));
	if (rd2_rws(0x15b3a) < 0)
		rd2_ww(0x15b3a, 0);
	d0 = W(rd2_rws(0x16d24) + rd2_rws(0x16d2c));
	rd2_ww(0x16d24, (U16)d0);
	d1 = W(rd2_rws(0x16d28) + rd2_rws(0x16d2e));
	rd2_ww(0x16d28, (U16)d1);
	d0 = W(d0 + 4);
	if (d0 < 0 || d0 >= 0x104)
		goto OFF;
	d1 = W(d1 + 4);
	rd2_ww(RD2_PX, (U16)d0);
	rd2_ww(RD2_PY, (U16)d1);
	rd2_161fe();
	if (rd2_rb(RD2_PRES) & 0x02)
		goto OFF;
	if (!rd2_14b7a(W(d0 - 2), W(d1 - 2), 4, 4))
		return;
	rd2_ww(0x12e2c, 0xffff);
OFF:
	rd2_ww(0x16d22, 0);
}

/* $15bc0 update_actor_group */
void
rd2_15bc0(void)
{
	U32 a6, a0;
	S16 d7, dx, dy;
	S16 r[4], s[4];

	if (rd2_rw(0x144c2) == 0 || rd2_rw(0x16b6a) == 0)
		return;
	rd2_1726e(0x16b6au, &dx, &dy);
	for (a6 = 0x16b6au, d7 = 4; ; a6 += 0x58) {
		rd2_ww(a6 + 0x02, (U16)(rd2_rw(a6 + 0x02) + (U16)dx));
		rd2_ww(a6 + 0x06, (U16)(rd2_rw(a6 + 0x06) + (U16)dy));
		rd2_171bc(a6);
		if (--d7 == -1) break;
	}
	r[0] = rd2_rws(0x16b6c);
	r[1] = rd2_rws(0x16b70);
	r[2] = 0x1c;
	r[3] = 0x3e;
	s[0] = W(r[0] - 0x14);
	s[1] = W(r[1] + 0x18);
	s[2] = 0x14;
	s[3] = 0x20;
	if (rd2_14b7a(r[0], r[1], r[2], r[3]))
		rd2_ww(0x12e2c, 0xffff);
	else {
		exg(r, s);
		if (rd2_14b7a(r[0], r[1], r[2], r[3]))
			rd2_ww(0x12e2c, 0xffff);
	}
	/* $15c38 */
	if (rd2_14bee(r[0], r[1], r[2], r[3])) {
		rd2_ww(0x16902, 0);
		if (rd2_15eca()) goto DEFEATED;
	} else {
		exg(r, s);
		if (rd2_14bee(r[0], r[1], r[2], r[3])) {
			rd2_ww(0x16902, 0);
			if (rd2_15eca()) goto DEFEATED;
		}
	}
	/* $15c6e */
	r[0] = W(r[0] - 0x10); r[1] = W(r[1] - 0x0e); r[2] = W(r[2] + 0x20); r[3] = W(r[3] + 0x1d);
	if (rd2_14c20(r[0], r[1], r[2], r[3])) {
		if (rd2_15eca()) goto DEFEATED;
	} else {
		exg(r, s);
		r[0] = W(r[0] - 0x10); r[1] = W(r[1] - 0x0e); r[2] = W(r[2] + 0x20); r[3] = W(r[3] + 0x1d);
		if (rd2_14c20(r[0], r[1], r[2], r[3]))
			if (rd2_15eca()) goto DEFEATED;
	}
	/* $15cb4 */
	if (rd2_rw(0x16d22) == 0)
		rd2_15d84();
	else
		rd2_15e48();
	return;

DEFEATED:                                                    /* $15cc8 */
	rd2_17810(0x5000);
	rd2_ww(0x144c2, 0);
	a0 = 0x15a2cu;
	for (a6 = 0x16b6au, d7 = 4; ; a6 += 0x58) {
		rd2_ww(a6 + 0x00, 0x11);
		rd2_wb(a6 + 0x2e, 0x60);
		rd2_wl(a6 + 0x2a, 0);
		rd2_ww(a6 + 0x22, 0);
		rd2_wl(a6 + 0x1e, rd2_rl(a0)); a0 += 4;
		rd2_ww(a6 + 0x1a, 0);
		rd2_ww(a6 + 0x1c, 0);
		rd2_wl(a6 + 0x16, rd2_rl(a0)); a0 += 4;
		if (--d7 == -1) break;
	}
	a6 = 0x167a2u;                                           /* object slot 0 flies off */
	rd2_ww(a6 + 0x00, 1);
	rd2_ww(a6 + 0x02, rd2_rw(0x16b6c));
	rd2_ww(a6 + 0x06, rd2_rw(0x16b70));
	rd2_ww(a6 + 0x3e, 0x4b);
	rd2_ww(a6 + 0x4e, 0xffff);
	rd2_ww(a6 + 0x12, 0);
	rd2_ww(a6 + 0x22, 0);
	rd2_ww(a6 + 0x08, 0);
	rd2_ww(a6 + 0x0c, 0xfb00);
	rd2_ww(a6 + 0x0a, 0xfffe);
	rd2_1a6aa(0x0b, 0);
	rd2_wl(a6 + 0x2a, 0);
	rd2_ww(0x16d22, 0);
}

/* eof */
