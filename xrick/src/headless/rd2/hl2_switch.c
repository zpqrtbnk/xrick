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
	hl2_switchesSort(sw, n, hl2_rickRow(), hl2_rickCol());
	return n;
}

/* nearest to (row, col) first (rows count double: climbing costs more than walking),
   and an actor's boxes only while that actor is out */
void
hl2_switchesSort(hl2_switch_t *sw, int n, int rr, int rc)
{
	int i, j;
	for (i = 1; i < n; i++)
		for (j = i; j > 0; j--) {
			hl2_switch_t *p = &sw[j - 1], *q = &sw[j], t;
			int dp = 2 * abs(p->row - rr) + abs(p->x / 8 - rc) + (p->actor && !p->spawned ? 1000 : 0);
			int dq = 2 * abs(q->row - rr) + abs(q->x / 8 - rc) + (q->actor && !q->spawned ? 1000 : 0);
			if (dq >= dp) break;
			t = *p; *p = *q; *q = t;
		}
}

/* the live actors spawned from switch <s>'s record (their +$2a, algo-actors.md §1)
   that a hit still changes: S (+$2e) bit 6 (profile swapped) or bit 4 (swap pending)
   set means its box has fired already (update_actor_ai, algo-actors.md §3) */
static int
sw_actors(const hl2_switch_t *s)
{
	U32 a;
	int i, n = 0;
	for (i = 0, a = 0x16b6au; i < 6; i++, a += 0x58)
		if (rd2_rws(a) > 0 && rd2_rl(a + 0x2a) == s->rec && !(rd2_rb(a + 0x2e) & 0x50))
			n++;
	return n;
}

/*
 * nothing left to do with this switch: an actor record whose live actors have all
 * reacted already, or whose actor was spawned and is gone (killed), or a trigger
 * record already spawned. An actor record not spawned yet is not: getting there
 * spawns it (map 1 submap 4's lift switch). The box's
 * latch bit is no use: a latched box is cleared again as soon as nothing hits it
 * (algo-actors.md §4), so it reads 0 a frame after firing.
 */
int
hl2_switchFired(const hl2_switch_t *s)
{
	U32 a;
	int i, live = 0;
	if (!s->actor)
		return (rd2_rb(s->rec) & 0x80) != 0;
	for (i = 0, a = 0x16b6au; i < 6; i++, a += 0x58)
		if (rd2_rws(a) > 0 && rd2_rl(a + 0x2a) == s->rec)
			live++;
	if (live == 0)                              /* spawned and gone: killed (a one-shot record */
		return (rd2_rb(s->rec) & 0x80) != 0;    /* keeps its bit, algo-actors.md §3 despawn) */
	return sw_actors(s) == 0;
}

/* live actors of switch <s>'s record that have reacted (S bit 6 or 4) */
static int
sw_reacted(const hl2_switch_t *s)
{
	U32 a;
	int i, n = 0;
	for (i = 0, a = 0x16b6au; i < 6; i++, a += 0x58)
		if (rd2_rws(a) > 0 && rd2_rl(a + 0x2a) == s->rec && (rd2_rb(a + 0x2e) & 0x50))
			n++;
	return n;
}

/*
 * play press <p> and tell whether box <s> fired: its latch bit (d3 bit 7) is set on
 * the frame a test hits it (algo-actors.md §4) -- watched every frame, since it
 * clears again once nothing hits -- or one more of its actors has reacted, or its
 * trigger record got spawned. Not "anything about the record changed": the record's
 * actor spawning as Rick walks up (map 1 submap 4) is not a fire.
 */
static int
play_fire(const U8 *p, int n, int sub, int lives, const hl2_switch_t *s)
{
	int i, r, hit = 0, re0 = sw_reacted(s), sp0 = rd2_rb(s->rec) >> 7;
	for (i = 0; i < n; i++) {
		r = hl2_step(p[i]);
		if (r != HL2_STEP || hl2_rickDead() || rd2_rw(HL2_LIVES) < lives ||
		    rd2_rw(HL2_SUBMAP) != sub)
			return 0;
		if (rd2_rb(s->box + 3) & 0x80)
			hit = 1;
	}
	if (sw_reacted(s) > re0 || (!s->actor && (rd2_rb(s->rec) >> 7) > sp0))
		hit = 1;
	return hit;
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

static int sw_tunnel;    /* Rick flies (tunnel mode, [$12e14]): any free spot will do */

#define PRESS_MAX 96
#define WP_TRIES 8
#define WP_STALL 200   /* frames with no new closest distance, in a waypoint search */
#define BOMB_ABOVE 6   /* rows above a bomb box to drop from */

/* a pseudo method: a bomb thrown (fire + down + left/right). On a plain floor it
   slides THROW_PX and blows there (measured 2026-10-02, maps 1-4); other floors
   change that, which the press loop's walking shifts and its fire check absorb */
#define THROW 0x100
#define THROW_PX 54

/* where to go after dropping a bomb: away along the floor, either way, or off it by
   a ladder (map 1 submap 4: the blocks at row 70 can only be bombed from where the
   way out is the ladder down) */
static const U8 escapes[] = { 0x04, 0x08, 0x02, 0x01, 0x06, 0x0a, 0x05, 0x09 };
#define N_ESC ((int)sizeof(escapes))
/* and for how long, then stand until 64 frames after the drop: 30 walks out of a
   room whose exit is 60 px away (map 3 submap 2: the bomb box by the left exit fires
   with a drop and 12-20 frames left, 2026-10-03) */
static const U8 esc_len[] = { 30, 20, 12 };
#define N_ELEN ((int)sizeof(esc_len))

/* the action program for method <m> facing <dir> (4 left, 8 right); <esc>: the
   bomb's escape, escapes[esc % N_ESC] for esc_len[esc / N_ESC] frames */
static int
press(int m, U8 dir, int esc, U8 *p)
{
	int n = 0, i;
	if (m == 0x08) {                         /* melee: face, then fire + direction */
		p[n++] = 0; p[n++] = dir;
		for (i = 0; i < 4; i++) p[n++] = (U8)(0x80 | dir);
		for (i = 0; i < 6; i++) p[n++] = 0;
	} else if (m == 0x02 && sw_tunnel) {     /* in the tunnel fire alone shoots (rd2_13d0a) */
		p[n++] = 0; p[n++] = dir;
		for (i = 0; i < 3; i++) p[n++] = 0x80;
		for (i = 0; i < 30; i++) p[n++] = 0;
	} else if (m == 0x02) {                  /* laser: re-arm (fire alone), face, fire + up */
		p[n++] = 0x80; p[n++] = 0x80; p[n++] = 0; p[n++] = dir;
		for (i = 0; i < 3; i++) p[n++] = 0x81;
		for (i = 0; i < 30; i++) p[n++] = 0;
	} else if (m == 0x04) {                  /* bomb: drop, get away, wait for the blast */
		p[n++] = 0; p[n++] = 0; p[n++] = 0x82; p[n++] = 0x82;
		for (i = 0; i < esc_len[esc / N_ESC]; i++) p[n++] = escapes[esc % N_ESC];
		for (i = esc_len[esc / N_ESC]; i < 60; i++) p[n++] = 0;
	} else {                                 /* THROW: fire + down + dir, the bomb slides */
		p[n++] = 0; p[n++] = 0; p[n++] = (U8)(0x82 | dir); p[n++] = (U8)(0x82 | dir);
		for (i = 0; i < esc_len[esc / N_ESC]; i++) p[n++] = esc % N_ESC ? escapes[esc % N_ESC] : 0;
		for (i = esc_len[esc / N_ESC]; i < 60; i++) p[n++] = 0;
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
	} else if (m == THROW) {                          /* the bomb stops 54 px off (measured, */
		*lo = s->x - 0xc + (dir == 8 ? -THROW_PX : THROW_PX);   /* plain floor) */
	} else {                                          /* laser: from a little way off */
		*lo = dir == 8 ? s->x - 0x30 : s->x + s->w + 0x18;
		*hi = *lo;
		return;
	}
	*hi = *lo + s->w;
}

/* Rick can stand with his anchor at (f, c): body and feet rows free, and the probe's
   feet row below holds a floor in one of its (up to 3) columns -- or, flying, just free */
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
	if (sw_tunnel)
		return 1;
	for (k = c; k < c + 3 && k < HL2_COLS; k++)
		below |= hl2_attr(f + 1, k);
	return (below & (HL2_T_FLOOR | HL2_T_SOLID)) != 0;
}

/*
 * from snapshot <at>: walk 0..12 frames either way (2 px each), then the press of
 * method <m> facing <dir> (for a bomb, with each escape) until box <s> fires; the
 * frames into p, or -1
 */
/*
 * a bomb dropped, then a search for any way to live through it (map 4 submap 0: the
 * bush on a ledge, a creature below its left end: crawl left, jump over the creature,
 * run -- three moves no fixed escape has); then the press is judged as any other
 */
static int
bomb_search(const hl2_solveopt_t *o0, const hl2_switch_t *s, const U8 *at, int sub, int lives, U8 *p)
{
	static U8 esc[PRESS_MAX * 4];
	hl2_solveopt_t o = *o0;
	int n;
	U8 drop[4] = { 0, 0, 0x82, 0x82 };

	hl2_stateLoad(at);
	if (!play(drop, 4, sub, lives))
		return -1;
	o.survive = 70;          /* fuse 40 + blast 7, and a margin */
	o.exit = -1; o.wp_row = -1; o.wp_col = -1;
	o.maxsteps = 160; o.stall = 160;
	o.stuck = NULL; o.stage = NULL; o.stage_n = NULL; o.res = NULL;
	n = hl2_solve(&o, esc, (int)sizeof esc - 8);
	if (n < 0 || n + 4 > PRESS_MAX + 24)
		return -1;
	memcpy(p, drop, 4);
	memcpy(p + 4, esc, (size_t)n);
	hl2_stateLoad(at);
	return play_fire(p, n + 4, sub, lives, s) ? n + 4 : -1;
}

static int
presses(int m, U8 dir, const hl2_switch_t *s, const U8 *at, int sub, int lives, U8 *p)
{
	int shift, k, np;
	for (shift = 0; shift < 25 * (m == 0x04 || m == THROW ? N_ESC * N_ELEN : 1); shift++) {
		int bomb = m == 0x04 || m == THROW;
		int sh = bomb ? shift % 25 : shift, esc = bomb ? shift / 25 : 0;
		U8 w = sh & 1 ? 4 : 8;
		int nw = (sh + 1) / 2;
		np = 0;
		for (k = 0; k < nw; k++) p[np++] = w;
		np += press(m, dir, esc, p + np);
		hl2_stateLoad(at);
		if (play_fire(p, np, sub, lives, s))
			return np;
	}
	return -1;
}

/*
 * fire switch <s>: frames into seq (the waypoint path plus the press), or -1. The
 * state is put back; the caller replays seq.
 */
int
hl2_switchFire(const hl2_solveopt_t *o0, const hl2_switch_t *s, U8 *seq, int max)
{
	static const int methods[4] = { 0x08, 0x02, 0x04, THROW };
	hl2_solveopt_t o = *o0;
	size_t sz = hl2_stateSize();
	U8 *start = malloc(sz), *at = malloc(sz), p[PRESS_MAX + 32];
	int mi, di, f, n, np, res = -1, sub = rd2_rw(HL2_SUBMAP), lives = rd2_rw(HL2_LIVES);
	int tries = 0, d;

	hl2_stateSave(start);
	sw_tunnel = rd2_rw(0x12e14u) != 0;
	for (mi = 0; mi < 4 && res < 0; mi++) {
		int m = methods[mi];
		if (!(s->mask & (m == THROW ? 0x04 : m)))
			continue;
		if ((m == 0x04 || m == THROW) && rd2_rw(HL2_BOMBS) == 0) continue;
		if (m == 0x02 && rd2_rw(HL2_LASER) == 0) continue;
		for (di = 0; di < 2 && res < 0; di++) {
			U8 dir = di ? 4 : 8;
			int xlo, xhi, x, c, lastc;
			aim_x(s, m, dir, &xlo, &xhi);
			if (xlo < 0) xlo = 0;
			if (xhi > 0xe8) xhi = 0xe8;
			/* first from where Rick stands, if that is within a walk of the spots: it
			   may be a lift or a platform, which the tile spots below do not know (map
			   2 submap 1: the lift's edge, over the pit) */
			{
				int rx = rd2_rws(HL2_RICK_X), rr = hl2_rickRow();
				hl2_stateLoad(start);
				if ((rd2_rw(0x12e1cu) != 0 || sw_tunnel) && rx >= xlo - 24 && rx <= xhi + 24 &&
				    rr <= s->row + (s->h + 5) / 8 &&
				    rr >= s->row + 1 - ((m == 0x04 || m == THROW) ? BOMB_ABOVE : 0)) {
					hl2_stateSave(at);
					np = presses(m, dir, s, at, sub, lives, p);
					if (np < 0 && m == 0x04)
						np = bomb_search(&o, s, at, sub, lives, p);
					if (np > 0) {
						memcpy(seq, p, (size_t)np);
						res = np;
						if (o.verbose)
							fprintf(stderr, "switch %lu: fired from where Rick stands\n", (unsigned long)s->rec);
						break;
					}
				}
			}
			/* feet rows that put the point inside the box, bottom first -- for a bomb
			   then up to BOMB_ABOVE rows above it, the bomb falls in (map 2 submap 1:
			   the hazard in the pit below the lift dies of a bomb dropped from the
			   lift's edge, 2026-10-03); columns of the x range where Rick can stand
			   (or the range's start if none) */
			for (f = s->row + (s->h + 5) / 8 + (sw_tunnel ? 2 : 0);
			     f >= s->row + 1 - ((m == 0x04 || m == THROW) ? BOMB_ABOVE : sw_tunnel ? 2 : 0) && res < 0; f--)
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
				o.stage = NULL;
				o.stage_n = NULL;
				o.res = NULL;
				/* a spot the field cannot reach costs nothing; the others a search capped by
				   their distance (map 2 submap 1: six ~340 000-node searches for one box) */
				d = hl2_solveDistance(&o);
				if (d < 0 || d >= 1000)
					continue;
				/* at most WP_TRIES waypoint searches per switch: map 2 submap 1 spent hours on one
				   far bomb box, every row x column x method x side */
				if (++tries > WP_TRIES)
					goto out;
				o.maxsteps = 300 + 16 * d < o0->maxsteps ? 300 + 16 * d : o0->maxsteps;
				o.stall = o0->stall < WP_STALL ? o0->stall : WP_STALL;
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
				np = presses(m, dir, s, at, sub, lives, p);
				if (np < 0 && m == 0x04)
					np = bomb_search(&o, s, at, sub, lives, p);
				if (np > 0) {
					memcpy(seq + n, p, (size_t)np);
					res = n + np;
				}
			}
		}
	}
out:
	hl2_stateLoad(start);
	free(start); free(at);
	return res;
}

/* eof */
