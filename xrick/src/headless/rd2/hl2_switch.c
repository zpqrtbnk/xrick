/*
 * xrick/src/headless/rd2/hl2_switch.c
 *
 * xrick2-core only (branch `solver`, never shipped): RD2's switches -- trigger boxes
 * fired by Rick's laser shot, bomb or melee (mask bits 1, 2, 3, algo-actors.md §4) --
 * and how to fire one (PLAN.md T47 phase 6). A switch can make an exit passable that
 * no amount of moving can: map 1 submap 2's exit creature despawns when Rick punches
 * a box at the far end of its corridor (hit class $0c, algo-actors.md §3), submap 5's
 * lift starts when he punches the room's right wall. The exit's distance cannot
 * point at those boxes, so the chain driver tries them when an exit search fails.
 *
 * Firing one: solve to a waypoint (hl2_solve) next to the box, then try short
 * programs -- walk a few frames, face the box, act -- until the box's latch bit
 * (d3 bit 7) is set, or the record's actor changes, with Rick still alive a few
 * frames later.
 *
 * Geometry, world coordinates, Rick standing on feet row f (y = f*8 - 13, the landing
 * snap of algo-player.md §3): melee point (x + $18 facing right, x facing left;
 * y + 8), shot (y + 7, moving along the row), bomb blast centre (bomb x + $c, y + $a).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd2_mem.h"
#include "hl2.h"
#include "hl2_state.h"
#include "hl2_obs.h"
#include "hl2_solve.h"
#include "hl2_switch.h"

/* the current submap's spawn table (header word 3), boxes with mask bits 1-3 */
int
hl2_switches(hl2_switch_t *sw, int max)
{
	U32 a = 0x54c00u + rd2_rw(0x54c06u + 8u * (U32)rd2_rw(HL2_SUBMAP));
	int n = 0, k, nb;

	while (rd2_rb(a) != 0) {
		nb = rd2_rb(a + 3) & 3;
		for (k = 0; k < nb && n < max; k++) {
			U32 d = a + 4 + 4u * (U32)k;
			U8 m = rd2_rb(d + 3);
			if (!(m & 0x0e))
				continue;
			sw[n].rec = a;
			sw[n].box = d;
			sw[n].x = rd2_rb(d);
			sw[n].row = rd2_rb(d + 1);
			sw[n].w = ((rd2_rb(d + 2) & 15) + 1) * 8;
			sw[n].h = ((rd2_rb(d + 2) >> 4) + 1) * 8;
			sw[n].mask = m & 0x7f;
			sw[n].actor = rd2_rb(a + 2) >> 7;
			sw[n].spawned = rd2_rb(a) >> 7;
			n++;
		}
		a += 4 + 4u * (U32)nb;
	}
	/* nearest first (rows count double: climbing costs more than walking), and an
	   actor's boxes only while that actor is out */
	{
		int i, j, rr = hl2_rickRow(), rc = hl2_rickCol();
		for (i = 1; i < n; i++)
			for (j = i; j > 0; j--) {
				hl2_switch_t *p = &sw[j - 1], *q = &sw[j], t;
				int dp = 2 * abs(p->row - rr) + abs(p->x / 8 - rc) + (p->actor && !p->spawned ? 1000 : 0);
				int dq = 2 * abs(q->row - rr) + abs(q->x / 8 - rc) + (q->actor && !q->spawned ? 1000 : 0);
				if (dq >= dp) break;
				t = *p; *p = *q; *q = t;
			}
	}
	return n;
}

int
hl2_switchFired(const hl2_switch_t *s)
{
	return (rd2_rb(s->box + 3) & 0x80) != 0;
}

/* what changes when a box fires: its latch, and the actor spawned from its record
   (its flags swap or it goes, algo-actors.md §3) */
static unsigned long long
sw_sig(const hl2_switch_t *s)
{
	unsigned long long h = rd2_rb(s->box + 3);
	U32 a;
	int i;

	for (i = 0, a = 0x16b6au; i < 6; i++, a += 0x58)
		if (rd2_rl(a + 0x2a) == s->rec)
			h = h * 1000003ULL + (unsigned long long)rd2_rw(a) * 7919ULL + rd2_rb(a + 0x2e) + 1;
	return h;
}

/* play <p> (n frames); 1 if Rick is alive and in the same submap after it */
static int
play(const U8 *p, int n, int sub, int lives)
{
	int i, r;
	for (i = 0; i < n; i++) {
		r = hl2_step(p[i]);
		if (r != HL2_STEP || hl2_rickDead() || rd2_rw(HL2_LIVES) < lives ||
		    rd2_rw(HL2_SUBMAP) != sub)
			return 0;
	}
	return 1;
}

#define PRESS_MAX 96

/* the action program for method <m> facing <dir> (4 left, 8 right) */
static int
press(int m, U8 dir, U8 *p)
{
	int n = 0, i;
	if (m == 0x08) {                         /* melee: face, then fire + direction */
		p[n++] = 0; p[n++] = dir;
		for (i = 0; i < 4; i++) p[n++] = (U8)(0x80 | dir);
		for (i = 0; i < 6; i++) p[n++] = 0;
	} else if (m == 0x02) {                  /* laser: re-arm (fire alone), face, fire + up */
		p[n++] = 0x80; p[n++] = 0x80; p[n++] = 0; p[n++] = dir;
		for (i = 0; i < 3; i++) p[n++] = 0x81;
		for (i = 0; i < 30; i++) p[n++] = 0;
	} else {                                 /* bomb: drop, run the other way, wait */
		p[n++] = 0; p[n++] = 0; p[n++] = 0x82; p[n++] = 0x82;
		for (i = 0; i < 24; i++) p[n++] = (U8)(dir == 4 ? 8 : 4);
		for (i = 0; i < 30; i++) p[n++] = 0;
	}
	return n;
}

/* the x range [*lo, *hi] of Rick for method <m> facing <dir> to put its point in box <s>
   (point_in_box is inclusive, algo-collision.md §7) */
static void
aim_x(const hl2_switch_t *s, int m, U8 dir, int *lo, int *hi)
{
	if (m == 0x08) {                                  /* melee point x + $18 / x */
		*lo = dir == 8 ? s->x - 0x18 : s->x;
	} else if (m == 0x04) {                           /* blast centre bomb x + $c */
		*lo = s->x - 0xc;
	} else {                                          /* laser: from a little way off */
		*lo = dir == 8 ? s->x - 0x30 : s->x + s->w + 0x18;
		*hi = *lo;
		return;
	}
	*hi = *lo + s->w;
}

/* Rick can stand with his anchor at (f, c): body and feet rows free, and the probe's
   feet row below holds a floor in one of its (up to 3) columns */
static int
standable(int f, int c)
{
	int r, k;
	U8 below = 0;
	if (c < 0 || c + 2 > HL2_COLS)
		return 0;
	for (r = f - 2; r <= f; r++)
		for (k = c; k < c + 2; k++)
			if (hl2_attr(r, k) & (HL2_T_SOLID | HL2_T_LETHAL))
				return 0;
	for (k = c; k < c + 3 && k < HL2_COLS; k++)
		below |= hl2_attr(f + 1, k);
	return (below & (HL2_T_FLOOR | HL2_T_SOLID)) != 0;
}

/*
 * fire switch <s>: frames into seq (the waypoint path plus the press), or -1. The
 * state is put back; the caller replays seq.
 */
int
hl2_switchFire(const hl2_solveopt_t *o0, const hl2_switch_t *s, U8 *seq, int max)
{
	static const int methods[3] = { 0x08, 0x02, 0x04 };
	hl2_solveopt_t o = *o0;
	size_t sz = hl2_stateSize();
	U8 *start = malloc(sz), *at = malloc(sz), p[PRESS_MAX + 32];
	int mi, di, f, n, shift, k, np, res = -1, sub = rd2_rw(HL2_SUBMAP), lives = rd2_rw(HL2_LIVES);
	unsigned long long sig0;

	hl2_stateSave(start);
	sig0 = sw_sig(s);
	for (mi = 0; mi < 3 && res < 0; mi++) {
		int m = methods[mi];
		if (!(s->mask & m))
			continue;
		if (m == 0x04 && rd2_rw(HL2_BOMBS) == 0) continue;
		if (m == 0x02 && rd2_rw(HL2_LASER) == 0) continue;
		for (di = 0; di < 2 && res < 0; di++) {
			U8 dir = di ? 4 : 8;
			int xlo, xhi, x, c, lastc;
			aim_x(s, m, dir, &xlo, &xhi);
			if (xlo < 0) xlo = 0;
			if (xhi > 0xe8) xhi = 0xe8;
			/* feet rows that put the point inside the box, bottom first; columns of
			   the x range where Rick can stand (or the range's start if none) */
			for (f = s->row + (s->h + 5) / 8; f >= s->row + 1 && res < 0; f--)
			for (x = xlo, lastc = -1; x <= xhi && res < 0; x += 2) {
				c = (x + 4) >> 3;
				if (c == lastc || (!standable(f, c) && !(x == xlo && !standable(f, (xhi + 4) >> 3))))
					continue;
				lastc = c;
				hl2_stateLoad(start);
				o.wp_row = f;
				o.wp_col = c;
				o.exit = -1;
				o.stuck = NULL;
				n = hl2_solve(&o, seq, max - PRESS_MAX - 32);
				if (n < 0)
					continue;
				if (o.verbose)
					fprintf(stderr, "switch %lu box (%d,%d) %dx%d: waypoint (%d,%d) in %d frames\n",
					        (unsigned long)s->rec, s->x, s->row, s->w, s->h, f, o.wp_col, n);
				hl2_stateLoad(start);
				if (!play(seq, n, sub, lives))
					continue;
				hl2_stateSave(at);
				/* walk 0..12 frames either way (2 px each), then the press */
				for (shift = 0; shift <= 24 && res < 0; shift++) {
					U8 w = shift & 1 ? 4 : 8;
					int nw = (shift + 1) / 2;
					np = 0;
					for (k = 0; k < nw; k++) p[np++] = w;
					np += press(m, dir, p + np);
					hl2_stateLoad(at);
					if (!play(p, np, sub, lives))
						continue;
					if (sw_sig(s) == sig0)
						continue;
					memcpy(seq + n, p, (size_t)np);
					res = n + np;
				}
			}
		}
	}
	hl2_stateLoad(start);
	free(start); free(at);
	return res;
}

/* eof */
