/*
 * xrick/src/headless/rd2/hl2_solve.c
 *
 * xrick2-core only (branch `solver`, never shipped): beam search over held joystick
 * bytes, from the current state through one exit of the current submap, Rick never
 * dying (PLAN.md T47 phase 6, kb2/demo-solver.md). The design is RD1's
 * (src/headless/hl_solve.c, kb/demo-solver.md §11), rebuilt on RD2's state:
 *
 * - action:  one joystick byte held 2, 4 or 8 frames, or a bomb program: stand, drop
 *            (fire + down), run left or right, stand until just before or after the
 *            blast (measured: fuse 40 frames, then about 7 lethal frames); or throw
 *            (fire + down + left/right: the bomb slides 54 px), stand or back off
 * - search:  time-synchronous beam: candidates grouped by frame count, the <beam>
 *            best of each group expanded, at most CELL_CAP per Rick tile
 * - pruned:  Rick hit or dead, a life lost, the run over, any other submap or map
 * - merged:  states with the same hl2_stateKey (render buffers left out)
 * - ranked:  f = 4 * tile distance + 2 * joystick changes - ammo held (T43 D3: the
 *            exit first, then efficient, then natural)
 *
 * Snapshots (without the drawing buffers) are kept only for the states expanded; a
 * candidate is re-simulated from its parent's snapshot when it is expanded.
 *
 * The tile distance is a Dijkstra over the submap's tiles, backwards from the goal,
 * on Rick's footprint (2 columns, rows f-2..f standing, f-1..f crouched; feet row f)
 * with k = rows risen
 * since the last support: a jump rises at most JUMP_ROWS rows (measured: 33 px),
 * ladders without limit, one-way floors only upwards -- and, at RELAX per row,
 * higher than a jump or down through a floor (platforms). It is a guide only:
 * gravity, timing and the actors are left to the search.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rd2_mem.h"
#include "hl2.h"
#include "hl2_state.h"
#include "hl2_obs.h"
#include "hl2_solve.h"

void
hl2_solveDefaults(hl2_solveopt_t *o)
{
	o->beam = 128;
	o->jobs = 1;
	o->stall = 600;
	o->res = NULL;
	o->maxsteps = 3000;
	o->exit = -1;
	o->verbose = 0;
	o->minbombs = 0;
	o->minlaser = 0;
	o->wp_row = -1;
	o->wp_col = -1;
	o->stuck = NULL;
	o->stage = NULL;
	o->stage_n = NULL;
}

/* ----------------------------------------------------------------------- */
/* the goal                                                                */

#define R_RUN 0
#define R_GOAL 1
#define R_FAIL 2

static hl2_exit_t g_exit;
static int g_wp_row = -1, g_wp_col;
static int g_submap, g_map, g_lives, g_minbombs, g_minlaser;

static int
goal_set(const hl2_solveopt_t *o)
{
	hl2_exit_t e[HL2_EXITS_MAX];
	int n = hl2_exits(e, HL2_EXITS_MAX), i = o->exit < 0 ? hl2_solveRoute() : o->exit;

	g_wp_row = o->wp_row;
	g_wp_col = o->wp_col;
	g_submap = rd2_rw(HL2_SUBMAP);
	g_map = rd2_rw(HL2_MAP_PLAYING);
	g_lives = rd2_rw(HL2_LIVES);
	g_minbombs = o->minbombs;
	g_minlaser = o->minlaser;
	if (i < 0 || i >= n)
		return 0;
	g_exit = e[i];
	return 1;
}

static int
counters_ok(void)
{
	return rd2_rw(HL2_BOMBS) >= g_minbombs && rd2_rw(HL2_LASER) >= g_minlaser;
}

static int
grounded(void)
{
	return rd2_rw(0x12e1cu) != 0 || rd2_rw(0x12e1au) != 0;   /* landed / on a ladder */
}

static int
judge(int status)
{
	if (status == HL2_MAP || status == HL2_HANG || status == HL2_END)
		return g_exit.done && g_wp_row < 0 && counters_ok() ? R_GOAL : R_FAIL;
	if (status != HL2_STEP || rd2_rw(HL2_MAP_PLAYING) != g_map)
		return R_FAIL;
	if (hl2_rickDead() || rd2_rw(HL2_LIVES) < g_lives)
		return R_FAIL;
	if (rd2_rw(HL2_SUBMAP) != g_submap) {
		/* the exit asked for, not another one to the same submap: Rick's feet now on
		   its entry row b3 (level-tables.md §2) */
		if (g_wp_row >= 0 || g_exit.done || rd2_rw(HL2_SUBMAP) != g_exit.target ||
		    hl2_rickRow() != g_exit.entry)
			return R_FAIL;
		return counters_ok() ? R_GOAL : R_FAIL;
	}
	if (g_wp_row >= 0) {
		int r = hl2_rickRow(), c = hl2_rickCol();
		/* on the ground (not a ladder: the waypoint is where Rick acts next), on the
		   row, within a column */
		if (rd2_rw(0x12e1cu) != 0 && rd2_rw(0x12e1au) == 0 && r == g_wp_row &&
		    c >= g_wp_col - 1 && c <= g_wp_col + 1)
			return counters_ok() ? R_GOAL : R_FAIL;
	}
	return R_RUN;
}

/* ----------------------------------------------------------------------- */
/* tile distance                                                           */

#define FP_W 2
#define JUMP_ROWS 4
#define NK (JUMP_ROWS + 1)
#define DIST_INF 0xffff
/*
 * Moving platforms and lifts are objects (algo-objects.md), not tiles: a submap can
 * need one to rise past a jump's height (map 1 submap 5) or to go down through a
 * floor. The field allows both everywhere at RELAX per row, so it stays connected
 * and still prefers what the tiles alone allow; the search finds the platform.
 */
#define RELAX 8
/* going down through a floor is rarer (map 1 submap 2: the one-way floor at row 55 is
   walked around, and at RELAX the field pointed straight through it) */
#define RELAX_DOWN 40

static int d_rows;
static U8 *tattr;          /* the submap's attributes, d_rows x 32 */
static U16 *dist;          /* per (row, col, k) */

static U8
at(int r, int c)
{
	if (r < 0 || r >= d_rows || c < 0 || c >= HL2_COLS)
		return HL2_T_SOLID;
	return tattr[r * HL2_COLS + c];
}

/* rows f-n+1..f (feet row f) free of solid and lethal tiles */
static int
fp_rows(int f, int c, int n)
{
	int r, k;
	if (c < 0 || c + FP_W > HL2_COLS)
		return 0;
	for (r = f - n + 1; r <= f; r++)
		for (k = c; k < c + FP_W; k++)
			if (at(r, k) & (HL2_T_SOLID | HL2_T_LETHAL))
				return 0;
	return 1;
}

/* Rick standing takes 3 rows (21 px), crouching 2 (the crouch probe, algo-collision.md
   §3); the field lets him through 2 rows, at a cost, and drops diagonally that way */
#define fp_free(f, c) fp_rows(f, c, 2)
#define fp_stand(f, c) fp_rows(f, c, 3)

static int
support(int f, int c)
{
	return ((at(f + 1, c) | at(f + 1, c + 1)) & (HL2_T_FLOOR | HL2_T_SOLID)) != 0;
}

static int
ladder(int f, int c)
{
	int r, k;
	for (r = f - 2; r <= f + 1; r++)
		for (k = c; k < c + FP_W; k++)
			if (at(r, k) & HL2_T_LADDER)
				return 1;
	return 0;
}

/* a state's k as the field stores it: 0 where Rick can stand or climb */
static int
knorm(int f, int c, int k)
{
	return (support(f, c) || ladder(f, c)) ? 0 : k;
}

#define IDX(f, c, k) (((f) * HL2_COLS + (c)) * NK + (k))

/* forward edges of state (f, c, k): up to 6, into to[] / cost[] */
static int
edges(int f, int c, int k, int *to, int *cost)
{
	int n = 0, d, nf, nk, ground = support(f, c) || ladder(f, c);

	/* walk / drift; through a 2-row gap only crawling, from the ground: in the air Rick
	   is standing height (map 4 submap 0: the field crawled over the barrels, rows
	   24-25, which only a jump reaches -- under a ceiling a jump cannot pass) */
	for (d = -1; d <= 1; d += 2)
		if (ground ? fp_free(f, c + d) : fp_stand(f, c + d)) {
			nk = knorm(f, c + d, ground ? JUMP_ROWS : k);
			to[n] = IDX(f, c + d, nk); cost[n++] = fp_stand(f, c + d) ? 1 : 2;
		}
	nf = f + 1;                                                  /* fall / climb down */
	if (ground ? fp_free(nf, c) : fp_stand(nf, c)) {
		if (!support(f, c) || ladder(nf, c)) {
			to[n] = IDX(nf, c, knorm(nf, c, JUMP_ROWS)); cost[n++] = 1;
		} else {                                                 /* through a floor: a lift? */
			to[n] = IDX(nf, c, knorm(nf, c, JUMP_ROWS)); cost[n++] = RELAX_DOWN;
		}
	}
	nf = f - 1;                                                  /* climb / jump up */
	if (fp_stand(nf, c)) {
		if (ladder(f, c) && ladder(nf, c)) {
			to[n] = IDX(nf, c, knorm(nf, c, 0)); cost[n++] = 1;
		} else if (k < JUMP_ROWS) {
			to[n] = IDX(nf, c, knorm(nf, c, k + 1)); cost[n++] = 2;
		} else {                                                 /* higher than a jump: a lift? */
			to[n] = IDX(nf, c, knorm(nf, c, JUMP_ROWS)); cost[n++] = RELAX;
		}
	}
	return n;
}

/* the column Rick's anchor has when clamped against an exit's side (x 0 / $e8) */
#define EXIT_COL(side) ((side) == 1 ? 0 : (0xe8 + 4) >> 3)

/* Dijkstra over submap <sm> backwards from goal cell (gf, gc), over reversed edges */
static void
field_build(int sm, int gf, int gc)
{
	int ns, s, f, c, k, i, n, to[6], cost[6], *rh, *rn, *rto, *rc, ne = 0, nh;
	int *heap, hn = 0;

	d_rows = hl2_rowsOf(sm);
	free(tattr); free(dist);
	tattr = malloc((size_t)d_rows * HL2_COLS);
	for (f = 0; f < d_rows; f++)
		for (c = 0; c < HL2_COLS; c++)
			tattr[f * HL2_COLS + c] = hl2_attrOf(sm, f, c);
	ns = d_rows * HL2_COLS * NK;
	dist = malloc((size_t)ns * sizeof(U16));
	for (s = 0; s < ns; s++) dist[s] = DIST_INF;

	/* reversed adjacency: rh[s] = first edge into s, rn = next, rto = source */
	rh = malloc((size_t)ns * sizeof(int));
	rn = malloc((size_t)ns * 6 * sizeof(int));
	rto = malloc((size_t)ns * 6 * sizeof(int));
	rc = malloc((size_t)ns * 6 * sizeof(int));
	for (s = 0; s < ns; s++) rh[s] = -1;
	for (f = 0; f < d_rows; f++)
		for (c = 0; c + FP_W <= HL2_COLS; c++)
			for (k = 0; k < NK; k++) {
				if (!fp_free(f, c) || knorm(f, c, k) != k)
					continue;
				n = edges(f, c, k, to, cost);
				for (i = 0; i < n; i++) {
					rto[ne] = IDX(f, c, k); rc[ne] = cost[i];
					rn[ne] = rh[to[i]]; rh[to[i]] = ne++;
				}
			}

	heap = malloc((size_t)ns * 8 * sizeof(int));
#define PUSH(st) do { heap[hn++] = (st); for (nh = hn - 1; nh > 0 && dist[heap[(nh - 1) / 2]] > dist[heap[nh]]; nh = (nh - 1) / 2) { int t_ = heap[nh]; heap[nh] = heap[(nh - 1) / 2]; heap[(nh - 1) / 2] = t_; } } while (0)
	if (gc > HL2_COLS - FP_W) gc = HL2_COLS - FP_W;
	for (i = 1; i <= 3 && gc >= 0 && !fp_free(gf, gc); i++)   /* a goal in a wall: the nearest free cell */
		if (gc + i <= HL2_COLS - FP_W && fp_free(gf, gc + i)) gc += i;
		else if (gc - i >= 0 && fp_free(gf, gc - i)) gc -= i;
	for (k = 0; k < NK && gf >= 0 && gf < d_rows && gc >= 0; k++) {
		dist[IDX(gf, gc, k)] = 0;
		PUSH(IDX(gf, gc, k));
	}
	while (hn > 0) {
		int u = heap[0], e, i2, l, r, m;
		heap[0] = heap[--hn];
		for (i2 = 0; ; i2 = m) {                                  /* sift down */
			l = 2 * i2 + 1; r = l + 1; m = i2;
			if (l < hn && dist[heap[l]] < dist[heap[m]]) m = l;
			if (r < hn && dist[heap[r]] < dist[heap[m]]) m = r;
			if (m == i2) break;
			{ int t_ = heap[m]; heap[m] = heap[i2]; heap[i2] = t_; }
		}
		for (e = rh[u]; e >= 0; e = rn[e]) {
			int v = rto[e], nd = dist[u] + rc[e];
			if (nd < dist[v] && hn < ns * 8) {
				dist[v] = (U16)nd;
				PUSH(v);
			}
		}
	}
#undef PUSH
	free(heap); free(rh); free(rn); free(rto); free(rc);
	if (getenv("HL2_FIELD_DEBUG")) {         /* per anchor: x wall, . unreachable, else (best d / 10) % 10 */
		fprintf(stderr, "field: submap %d goal (%d,%d)", sm, gf, gc);
		for (f = 0; f < d_rows; f++) {
			fprintf(stderr, "%c%3d ", 10, f);
			for (c = 0; c + FP_W <= HL2_COLS; c++) {
				int d = DIST_INF;
				for (k = 0; k < NK; k++) if (dist[IDX(f, c, k)] < d) d = dist[IDX(f, c, k)];
				fputc(!fp_free(f, c) ? 'x' : d == DIST_INF ? '.' : '0' + (d / 10) % 10, stderr);
			}
		}
		fputc(10, stderr);
	}
}

/* the field's distance at anchor (f, c), with k as given or else the best k */
static int
field_at(int f, int c, int k)
{
	int d;

	if (c > HL2_COLS - FP_W) c = HL2_COLS - FP_W;
	if (f < 0 || f >= d_rows || c < 0)
		return 1000;
	d = dist[IDX(f, c, k)];
	if (d == DIST_INF)
		for (k = 0; k < NK; k++)
			if (dist[IDX(f, c, k)] < d) d = dist[IDX(f, c, k)];
	return d == DIST_INF ? 1000 : d;
}

/* the goal's field, in the current submap */
static void
field_goal(void)
{
	if (g_wp_row >= 0)
		field_build(g_submap, g_wp_row, g_wp_col);
	else
		field_build(g_submap, g_exit.row, EXIT_COL(g_exit.side));
}

/* Rick's distance now; in the air k is not known from the state: half a jump */
static int
field_rick(void)
{
	int f = hl2_rickRow(), c = hl2_rickCol(), k, d;

	if (c > HL2_COLS - FP_W) c = HL2_COLS - FP_W;
	if (f < 0 || f >= d_rows || c < 0)
		return 1000;
	k = grounded() ? 0 : JUMP_ROWS / 2;
	d = dist[IDX(f, c, k)];
	if (d == DIST_INF) {                    /* Rick's own cell off the field: its best k */
		for (k = 0; k < NK; k++)
			if (dist[IDX(f, c, k)] < d) d = dist[IDX(f, c, k)];
	}
	return d == DIST_INF ? 1000 : d;
}

/* ----------------------------------------------------------------------- */
/* the route through the map's submaps                                     */

/*
 * Dijkstra over (submap, entry row, entry column) nodes, from Rick's cell now to
 * any map-done exit. A node's edges are its submap's exits that the tile field
 * reaches from the entry, weighted by that distance; taking exit e lands at
 * (e.target, e.entry, the opposite side): $14434 puts Rick at x $e8 after a left
 * exit, 0 after a right one, and the new scroll keeps his feet on row b3
 * (algo-flow.md §9). Returns the index of the current submap's exit that starts
 * the shortest such path, -1 if none. The field knows nothing of actors, so a
 * route can still fail in the search.
 */
#define RT_MAX 512

static int route_cost = -1;      /* the last hl2_solveRoute's whole path, in field steps */

int
hl2_solveRouteCost(void)
{
	return route_cost;
}

int
hl2_solveRoute(void)
{
	static int ns_[RT_MAX], nf_[RT_MAX], nc_[RT_MAX], nd_[RT_MAX], first[RT_MAX], done_[RT_MAX];
	hl2_exit_t e[HL2_EXITS_MAX];
	int nn, i, j, n, u, nsub = hl2_submaps(), best_d = -1, best_exit = -1;
	int rc = hl2_rickCol();

	if (rc > HL2_COLS - FP_W) rc = HL2_COLS - FP_W;
	ns_[0] = rd2_rw(HL2_SUBMAP); nf_[0] = hl2_rickRow(); nc_[0] = rc;
	nd_[0] = 0; first[0] = -1; done_[0] = 0;
	nn = 1;
	for (;;) {
		for (u = -1, i = 0; i < nn; i++)
			if (!done_[i] && (u < 0 || nd_[i] < nd_[u]))
				u = i;
		if (u < 0 || (best_d >= 0 && nd_[u] >= best_d))
			break;                              /* nothing left that could be shorter */
		done_[u] = 1;
		n = hl2_exitsOf(ns_[u], e, HL2_EXITS_MAX);
		for (i = 0; i < n; i++) {
			int d, tf, tc, k;
			field_build(ns_[u], e[i].row, EXIT_COL(e[i].side));
			d = field_at(nf_[u], nc_[u], 0);
			if (getenv("HL2_ROUTE_DEBUG"))
				fprintf(stderr, "route: submap %d at (%d,%d) cost %d: exit %d (%d row %d -> %d entry %d%s): %d\n",
				        ns_[u], nf_[u], nc_[u], nd_[u], i, e[i].side, e[i].row, e[i].target,
				        e[i].entry, e[i].done ? " done" : "", d);
			if (d >= 1000)
				continue;
			k = nd_[u] + d + 1;
			if (e[i].done) {
				if (best_d < 0 || k < best_d) {
					best_d = k;
					best_exit = u == 0 ? i : first[u];
				}
				continue;
			}
			if (e[i].target >= nsub)
				continue;
			tf = e[i].entry;
			tc = e[i].side == 1 ? EXIT_COL(2) : EXIT_COL(1);
			if (tc > HL2_COLS - FP_W) tc = HL2_COLS - FP_W;
			for (j = 0; j < nn; j++)
				if (ns_[j] == e[i].target && nf_[j] == tf && nc_[j] == tc)
					break;
			if (j == nn) {
				if (nn == RT_MAX) continue;
				ns_[j] = e[i].target; nf_[j] = tf; nc_[j] = tc; nd_[j] = 1 << 29;
				done_[j] = 0; nn++;
			}
			if (!done_[j] && k < nd_[j]) {
				nd_[j] = k;
				first[j] = u == 0 ? i : first[u];
			}
		}
	}
	route_cost = best_d;
	return best_exit;
}

int
hl2_solveDistance(const hl2_solveopt_t *o)
{
	if (!goal_set(o))
		return -1;
	field_goal();
	return field_rick();
}

/* ----------------------------------------------------------------------- */
/* actions                                                                 */

static const U8 act_mask[] = {
	0, 0x04, 0x08, 0x01, 0x02, 0x05, 0x09, 0x06, 0x0a,
	0x81, 0x84, 0x88      /* laser (fire + up), melee left / right */
};
static const U8 act_len[] = { 2, 4, 8 };
#define N_MASK ((int)sizeof(act_mask))
#define N_LEN ((int)sizeof(act_len))
#define N_BOMB 16
#define N_LASER 3
#define N_ACT (N_MASK * N_LEN + N_BOMB + N_LASER)
#define MAXLEN 52

#define K_MOVE 0
#define K_BOMB 1
#define K_LASER 2

typedef struct {
	U8 n;
	U8 mask[4];
	U8 len[4];
	U8 steps;
	U8 kind;
} prog_t;

static prog_t progs[N_ACT];

static void
progs_init(void)
{
	static const U8 dirs[2] = { 0x04, 0x08 }, runs[2] = { 12, 24 }, ends[2] = { 38, MAXLEN };
	int a = 0, m, l, d, r, e;

	if (progs[0].n)
		return;
	for (l = 0; l < N_LEN; l++)
		for (m = 0; m < N_MASK; m++, a++) {
			progs[a].n = 1;
			progs[a].mask[0] = act_mask[m];
			progs[a].len[0] = act_len[l];
			progs[a].steps = act_len[l];
		}
	for (e = 0; e < 2; e++)
		for (d = 0; d < 2; d++)
			for (r = 0; r < 2; r++, a++) {
				progs[a].n = 4;
				progs[a].mask[0] = 0;    progs[a].len[0] = 2;   /* stand up first */
				progs[a].mask[1] = 0x82; progs[a].len[1] = 2;   /* drop */
				progs[a].mask[2] = dirs[d]; progs[a].len[2] = runs[r];
				progs[a].mask[3] = 0;
				progs[a].len[3] = (U8)(ends[e] - 4 - runs[r]);
				progs[a].steps = ends[e];
			}
	/*
	 * throws: fire + down + left/right gives the bomb dx -/+$200, slowed by 8 a frame
	 * (algo-player.md §4, §11). Measured 2026-10-02 on the four start floors: it stops
	 * 54 px away and blows at frame 40, so Rick may stay where he is (then: stand) or
	 * back off the other way (12 frames)
	 */
	for (e = 0; e < 2; e++)
		for (d = 0; d < 2; d++)
			for (r = 0; r < 2; r++, a++) {
				progs[a].n = 4;
				progs[a].mask[0] = 0;    progs[a].len[0] = 2;
				progs[a].mask[1] = (U8)(0x82 | dirs[d]); progs[a].len[1] = 2;   /* throw */
				progs[a].mask[2] = r ? dirs[1 - d] : 0; progs[a].len[2] = 12;
				progs[a].mask[3] = 0;
				progs[a].len[3] = (U8)(ends[e] - 4 - 12);
				progs[a].steps = ends[e];
			}
	for (a = N_MASK * N_LEN; a < N_MASK * N_LEN + N_BOMB; a++)
		progs[a].kind = K_BOMB;
	/*
	 * laser: outside the tunnel [$12e26] re-arms only on a frame with fire held and up
	 * not (rd2_player.c L133ee), so a second shot needs fire alone first; then face as
	 * is, left or right (one frame of walking), and fire + up
	 */
	for (d = 0; d < N_LASER; d++, a++) {
		progs[a].kind = K_LASER;
		progs[a].mask[0] = 0x80; progs[a].len[0] = 2;
		if (d == 0) {
			progs[a].n = 3;
			progs[a].mask[1] = 0x81; progs[a].len[1] = 3;
			progs[a].mask[2] = 0;    progs[a].len[2] = 3;
		} else {
			progs[a].n = 4;
			progs[a].mask[1] = dirs[d - 1]; progs[a].len[1] = 1;
			progs[a].mask[2] = 0x81; progs[a].len[2] = 3;
			progs[a].mask[3] = 0;    progs[a].len[3] = 2;
		}
		progs[a].steps = 8;
	}
}

static U8
prog_mask(int a, int k)
{
	int s;
	for (s = 0; s < progs[a].n; s++) {
		if (k < progs[a].len[s])
			return progs[a].mask[s];
		k -= progs[a].len[s];
	}
	return progs[a].mask[progs[a].n - 1];
}

static U32
prog_toggles(int a, U8 prev)
{
	U32 t = progs[a].mask[0] != prev;
	int s;
	for (s = 1; s < progs[a].n; s++)
		t += progs[a].mask[s] != progs[a].mask[s - 1];
	return t;
}

/* ----------------------------------------------------------------------- */
/* the search                                                              */

typedef struct {
	int parent;
	U8 act, mask;
	U32 g, toggles;
	long f;
	U16 cell;
	U8 *snap;     /* while expanded and its children may still be */
} node_t;

static node_t *nodes;
static int n_nodes, cap_nodes;

static int
node_new(int parent, U8 act, U32 g, U32 toggles, long f)
{
	if (n_nodes == cap_nodes) {
		cap_nodes = cap_nodes ? cap_nodes * 2 : 1 << 16;
		nodes = realloc(nodes, (size_t)cap_nodes * sizeof(node_t));
	}
	nodes[n_nodes].parent = parent;
	nodes[n_nodes].act = act;
	nodes[n_nodes].mask = act == 0xff ? 0xff : prog_mask(act, progs[act].steps - 1);
	nodes[n_nodes].g = g;
	nodes[n_nodes].toggles = toggles;
	nodes[n_nodes].f = f;
	nodes[n_nodes].cell = 0;
	nodes[n_nodes].snap = NULL;
	return n_nodes++;
}

#define SEEN_BITS 22
static unsigned long long *seen;

static int
seen_add(unsigned long long k)
{
	size_t m = ((size_t)1 << SEEN_BITS) - 1, i;
	if (!k) k = 1;
	for (i = (size_t)(k ^ (k >> 29)) & m; seen[i]; i = (i + 1) & m)
		if (seen[i] == k)
			return 0;
	seen[i] = k;
	return 1;
}

/* the path to <node> plus the first <extra> frames of program <act> */
static int
path(int node, int act, int extra, U8 *seq, int max)
{
	int n = (int)nodes[node].g + extra, i, k;

	if (n > max)
		return -1;
	for (i = (int)nodes[node].g, k = 0; k < extra; k++)
		seq[i + k] = prog_mask(act, k);
	for (; nodes[node].parent >= 0; node = nodes[node].parent)
		for (k = 0; k < progs[nodes[node].act].steps; k++)
			seq[(int)nodes[nodes[node].parent].g + k] = prog_mask(nodes[node].act, k);
	return n;
}

/* play program <a>; R_RUN after its last frame, else the frame's verdict, *j = frames played */
static int
play(int a, int *j)
{
	int r = R_RUN;
	for (*j = 0; *j < progs[a].steps; ) {
		r = judge(hl2_step(prog_mask(a, *j)));
		(*j)++;
		if (r != R_RUN)
			break;
	}
	return r;
}

/*
 * staging: the search keeps, per Rick tile, its closest state with Rick on the
 * ground; on failure the first of them, closest first, that survives STAGE_IDLE
 * frames of no input is handed back (o->stage), so the caller can commit that much
 * progress and search again from there -- a beam that reaches a room and dies in it
 * every time still leaves the way to that room. Per tile, not the K closest overall:
 * those can all lie in the deadly room (map 2 submap 1, 2026-10-03: distance 315
 * reached past two dart traps, nothing committed).
 */
#define STAGE_TRY 256
#define STAGE_IDLE 50

#define LASER_VALUE 4    /* one tile */
#define BOMB_VALUE 16    /* four tiles */

#define CELL_CAP 4
#define BUCKET_CAP 4

typedef struct { int n, cap; int *node; } bucket_t;

/* one child: program <a> played from the state loaded */
typedef struct {
	S8 r;            /* R_GOAL, R_FAIL, R_RUN; -1: not played */
	U8 j;            /* frames played */
	U8 landed;
	U8 pad;
	U16 cell;
	U16 pad2;
	int h, row, col;
	long ammo;       /* LASER_VALUE * laser + BOMB_VALUE * bombs */
	unsigned long long key;
} child_t;

static int
skip_prog(int a)
{
	if (progs[a].kind == K_BOMB)             /* a bomb in play already, or none to spare */
		return rd2_rw(0x16b12u) != 0 || rd2_rw(HL2_BOMBS) <= g_minbombs;
	if (progs[a].kind == K_LASER)            /* a shot in flight, or none to spare */
		return rd2_rw(0x16902u) != 0 || rd2_rw(HL2_LASER) <= g_minlaser;
	return 0;
}

/* every program from snapshot <snap>, into ch[N_ACT] */
static void
expand(const U8 *snap, child_t *ch)
{
	int a, j, cell;

	for (a = 0; a < N_ACT; a++) {
		child_t *c = &ch[a];
		hl2_stateLoad(snap);
		c->r = -1;
		if (skip_prog(a))
			continue;
		c->r = (S8)play(a, &j);
		c->j = (U8)j;
		if (c->r != R_RUN)
			continue;
		c->key = hl2_stateKey();
		c->h = field_rick();
		c->row = hl2_rickRow();
		c->col = hl2_rickCol();
		c->landed = rd2_rw(0x12e1cu) != 0;
		c->ammo = (long)LASER_VALUE * rd2_rw(HL2_LASER) + (long)BOMB_VALUE * rd2_rw(HL2_BOMBS);
		cell = c->row * HL2_COLS + c->col;
		c->cell = (U16)((cell >= 0 && cell < d_rows * HL2_COLS) ? cell : d_rows * HL2_COLS);
	}
}

/*
 * workers (o->jobs > 1): processes forked when the search starts, so each has the
 * game, the goal and the field as they are then. Per frame group the parent puts the
 * kept states' snapshots in shared memory; the workers take them one at a time
 * (an atomic counter) and write the children's results back. The parent then takes
 * the results in the same order as the serial search does: the outcome does not
 * depend on the number of workers.
 */
typedef struct {
	int next, n;
} wctl_t;

static int w_n;
static pid_t w_pid[64];
static int w_cmd[64], w_done = -1;
static U8 *w_slab;
static child_t *w_res;
static wctl_t *w_ctl;
static size_t w_slab_sz, w_res_sz, w_sz;

static void
workers_stop(void)
{
	int i;
	for (i = 0; i < w_n; i++) {
		close(w_cmd[i]);
		waitpid(w_pid[i], NULL, 0);
	}
	if (w_done >= 0) close(w_done);
	if (w_slab) munmap(w_slab, w_slab_sz);
	if (w_res) munmap(w_res, w_res_sz);
	if (w_ctl) munmap(w_ctl, sizeof(wctl_t));
	w_n = 0; w_done = -1; w_slab = NULL; w_res = NULL; w_ctl = NULL;
}

static void
workers_start(int jobs, int beam, size_t sz)
{
	int i, p[2], d[2];

	if (jobs > 64) jobs = 64;
	w_sz = sz;
	w_slab_sz = (size_t)beam * sz;
	w_res_sz = (size_t)beam * N_ACT * sizeof(child_t);
	w_slab = mmap(NULL, w_slab_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	w_res = mmap(NULL, w_res_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	w_ctl = mmap(NULL, sizeof(wctl_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (w_slab == MAP_FAILED || w_res == MAP_FAILED || w_ctl == MAP_FAILED || pipe(d) < 0) {
		w_slab = NULL; w_res = NULL; w_ctl = NULL;
		return;                              /* the search runs alone */
	}
	fflush(stdout); fflush(stderr);
	for (i = 0; i < jobs; i++) {
		pid_t pid;
		if (pipe(p) < 0)
			break;
		pid = fork();
		if (pid < 0) {
			close(p[0]); close(p[1]);
			break;
		}
		if (pid == 0) {                      /* a worker: one byte per group, EOF = stop */
			char c;
			int k;
			close(p[1]); close(d[0]);
			for (k = 0; k < i; k++) close(w_cmd[k]);
			while (read(p[0], &c, 1) == 1) {
				int t;
				while ((t = __atomic_fetch_add(&w_ctl->next, 1, __ATOMIC_SEQ_CST)) < w_ctl->n)
					expand(w_slab + (size_t)t * w_sz, w_res + (size_t)t * N_ACT);
				if (write(d[1], &c, 1) != 1)
					break;
			}
			_exit(0);
		}
		close(p[0]);
		w_cmd[i] = p[1];
		w_pid[i] = pid;
		w_n = i + 1;
	}
	close(d[1]);
	w_done = d[0];
}

/* the workers expand snapshots w_slab[0..n) into w_res */
static void
workers_run(int n)
{
	char c = 'g';
	int i, got = 0;
	w_ctl->next = 0;
	w_ctl->n = n;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	for (i = 0; i < w_n; i++)
		if (write(w_cmd[i], &c, 1) != 1)
			break;
	while (got < i && read(w_done, &c, 1) == 1)
		got++;
}

static double
clk(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int
hl2_solve(const hl2_solveopt_t *o, U8 *seq, int max)
{
	size_t sz;
	int nb = o->maxsteps + MAXLEN + 1, g, i, k, a, j, found = -1, n_keep;
	int best_h = 1 << 30, best_g = 0, root, n_dup = 0, n_fail = 0, best_node = 0;
	int best_row = -1, best_col = -1, ncell, par_on, n_jobs = 1;
	int *st_node, *st_h;
	bucket_t *bk;
	int *order, *keep, *expanded = NULL, n_exp = 0, cap_exp = 0;
	U8 *cellcount, *start, *full;
	child_t *ch;
	double t_0 = clk(), t_s = 0, t_a, t_b = 0, tm_prep = 0, tm_exp = 0, tm_merge = 0;

	progs_init();
	full = malloc(hl2_stateSize());         /* the start with its drawing buffers, put back at the end */
	hl2_stateSave(full);
	hl2_stateRender(0);
	sz = hl2_stateSize();
	start = malloc(sz);
	hl2_stateSave(start);
	if (o->res) { o->res->best_h = 1000; o->res->row = hl2_rickRow(); o->res->col = hl2_rickCol(); }
	if (!goal_set(o)) {
		hl2_stateRender(1);
		free(start); free(full);
		return -1;
	}
	field_goal();
	if (g_wp_row >= 0 && field_rick() >= 1000) {   /* a waypoint the field cannot reach: no search */
		hl2_stateRender(1);
		hl2_stateLoad(full);
		free(start); free(full);
		if (o->stage_n) *o->stage_n = -1;
		return -1;
	}
	ncell = d_rows * HL2_COLS;
	bk = calloc((size_t)nb, sizeof(bucket_t));
	order = malloc((size_t)o->beam * BUCKET_CAP * sizeof(int));
	keep = malloc((size_t)o->beam * BUCKET_CAP * sizeof(int));
	cellcount = malloc((size_t)ncell);
	st_node = malloc((size_t)ncell * sizeof(int));
	st_h = malloc((size_t)ncell * sizeof(int));
	for (i = 0; i < ncell; i++) st_node[i] = -1;
	ch = malloc((size_t)N_ACT * sizeof(child_t));
	seen = calloc((size_t)1 << SEEN_BITS, sizeof(unsigned long long));
	n_nodes = 0;
	root = node_new(-1, 0xff, 0, 0, 0);
	nodes[root].snap = malloc(sz);
	memcpy(nodes[root].snap, start, sz);
	seen_add(hl2_stateKey());
	bk[0].cap = o->beam * BUCKET_CAP;
	bk[0].node = malloc((size_t)bk[0].cap * sizeof(int));
	bk[0].node[bk[0].n++] = root;
	par_on = 0;
	if (o->jobs > 1) {
		workers_start(o->jobs, o->beam, sz);
		par_on = w_n > 0;
	}

	for (g = 0; g <= o->maxsteps && found < 0; g++) {
		bucket_t *b = &bk[g];
		int bh = 1 << 30;

		/* free the snapshots no child can need any more */
		for (i = 0; i < n_exp; ) {
			if ((int)nodes[expanded[i]].g + MAXLEN < g) {
				free(nodes[expanded[i]].snap);
				nodes[expanded[i]].snap = NULL;
				expanded[i] = expanded[--n_exp];
			} else
				i++;
		}
		if (!b->n)
			continue;

		/* the <beam> best, at most CELL_CAP per Rick tile */
		t_s = clk();
		for (i = 0; i < b->n; i++) order[i] = b->node[i];
		for (i = 1; i < b->n; i++) {
			int x = order[i];
			for (k = i; k > 0 && nodes[order[k - 1]].f > nodes[x].f; k--)
				order[k] = order[k - 1];
			order[k] = x;
		}
		memset(cellcount, 0, (size_t)ncell);
		for (n_keep = 0, i = 0; i < b->n && n_keep < o->beam; i++) {
			int c = nodes[order[i]].cell;
			if (c < ncell) {
				if (cellcount[c] >= CELL_CAP) continue;
				cellcount[c]++;
			}
			keep[n_keep++] = order[i];
		}

		for (i = 0; i < n_keep; i++) {
			int nd = keep[i], par = nodes[nd].parent;
			if (!nodes[nd].snap) {           /* re-simulate it from its parent */
				hl2_stateLoad(nodes[par].snap);
				play(nodes[nd].act, &j);
				nodes[nd].snap = malloc(sz);
				hl2_stateSave(nodes[nd].snap);
			}
			if (n_exp == cap_exp) {
				cap_exp = cap_exp ? cap_exp * 2 : 1024;
				expanded = realloc(expanded, (size_t)cap_exp * sizeof(int));
			}
			expanded[n_exp++] = nd;
			if (par_on)
				memcpy(w_slab + (size_t)i * sz, nodes[nd].snap, sz);
		}
		t_a = clk();
		if (par_on)
			workers_run(n_keep);
		t_b = clk();
		tm_prep += t_a - t_s;
		tm_exp += t_b - t_a;

		for (i = 0; i < n_keep && found < 0; i++) {
			int nd = keep[i];
			child_t *cs = par_on ? w_res + (size_t)i * N_ACT : ch;

			if (!par_on)
				expand(nodes[nd].snap, ch);
			for (a = 0; a < N_ACT; a++) {
				child_t *c = &cs[a];
				int child;
				long f;

				if (c->r < 0)
					continue;
				if (c->r == R_GOAL) {
					found = path(nd, a, c->j, seq, max);
					break;
				}
				if (c->r == R_FAIL) { n_fail++; continue; }
				if (g + progs[a].steps >= nb) continue;
				if (!seen_add(c->key)) { n_dup++; continue; }
				if (c->h < bh) bh = c->h;
				if (c->h < best_h) {
					best_node = n_nodes;         /* the child made just below */
					best_h = c->h; best_g = g;
					best_row = c->row; best_col = c->col;
				}
				child = node_new(nd, (U8)a, (U32)g + progs[a].steps,
				                 nodes[nd].toggles + prog_toggles(a, nodes[nd].mask), 0);
				/* ammo held is worth something: RD2 refills it only at the next map (or
				   a life), and switches need bombs (map 1 submap 4: two blocks) */
				f = 4L * c->h + 2L * (long)nodes[child].toggles - c->ammo;
				nodes[child].f = f;
				nodes[child].cell = c->cell;
				if (o->stage && c->landed && c->cell < ncell) {   /* landed: a staging candidate */
					int q = c->cell;
					if (st_node[q] < 0 || c->h < st_h[q]) { st_node[q] = child; st_h[q] = c->h; }
				}
				{
					bucket_t *cb = &bk[nodes[child].g];
					if (!cb->node) {
						cb->cap = o->beam * BUCKET_CAP;
						cb->node = malloc((size_t)cb->cap * sizeof(int));
					}
					if (cb->n < cb->cap)
						cb->node[cb->n++] = child;
					else {                   /* full: replace the worst if better */
						int w = 0, q;
						for (q = 1; q < cb->n; q++)
							if (nodes[cb->node[q]].f > nodes[cb->node[w]].f) w = q;
						if (nodes[cb->node[w]].f > f)
							cb->node[w] = child;
					}
				}
			}
		}
		(void)bh;
		tm_merge += clk() - t_b;
		if (o->verbose && g % 100 == 0)
			fprintf(stderr, "solve: frame %d, %d kept, best distance %d (at %d), %d nodes, "
			        "%d dups, %d fails\n", g, n_keep, best_h, best_g, n_nodes, n_dup, n_fail);
		if (g - best_g > o->stall)
			break;
	}
	if (par_on) {
		n_jobs = w_n;
		workers_stop();
	}

	if (o->res && best_row >= 0) {
		o->res->best_h = best_h;
		o->res->row = best_row;
		o->res->col = best_col;
	}
	if (found < 0 && o->stuck) {
		FILE *sf = fopen(o->stuck, "wb");
		int sn = path(best_node, 0, 0, seq, max);
		if (sf && sn > 0)
			fwrite(seq, 1, (size_t)sn, sf);
		if (sf)
			fclose(sf);
		if (o->verbose)
			fprintf(stderr, "solve: closest state (%d frames) written to %s\n", sn, o->stuck);
	}
	if (found < 0 && o->stage) {
		/* the tiles' best states, closest first (then fewest frames); STAGE_TRY tested */
		int *cand = malloc((size_t)ncell * sizeof(int)), nc = 0, q, t, sn, ok;
		for (q = 0; q < ncell; q++)
			if (st_node[q] >= 0) cand[nc++] = q;
		for (q = 1; q < nc; q++) {
			int x = cand[q];
			for (t = q; t > 0 && (st_h[cand[t - 1]] > st_h[x] || (st_h[cand[t - 1]] == st_h[x] &&
			     nodes[st_node[cand[t - 1]]].g > nodes[st_node[x]].g)); t--)
				cand[t] = cand[t - 1];
			cand[t] = x;
		}
		*o->stage_n = -1;
		for (t = 0; t < nc && t < STAGE_TRY && *o->stage_n < 0; t++) {
			int nd = st_node[cand[t]];
			sn = path(nd, 0, 0, o->stage, max);
			hl2_stateRender(1);
			hl2_stateLoad(full);
			for (ok = sn > 0, q = 0; ok && q < sn + STAGE_IDLE; q++) {
				int st = hl2_step(q < sn ? o->stage[q] : 0);
				if (st != HL2_STEP || hl2_rickDead() || rd2_rw(HL2_LIVES) < g_lives ||
				    rd2_rw(HL2_SUBMAP) != g_submap)
					ok = 0;
			}
			hl2_stateRender(0);
			if (ok) {
				*o->stage_n = sn;
				if (o->verbose)
					fprintf(stderr, "solve: stage: %d frames to distance %d (candidate %d of %d)\n",
					        sn, st_h[cand[t]], t + 1, nc);
			}
		}
		free(cand);
	}
	for (i = 0; i < n_nodes; i++)
		free(nodes[i].snap), nodes[i].snap = NULL;
	for (g = 0; g < nb; g++) free(bk[g].node);
	free(bk); free(order); free(keep); free(cellcount); free(seen); free(expanded);
	free(st_node); free(st_h); free(ch);
	hl2_stateRender(1);
	hl2_stateLoad(full);
	free(start); free(full);
	if (o->verbose)
		fprintf(stderr, "solve: %s, %d nodes, best distance %d; %.1f s: select+resim %.1f, "
		        "expand %.1f, merge %.1f (jobs %d)\n", found >= 0 ? "found" : "NOT found", n_nodes,
		        best_h, clk() - t_0, tm_prep, tm_exp, tm_merge, n_jobs);
	return found;
}

/* play <seq> from the current state; frames to the goal, or -1 */
int
hl2_solveReplay(const hl2_solveopt_t *o, const U8 *seq, int n)
{
	int i, r;

	if (!goal_set(o))
		return -1;
	for (i = 0; i < n; i++) {
		r = judge(hl2_step(seq[i]));
		if (r == R_GOAL) return i + 1;
		if (r == R_FAIL) return -1;
	}
	return -1;
}

static int
toggles(const U8 *s, int n)
{
	int i, t = 0;
	for (i = 1; i < n; i++)
		t += s[i] != s[i - 1];
	return t;
}

/*
 * fewer joystick changes: repeatedly, shortest run first, give a run the mask of
 * its left or right neighbour, or none; keep the change when the goal is still
 * reached without dying, in no more frames
 */
int
hl2_solvePolish(const hl2_solveopt_t *o, U8 *seq, int n)
{
	size_t sz = hl2_stateSize();
	U8 *start = malloc(sz), *try = malloc((size_t)n);
	int improved = 1, i, j, k, len, got, side, m, runs;
	static int starts[1 << 15], lens[1 << 15];

	hl2_stateSave(start);
	while (improved) {
		improved = 0;
		for (runs = 0, i = 0; i < n && runs < (1 << 15); i = j) {
			for (j = i; j < n && seq[j] == seq[i]; j++) ;
			starts[runs] = i; lens[runs] = j - i; runs++;
		}
		for (len = 1; len <= n && !improved; len++)
			for (k = 0; k < runs && !improved; k++) {
				if (lens[k] != len) continue;
				for (side = 0; side < 3 && !improved; side++) {
					if (side == 0 && k == 0) continue;
					if (side == 1 && k == runs - 1) continue;
					m = side == 0 ? seq[starts[k - 1]] : side == 1 ? seq[starts[k + 1]] : 0;
					if (m == seq[starts[k]]) continue;
					memcpy(try, seq, (size_t)n);
					memset(try + starts[k], m, (size_t)lens[k]);
					hl2_stateLoad(start);
					got = hl2_solveReplay(o, try, n);
					if (got > 0 && toggles(try, got) < toggles(seq, n)) {
						memcpy(seq, try, (size_t)got);
						n = got;
						improved = 1;
					}
				}
			}
	}
	hl2_stateLoad(start);
	free(start); free(try);
	return n;
}

/* eof */
