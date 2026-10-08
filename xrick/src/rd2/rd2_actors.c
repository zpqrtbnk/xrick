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
 * Rick Dangerous 2 -- 6-slot actor table, script steppers and chain helpers, transliterated
 * from the disassembly ($14d48-$150a1, $1704c-$17115 minus the draw, $171a4-$17391,
 * $15740-$157aa; read 2026-09-24; kb2/algo-actors.md §1-§3, algo-spawn.md §6-§7).
 */

#include "rd2_mem.h"
#include "rd2_game.h"
#include "rd2_collide.h"

#define W(v)  ((S16)(v))

/* ---- script steppers ---- */

/* $171bc animation step (carry = a jump was taken); preserves d0/d1/a0 */
int
rd2_171bc(U32 a6)
{
	U32 a0;
	int d1;
	S16 d0;

	if (rd2_rl(a6 + 0x1e) == 0)
		return 0;
	if (rd2_rw(a6 + 0x22) != 0) {
		rd2_ww(a6 + 0x22, (U16)(rd2_rw(a6 + 0x22) - 1));
		return 0;
	}
	d1 = 0;
	a0 = rd2_rl(a6 + 0x1e);
	for (;;) {
		d0 = rd2_rws(a0);
		if (d0 >= 0) {                                       /* frame */
			rd2_ww(a6 + 0x0e, (U16)d0);
			a0 += 2;
			rd2_wl(a6 + 0x1e, a0);
			break;
		}
		if (d0 == -1) {                                      /* jump, relative to this record */
			a0 += (U32)rd2_extw(rd2_rw(a0 + 2));
			rd2_wl(a6 + 0x1e, a0);
			d1 = -1;
			continue;
		}
		if (d0 == -3) {                                      /* sound */
			if (rd2_rws(a6 + 0x06) >= 0x23 && rd2_rws(a6 + 0x06) <= 0x148)
				rd2_1a6aa(rd2_rw(a0 + 2), 0);
			a0 += 4;
			continue;
		}
		rd2_ww(a6 + 0x22, (U16)(rd2_rw(a0 + 2) - 1));        /* show frame for n calls */
		rd2_ww(a6 + 0x0e, rd2_rw(a0 + 4));
		a0 += 6;
		rd2_wl(a6 + 0x1e, a0);
		break;
	}
	return d1 != 0;
}

/* $172fa movement step: dx, dy (sign-extended), carry = a jump was taken (dx = dy = 0) */
int
rd2_172fa(U32 a6, S16 *dx, S16 *dy)
{
	U32 a0;

	*dx = 0;
	*dy = 0;
	if (rd2_rl(a6 + 0x16) == 0)
		return 0;
	a0 = rd2_rl(a6 + 0x16);
	if (rd2_rw(a6 + 0x1a) == 0) {
		for (;;) {
			if (rd2_rw(a0) == 0) {                           /* jump, relative to this record */
				a0 += (U32)rd2_extw(rd2_rw(a0 + 2));
				rd2_wl(a6 + 0x16, a0);
				return 1;
			}
			if (rd2_rw(a0) != 0xffff)
				break;
			if (rd2_rws(a6 + 0x06) >= 0x23 && rd2_rws(a6 + 0x06) <= 0x148)
				rd2_1a6aa(rd2_rw(a0 + 2), 0);
			a0 += 4;
		}
		rd2_ww(a6 + 0x1a, rd2_rw(a0));
	}
	*dx = rd2_extb(rd2_rb(a0 + 2));
	*dy = rd2_extb(rd2_rb(a0 + 3));
	rd2_ww(a6 + 0x1a, (U16)(rd2_rw(a6 + 0x1a) - 1));
	if (rd2_rw(a6 + 0x1a) == 0)
		a0 += 4;
	rd2_wl(a6 + 0x16, a0);
	return 0;
}

/* $1726e group/scene movement step: jump taken eagerly with this step's dx/dy */
int
rd2_1726e(U32 a6, S16 *dx, S16 *dy)
{
	U32 a0;

	*dx = 0;
	*dy = 0;
	if (rd2_rl(a6 + 0x16) == 0)
		return 0;
	a0 = rd2_rl(a6 + 0x16);
	for (;;) {
		if (rd2_rw(a6 + 0x1a) != 0)
			break;
		if (rd2_rws(a0) >= 0) {
			rd2_ww(a6 + 0x1a, rd2_rw(a0));
			break;
		}
		if (rd2_rws(a6 + 0x06) >= 0x23 && rd2_rws(a6 + 0x06) <= 0x148)   /* any negative word = sound */
			rd2_1a6aa(rd2_rw(a0 + 2), 0);
		a0 += 4;
	}
	*dx = rd2_extb(rd2_rb(a0 + 2));
	*dy = rd2_extb(rd2_rb(a0 + 3));
	rd2_ww(a6 + 0x1a, (U16)(rd2_rw(a6 + 0x1a) - 1));
	if (rd2_rw(a6 + 0x1a) != 0) {
		rd2_wl(a6 + 0x16, a0);
		return 0;
	}
	a0 += 4;
	if (rd2_rw(a0) != 0) {
		rd2_wl(a6 + 0x16, a0);
		return 0;
	}
	a0 += (U32)rd2_extw(rd2_rw(a0 + 2));
	rd2_wl(a6 + 0x16, a0);
	return 1;
}

/* ---- chain helpers ---- */

/* $1704c: clear every record from a6 until a negative +0 word */
void
rd2_1704c(U32 a6)
{
	for (; rd2_rws(a6) >= 0; a6 += 0x58) {
		rd2_ww(a6 + 0x00, 0);
		rd2_wl(a6 + 0x16, 0);
		rd2_ww(a6 + 0x1a, 0);
		rd2_ww(a6 + 0x1c, 0);
		rd2_wl(a6 + 0x1e, 0);
		rd2_ww(a6 + 0x22, 0);
		rd2_ww(a6 + 0x10, 0);
	}
}

/* $171a4: y += d0 for every live record of the chain */
void
rd2_171a4(S16 d0)
{
	U32 a6;
	for (a6 = 0x167a2u; ; a6 += 0x58) {
		S16 st = rd2_rws(a6);
		if (st < 0) return;
		if (st != 0)
			rd2_ww(a6 + 0x06, (U16)(rd2_rw(a6 + 0x06) + (U16)d0));
	}
}

/* $15740: objects inside the box are marked hit */
static void
rd2_15740(S16 d0, S16 d1, S16 d2, S16 d3)
{
	U32 a2;
	S16 d7;
	for (a2 = 0x167a2u, d7 = 3; ; a2 += 0x58) {
		if (rd2_rw(a2) != 0 && rd2_rw(a2 + 0x4e) == 0 && rd2_rw(a2 + 0x4c) == 0 &&
		    rd2_14cb4(d0, d1, d2, d3, a2))
			rd2_ww(a2 + 0x4c, 0xffff);
		if (--d7 == -1) break;
	}
}

/* $15776: a falling bomb inside the box explodes */
static void
rd2_15776(S16 d0, S16 d1, S16 d2, S16 d3)
{
	if (rd2_rw(0x16b12) != 1)
		return;
	if (rd2_161ce(d0, d1, d2, d3, W(rd2_rws(0x16b14) + 8), rd2_rws(0x16b18), 8, 0x10))
		rd2_ww(0x16b5e, 0xffff);
}

/* ---- actor update ---- */

/* $14ed4: swap in the alternate profile */
static void
swap_profile(U32 a6)
{
	U8 f = rd2_rb(a6 + 0x01);
	U32 t;
	rd2_wb(a6 + 0x01, rd2_rb(a6 + 0x30));
	rd2_wb(a6 + 0x30, f);
	t = rd2_rl(a6 + 0x16);
	rd2_wl(a6 + 0x16, rd2_rl(a6 + 0x32));
	rd2_wl(a6 + 0x32, t);
	t = rd2_rl(a6 + 0x1e);
	rd2_wl(a6 + 0x1e, rd2_rl(a6 + 0x36));
	rd2_wl(a6 + 0x36, t);
	rd2_ww(a6 + 0x1a, 0);
	rd2_ww(a6 + 0x22, 0);
}

#define S_SET(a6, b)  rd2_wb((a6) + 0x2e, rd2_rb((a6) + 0x2e) | (1 << (b)))
#define S_CLR(a6, b)  rd2_wb((a6) + 0x2e, rd2_rb((a6) + 0x2e) & ~(1 << (b)))
#define S_TST(a6, b)  (rd2_rb((a6) + 0x2e) & (1 << (b)))
#define F_TST(a6, b)  (rd2_rb((a6) + 0x01) & (1 << (b)))

/* $14f24: static actors (modes $20/$28/$2c) */
static void
update_static(U32 a6)
{
	S16 d0, d1, d2, d3;
	U8 mode;

	if (rd2_rw(a6 + 0x4e) != 0)
		goto DYING;
	if (rd2_14998(a6)) {
		rd2_149f0(a6);
		return;
	}
	rd2_171bc(a6);
	d0 = rd2_rws(a6 + 0x02);
	d1 = rd2_rws(a6 + 0x06);
	d2 = 0x18;
	d3 = 0x15;
	if (rd2_14b7a(d0, d1, d2, d3)) {                         /* $14f90: touched by Rick */
		mode = rd2_rb(a6 + 0x01) & 0x3c;
		if (mode == 0x20) {
			rd2_17810(0x500);
			rd2_1a6aa(0x15, 0);
			rd2_ww(a6 + 0x4e, 0xffff);
			rd2_ww(a6 + 0x12, 0);
			rd2_wl(a6 + 0x1e, 0x14696u);
			rd2_ww(a6 + 0x22, 0);
			goto DYING;
		}
		if (mode == 0x28) {
			rd2_ww(0x176f4, 6);                              /* laser ammo */
			rd2_ww(0x176f2, 0xffff);
		} else {
			rd2_ww(0x17702, 6);                              /* bombs */
			rd2_ww(0x17700, 0xffff);
		}
		rd2_1a6aa(0x14, 0);
		rd2_14a12(a6);
		return;
	}
	if ((rd2_rb(a6 + 0x01) & 0x3c) == 0x20)
		return;
	if (rd2_14bee(d0, d1, d2, d3)) {
		rd2_ww(RD2_SHOT_HIT, 0xffff);
		goto DESTROYED;
	}
	if (rd2_14c5a(d0, d1, d2, d3))
		goto DESTROYED;
	d0 = W(d0 - 0x10);
	d1 = W(d1 - 0x0e);
	d2 = W(d2 + 0x20);
	d3 = W(d3 + 0x1d);
	if (rd2_14c20(d0, d1, d2, d3))
		goto DESTROYED;
	return;

DESTROYED:                                                   /* $15028 */
	rd2_1a6aa(0x13, 0);
	rd2_ww(a6 + 0x4e, 0xffff);
	rd2_wb(a6 + 0x01, rd2_rb(a6 + 0x01) | 0x40);
	rd2_wl(a6 + 0x1e, 0x12ed6u);
	rd2_ww(a6 + 0x22, 0);

DYING:                                                       /* $15054 */
	if (rd2_14998(a6) || rd2_171bc(a6)) {
		rd2_14a12(a6);
		return;
	}
	if ((rd2_rb(a6 + 0x01) & 0x3c) == 0x20) {
		rd2_ww(a6 + 0x06, (U16)(rd2_rw(a6 + 0x06) - 2));
		return;
	}
	if (rd2_14b7a(rd2_rws(a6 + 0x02), rd2_rws(a6 + 0x06), 0x18, 0x15))
		rd2_ww(0x12e2c, 0xffff);
}

/* $14d70 update_actor_ai */
static void
rd2_14d70(U32 a6)
{
	S16 d0, d1;
	U8 m;

	m = rd2_rb(a6 + 0x01) & 0x3c;
	if (m == 0x20 || m == 0x28 || m == 0x2c) {
		update_static(a6);
		return;
	}
	if (F_TST(a6, 1)) {                                      /* frozen */
		d0 = rd2_rws(a6 + 0x02);
		d1 = rd2_rws(a6 + 0x06);
	} else {
		while (rd2_172fa(a6, &d0, &d1)) {                    /* $14da4: a jump was taken */
			if (S_TST(a6, 5)) {
				rd2_14a12(a6);
				return;
			}
			if (!S_TST(a6, 4))
				continue;
			if (S_TST(a6, 3))
				S_CLR(a6, 3);
			else
				S_CLR(a6, 4);
			if ((rd2_rb(a6 + 0x01) & 0x1c) == 0x14)
				S_SET(a6, 5);
			swap_profile(a6);
			return;
		}
		rd2_ww(a6 + 0x3a, (U16)d0);
		rd2_ww(a6 + 0x3c, (U16)d1);
		d0 = W(d0 + rd2_rws(a6 + 0x02));
		d1 = W(d1 + rd2_rws(a6 + 0x06));
		rd2_ww(a6 + 0x02, (U16)d0);
		rd2_ww(a6 + 0x06, (U16)d1);
		if (rd2_14998(a6)) {
			rd2_149f0(a6);
			return;
		}
		rd2_171bc(a6);
	}

	/* $14e10: contact with Rick */
	if (rd2_rw(0x12e2a) == 0 && F_TST(a6, 6) && rd2_rw(a6 + 0x0e) != 0xfe) {
		S16 d2 = rd2_rws(a6 + 0x26), d3 = rd2_rws(a6 + 0x28);
		if (F_TST(a6, 7))
			rd2_15776(d0, d1, d2, d3);
		rd2_15740(d0, d1, d2, d3);
		if (rd2_14b7a(d0, d1, d2, d3))
			rd2_ww(0x12e2c, 0xffff);
	}

	/* $14e56: own trigger boxes */
	if (S_TST(a6, 6))
		return;
	if (S_TST(a6, 4) && F_TST(a6, 5))
		return;
	if (!rd2_14a3c(rd2_rl(a6 + 0x2a), a6)) {
		if ((rd2_rb(a6 + 0x01) & 0x1c) == 0x1c)
			rd2_14a12(a6);
		return;
	}
	m = rd2_rb(a6 + 0x01) & 0x1c;
	if (m == 0) {
		rd2_wb(a6 + 0x01, rd2_rb(a6 + 0x01) ^ 0x02);
		return;
	}
	if (m == 0x1c)
		return;
	if (m == 0x0c) {
		rd2_14a12(a6);
		return;
	}
	if (m & 0x08) {
		S_SET(a6, 6);
		if (m & 0x10)
			S_SET(a6, 5);
		swap_profile(a6);
		return;
	}
	S_SET(a6, 4);
	if (F_TST(a6, 5))
		S_SET(a6, 3);
}

/* $14d48 update actors */
void
rd2_14d48(void)
{
	U32 a6;
	S16 d7;

	if (rd2_rw(0x144c2) != 0)
		return;
	for (a6 = 0x16b6au, d7 = 5; ; a6 += 0x58) {
		if (rd2_rw(a6) != 0)
			rd2_14d70(a6);
		if (--d7 == -1) break;
	}
}

/* eof */
