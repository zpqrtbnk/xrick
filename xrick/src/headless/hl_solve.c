/*
 * xrick/src/headless/hl_solve.c
 *
 * xrick-core only (branch `solver`, never shipped): beam search over held
 * controls, from the current state to the target submap, rick never dying
 * (PLAN.md T43 phase 6, kb/demo-solver.md §11).
 *
 * - action:  a program -- one CONTROL_* mask held 2, 4 or 8 steps, or dynamite:
 *            drop, run 12/24 steps, stand until the blast is over (progs_init)
 * - search:  time-synchronous beam: states grouped by step count, the <beam> best
 *            of each group kept, at most CELL_CAP per rick tile (search)
 * - pruned:  rick dying (zombie / dead), fewer lives, game over, any other submap
 * - merged:  states with the same hl_stateKey (the state minus the counters)
 * - ranked:  f = 4 * tile distance + 2 * control changes - credits for progress
 *            (placements done, traps defused) + a penalty while a wall entity
 *            stands (T43 D3: exit first, then efficient, then natural)
 * - retried: a failed search closes the tiles where rick nearly always died, then
 *            searches again (hl_solve)
 *
 * The tile distance is a Dijkstra over the submap's tiles (decoded here the way
 * maps.c map_expand does, plus one map_map height past them), backwards from the
 * target exit's rows, on rick's footprint -- standing, or crawling under a lower
 * ceiling -- with one-way floors crossed upwards only and a climb up costing more
 * where there is nothing to climb. It is only a guide: gravity, timing and
 * entities are left to the search.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hl_solve.h"
#include "hl_state.h"
#include "control.h"
#include "env.h"
#include "ents.h"
#include "maps.h"
#include "game.h"
#include "e_rick.h"
#include "e_bomb.h"
#include "hl_dump.h"

/* ----------------------------------------------------------------------- */
/* the goal                                                                */

static int g_target;              /* submap, or HL_SOLVE_NEXTMAP */
static U16 g_submap, g_map;       /* where the search started */
static U8 g_lives;
static int g_died;                /* the last R_FAIL was rick dying */
static int g_minbombs;            /* bombs rick must still hold at the exit */
/* hints from outside (the MCP): a waypoint to reach instead of the exit, and
   anchor rectangles closed from the start -- hl_solveWaypoint / hl_solveForbid */
static int g_wp_row = -1, g_wp_col = -1;
#define FORBID_MAX 16
static int g_forbid[FORBID_MAX][4], g_n_forbid;
static int rick_cell(void);

#define R_RUN 0   /* nothing decided yet */
#define R_GOAL 1  /* reached the target */
#define R_FAIL 2  /* dead, game over, or somewhere else */

static int
judge(U8 status)
{
  if (env_map != g_map || status == GAME_HL_END)
    return g_target == HL_SOLVE_NEXTMAP ? R_GOAL : R_FAIL;
  if (status != GAME_HL_STEP)
    return R_FAIL;
  if (E_RICK_STTST(E_RICK_STDEAD|E_RICK_STZOMBIE) || env_lives < g_lives)
  {
    g_died = 1;  /* for the death map, see hl_solve */
    return R_FAIL;
  }
  if (env_submap != g_submap)
  {
    if ((int)env_submap != g_target || g_wp_row >= 0)
      return R_FAIL;
    return env_bombs >= g_minbombs ? R_GOAL : R_FAIL;  /* see hl_solveMinBombs */
  }
  if (g_wp_row >= 0)
  {
    /* a waypoint (hl_solveWaypoint): rick's anchor within one tile of it, and
       rick on the ground -- a state taken mid-jump can be doomed (0x09: falling
       onto an enemy), and the next solve starts from it */
    int cell = rick_cell(), r = cell / 0x20, c = cell % 0x20;
    if (cell >= 0 && !E_RICK_STTST(E_RICK_STJUMP|E_RICK_STCLIMB) &&
	r >= g_wp_row - 1 && r <= g_wp_row + 1 &&
	c >= g_wp_col - 1 && c <= g_wp_col + 1)
      return env_bombs >= g_minbombs ? R_GOAL : R_FAIL;  /* as at the exit */
  }
  return R_RUN;
}

static void
goal_set(int target)
{
  g_target = target == HL_SOLVE_AUTO ? hl_solveTarget() : target;
  g_submap = env_submap;
  g_map = env_map;
  g_lives = env_lives;
}

/*
 * the forward exit: the connector to the highest submap above this one (the game
 * runs through its submaps in increasing order), else the one to the next map.
 * NOT "next map first": submap 0x00 has a next-map connector on its left edge, at
 * rick's start rows, that is not the way on.
 */
int
hl_solveTarget(void)
{
  U16 c;
  int best = -3, nextmap = 0;

  for (c = map_submaps[env_submap].connect; map_connect[c].dir != 0xff; c++)
  {
    if (map_connect[c].submap == 0xff)
      nextmap = 1;
    else if ((int)map_connect[c].submap > (int)env_submap &&
	     (int)map_connect[c].submap > best)
      best = map_connect[c].submap;
  }
  if (best >= 0)
    return best;
  return nextmap ? HL_SOLVE_NEXTMAP : best;
}

/* ----------------------------------------------------------------------- */
/* tile distance to the exit                                               */

#define DIST_INF 0xffff

static U16 *dist;       /* tile distance to the exit, walls ignored (best over k) */
static U16 *dist_wall;  /* the same, every wall standing (best over k) */
/*
 * wall entities (ENT_FLG_STOPRICK placements): at most MAXW tracked, each with
 * its bit; wallcell holds, per anchor, the bits of the walls its footprint
 * touches. One distance field per subset of standing walls (walls_present), a
 * wall's cells costing WALL_COST more to enter while it stands.
 */
#define MAXW 4
static U8 *wallcell;
static U16 wall_mark[MAXW];
static int n_walls;
/* extra cost of passing a wall: about a dynamite program, 58 steps = 14 tiles */
#define WALL_COST 16
static void dijkstra(U16 *, int);
static void walls_build(void);
static int walls_present(void);
static int d_rows;

static U8
submap_eflg(int row, int col)
{
  U16 b = (U16)(map_submaps[g_submap].bnum + (row >> 2) * 8 + (col >> 2));
  return map_eflg[map_blocks[map_bnums[b]][(row & 3) * 4 + (col & 3)]];
}

/*
 * death map: where rick died during the search, per tile. Tiles that killed him
 * often enough are closed to the distance field on the next restart, so the
 * heuristic stops pointing into a trap (hl_solve).
 */
static U16 *deaths, *visits;  /* per tile: expansions that died there, that ended there */
static U8 *blocked;

/*
 * The field works on rick's footprint, not on single tiles: an anchor (row, col) =
 * (y >> 3, (x + 4) >> 3), where u_envtest starts probing, stands for FP_W columns
 * by FP_H rows. FP_W is 2, rick's width when aligned on a ladder (u_envtest's
 * aligned case reads 2 columns, else 3), so the field never threads a gap he
 * cannot pass, yet still fits the ladder shafts.
 */
#define FP_W 2
#define FP_H 3

static int
tile_free(int row, int col)
{
  if (row < 0 || row >= d_rows || col < 0 || col >= 0x20)
    return 0;
  return !(submap_eflg(row, col) & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_LETHAL));
}

/* rows row0..row0+FP_H-1 free, or when <crawl> only the lower two */
static int
fp_free(int row, int col, int crawl)
{
  int r, c;

  for (r = row + (crawl ? 1 : 0); r < row + FP_H; r++)
    for (c = col; c < col + FP_W; c++)
      if (!tile_free(r, c))
	return 0;
  return 1;
}

/*
 * standing room at anchor (row, col), or crawling room: crawling, u_envtest
 * probes from the row below the anchor (its `crawl` path), so rick fits under a
 * one tile lower ceiling. Crawl-only anchors are joined horizontally only
 * (move_cost).
 */
static int
passable(int row, int col)
{
  if (blocked && blocked[row * 0x20 + col])
    return 0;
  return fp_free(row, col, 0) || fp_free(row, col, 1);
}

/* some tile of the footprint at (row, col) has one of <flags> */
static int
fp_any(int row, int col, U8 flags)
{
  int r, c;

  for (r = row; r < row + FP_H; r++)
    for (c = col; c < col + FP_W; c++)
      if (r < d_rows && (submap_eflg(r, c) & flags))
	return 1;
  return 0;
}

/*
 * cost of a move of the anchor from (r0,c0) to the neighbour (r1,c1), 0 =
 * impossible. A one-way floor (WAYUP) can be crossed going up, never going down
 * unless it is also a ladder (the "TT" climb tops): rick only drops through it
 * there. Going down, the tiles that matter are the new bottom row's.
 */
static int
move_cost(int r0, int c0, int r1, int c1)
{
  int c;
  U8 f;

  if (r1 < r0 && (!fp_free(r0, c0, 0) || !fp_free(r1, c1, 0)))
    return 0;  /* no jumping or climbing up while crawling; falling is fine */
  if (r1 > r0)
    for (c = c1; c < c1 + FP_W; c++)
    {
      f = submap_eflg(r1 + FP_H - 1, c);
      if ((f & MAP_EFLG_WAYUP) && !(f & (MAP_EFLG_CLIMB|MAP_EFLG_VERT)))
	return 0;
    }
  if (r1 < r0 && !fp_any(r0, c0, MAP_EFLG_CLIMB|MAP_EFLG_VERT) &&
      !fp_any(r1, c1, MAP_EFLG_CLIMB|MAP_EFLG_VERT))
    return 3;  /* up with nothing to climb: a jump */
  return 1;
}

/* a small binary heap of (distance, cell) */
static int *hp_cell;
static U16 *hp_key;
static int hp_n;

static void
hp_push(U16 k, int cell)
{
  int i = hp_n++, p;

  while (i > 0 && hp_key[p = (i - 1) / 2] > k)
  {
    hp_key[i] = hp_key[p];
    hp_cell[i] = hp_cell[p];
    i = p;
  }
  hp_key[i] = k;
  hp_cell[i] = cell;
}

static int
hp_pop(void)
{
  int top = hp_cell[0], i = 0, c;
  U16 k = hp_key[--hp_n];
  int cell = hp_cell[hp_n];

  for (;;)
  {
    c = 2 * i + 1;
    if (c >= hp_n) break;
    if (c + 1 < hp_n && hp_key[c + 1] < hp_key[c]) c++;
    if (hp_key[c] >= k) break;
    hp_key[i] = hp_key[c];
    hp_cell[i] = hp_cell[c];
    i = c;
  }
  hp_key[i] = k;
  hp_cell[i] = cell;
  return top;
}

/*
 * Jumps. The field's states are (anchor, k), k = rows risen since rick last stood
 * on something or held a ladder. A jump starts at offsy -0x580 and gravity adds
 * 0x80 a step (e_rick.c), so rick rises for 11 steps, 33 px: 4.1 tiles. (3 made
 * submap 0x06 unreachable -- some ledges do need all of it.)
 * Without this the field sent him up open shafts he cannot jump (submap 0x06).
 */
#define JUMP_ROWS 4
#define NK (JUMP_ROWS + 1)

/* per subset of standing walls, per (anchor, k): [walls][anchor * NK + k] */
static U16 *fk[1 << MAXW];

/* rick at anchor (row, col) stands on something, or holds a ladder */
static int
grounded(int row, int col)
{
  int c;

  if (fp_any(row, col, MAP_EFLG_CLIMB|MAP_EFLG_VERT))
    return 1;
  for (c = col; c < col + FP_W; c++)
    if (row + FP_H >= d_rows ||
	(submap_eflg(row + FP_H, c) & (MAP_EFLG_SOLID|MAP_EFLG_SPAD|MAP_EFLG_WAYUP|
				       MAP_EFLG_CLIMB|MAP_EFLG_VERT)))
      return 1;
  return 0;
}

/* k after the forward move (ra,ca),ka -> (rb,cb); -1 if rick cannot make it */
static int
k_after(int ra, int ca, int ka, int rb, int cb)
{
  int k;

  if (rb < ra)
  {
    if (fp_any(ra, ca, MAP_EFLG_CLIMB|MAP_EFLG_VERT) ||
	fp_any(rb, cb, MAP_EFLG_CLIMB|MAP_EFLG_VERT))
      k = 0;              /* climbing */
    else if (ka >= JUMP_ROWS)
      return -1;          /* no higher without a ladder */
    else
      k = ka + 1;
  }
  else if (rb > ra)
    k = JUMP_ROWS;        /* falling: no rising again before landing */
  else
    k = ka;               /* drifting sideways */
  return grounded(rb, cb) ? 0 : k;
}

static void
field_build(void)
{
  U16 next = g_submap + 1 < MAP_NBR_SUBMAPS ?
    map_submaps[g_submap + 1].bnum : MAP_NBR_BNUMS;
  int n, r, c, k, j;
  U16 cn;

  /*
   * the submap's own block rows, plus one map_map height: map_expand reads on
   * past them (it takes 0x0b block rows from wherever frow points), and those
   * rows are played -- submap 0x01's exit is reached along rows 0x6c+, past its
   * 0x6c own rows. Bounded by the end of map_bnums.
   */
  d_rows = (int)(next - map_submaps[g_submap].bnum) / 8 * 4 + 0x28;
  if (d_rows > (MAP_NBR_BNUMS - map_submaps[g_submap].bnum) / 8 * 4)
    d_rows = (MAP_NBR_BNUMS - map_submaps[g_submap].bnum) / 8 * 4;
  n = d_rows * 0x20;
  free(dist); free(dist_wall);
  for (j = 0; j < (1 << MAXW); j++) { free(fk[j]); fk[j] = NULL; }
  free(hp_cell); free(hp_key);
  dist = malloc((size_t)n * sizeof(U16));
  dist_wall = malloc((size_t)n * sizeof(U16));
  hp_cell = malloc((size_t)n * NK * 4 * sizeof(int));
  hp_key = malloc((size_t)n * NK * 4 * sizeof(U16));
  walls_build();

  for (j = 0; j < (1 << n_walls); j++)
  {
    U16 *f = fk[j] = malloc((size_t)n * NK * sizeof(U16));
    for (k = 0; k < n * NK; k++) f[k] = DIST_INF;
    hp_n = 0;
    /* seeds: a waypoint's 3x3 anchors, any k (hl_solveWaypoint) ... */
    if (g_wp_row >= 0)
      for (r = g_wp_row - 1; r <= g_wp_row + 1; r++)
	for (c = g_wp_col - 1; c <= g_wp_col + 1; c++)
	  if (r >= 0 && r < d_rows && c >= 0 && c < 0x20)
	    for (k = 0; k < NK; k++)
	    {
	      f[(r * 0x20 + c) * NK + k] = 0;
	      hp_push(0, (r * 0x20 + c) * NK + k);
	    }
    /* ... else the target connectors' rows, at the edge they leave by, any k */
    for (cn = map_submaps[g_submap].connect; g_wp_row < 0 && map_connect[cn].dir != 0xff; cn++)
    {
      int to = map_connect[cn].submap == 0xff ? HL_SOLVE_NEXTMAP : map_connect[cn].submap;
      if (to != g_target) continue;
      for (r = map_connect[cn].rowout; r < map_connect[cn].rowout + 3 && r < d_rows; r++)
	for (c = 0; c < 2; c++)
	{
	  /* anchors at the edge: x ~ 0 -> col 0..1; x ~ 0xE6 -> col 0x1d..0x1e */
	  int cc = map_connect[cn].dir == LEFT ? c : 0x20 - FP_W - c;
	  for (k = 0; k < NK; k++)
	  {
	    f[(r * 0x20 + cc) * NK + k] = 0;
	    hp_push(0, (r * 0x20 + cc) * NK + k);
	  }
	}
    }
    dijkstra(f, j);
  }

  /* per anchor, the best over k: what the death map and -v prints look at */
  for (k = 0; k < n; k++)
  {
    U16 *all = fk[(1 << n_walls) - 1];
    dist[k] = dist_wall[k] = DIST_INF;
    for (j = 0; j < NK; j++)
    {
      if (fk[0][k * NK + j] < dist[k]) dist[k] = fk[0][k * NK + j];
      if (all[k * NK + j] < dist_wall[k]) dist_wall[k] = all[k * NK + j];
    }
  }
}

/*
 * Dijkstra backwards over (anchor, k) from the states pushed on the heap:
 * f(a,ka) = min over moves (a,ka) -> (b,kb) of cost + f(b,kb). With <walls>,
 * entering an anchor whose footprint touches a wall entity costs WALL_COST more
 * (see walls_build).
 */
static void
dijkstra(U16 *f, int walls)
{
  int s, cell, kb, r, c, k, ka, d, r1, c1, dr, dc, mc;

  while (hp_n)
  {
    s = hp_pop();
    cell = s / NK; kb = s % NK;
    r = cell / 0x20; c = cell % 0x20; d = f[s];
    for (k = 0; k < 4; k++)
    {
      dr = k == 0 ? -1 : k == 1 ? 1 : 0;
      dc = k == 2 ? -1 : k == 3 ? 1 : 0;
      r1 = r + dr; c1 = c + dc;
      if (r1 < 0 || r1 >= d_rows || c1 < 0 || c1 >= 0x20) continue;
      if (!passable(r1, c1)) continue;
      mc = move_cost(r1, c1, r, c);
      if (!mc) continue;
      {
	/* into a standing wall from outside it: once per wall, not per cell (the
	   stone head's tunnel crosses about five) */
	int in = (wallcell[cell] & walls) & ~wallcell[r1 * 0x20 + c1], b;
	for (b = 0; b < MAXW; b++)
	  if (in & (1 << b))
	    mc += WALL_COST;
      }
      for (ka = 0; ka < NK; ka++)
      {
	int sa = (r1 * 0x20 + c1) * NK + ka;
	if (k_after(r1, c1, ka, r, c) != kb) continue;
	if (d + mc < f[sa])
	{
	  f[sa] = (U16)(d + mc);
	  hp_push(f[sa], sa);
	}
      }
    }
  }
}


/*
 * wall entities (ENT_FLG_STOPRICK placements, e.g. submap 0x03's stone head,
 * 0x06's mark 58): the anchors whose footprint touches one. Placement position
 * as ents.c ent_actvis decodes it; size from ent_entdata.
 */
static void
walls_build(void)
{
  U16 m;
  int r0, c0, hr, wc, r, c;

  free(wallcell);
  wallcell = calloc((size_t)d_rows * 0x20, 1);
  n_walls = 0;
  for (m = map_submaps[g_submap].mark; map_marks[m].row != 0xff; m++)
  {
    if (!(map_marks[m].flags & ENT_FLG_STOPRICK) || n_walls == MAXW)
      continue;
    wall_mark[n_walls++] = m;
    r0 = (map_marks[m].row & 0xf8) + (map_marks[m].xy & 0x07);
    c0 = (map_marks[m].xy & 0xf8) >> 3;
    hr = (ent_entdata[map_marks[m].ent & 0x7f].h + 7) >> 3;
    wc = (ent_entdata[map_marks[m].ent & 0x7f].w + 7) >> 3;
    for (r = r0 - FP_H + 1; r < r0 + hr; r++)
      for (c = c0 - FP_W + 1; c < c0 + wc; c++)
	if (r >= 0 && r < d_rows && c >= 0 && c < 0x20)
	  wallcell[r * 0x20 + c] |= (U8)(1 << (n_walls - 1));
  }
}

/*
 * which walls still stand, as walls_build's bits? A wall placement in view
 * (spawned rows: frow .. frow + 0x27, ents.c ent_actvis) stands while its entity
 * is in slot 0. Out of view it stands: it respawns when scrolled back in -- an
 * ONCE trap that has run is despawned, not marked done, on the ST too
 * (scripted_trap_update PATH_END, kb/algo-entities.md). A wall off the way on,
 * e.g. submap 0x03's mark 30 once passed, then costs nothing: only its own bit's
 * cells are dearer.
 */
static int
walls_present(void)
{
  U16 m;
  int r0, w, bits = 0;

  for (w = 0; w < n_walls; w++)
  {
    m = wall_mark[w];
    if (map_marks[m].ent & MAP_MARK_NACT)
      continue;
    r0 = (map_marks[m].row & 0xf8) + (map_marks[m].xy & 0x07);
    /* in view it stands while in slot 0 AND at its spawn point: submap 0x07's
       mark 72 is not blown up but set sliding, and once moved it no longer
       closes the passage it stood in */
    if (r0 < map_frow || r0 >= map_frow + 0x28 ||
	(ent_ents[0].n && ent_ents[0].mark == m &&
	 ent_ents[0].x == (S16)ent_ents[0].xsave && ent_ents[0].y == (S16)ent_ents[0].ysave))
      bits |= 1 << w;
  }
  return bits;
}

/* the field, for -v -v -v: # no footprint, E exit, + reaches the exit, . does not */
static void
field_print(void)
{
  int r, c;

  static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

  for (r = 0; r < d_rows; r++)
  {
    fprintf(stderr, "field %3d ", r);
    for (c = 0; c < 0x20; c++)
      fputc(!passable(r, c) ? '#' : dist[r * 0x20 + c] == 0 ? 'E' :
	    dist[r * 0x20 + c] != DIST_INF ? '+' : '.', stderr);
    /* and the wall field grounded (k = 0), distance / 4 in base 36, W = wall */
    fputc(' ', stderr);
    for (c = 0; c < 0x20; c++)
    {
      U16 v = fk[(1 << n_walls) - 1][(r * 0x20 + c) * NK];
      fputc(!passable(r, c) ? '#' : wallcell[r * 0x20 + c] ? 'W' :
	    v == DIST_INF ? '.' : digits[v / 4 < 35 ? v / 4 : 35], stderr);
    }
    fputc('\n', stderr);
  }
}

/* the tile where the last failed search got stuck, -1 if none */
static int g_stuck_cell = -1;
/*
 * f credits for progress (see search). Per placement done, 8 = two tiles: 32
 * was worse on submap 0x03, enemies and pickups pulled the beam off the way on.
 * Per lethal entity made harmless, 32: submap 0x03's corridor traps (marks
 * 36/37) lose ENT_LETHAL when dynamite hits them but are never marked done.
 */
#define CREDIT 8
#define CREDIT_LETHAL 32
#define BOMB_CLEAR 32
/*
 * No value on bombs held. Tried 24 per bomb (6 tiles) when submap 0x07 spent its
 * only bomb before its exit wall: 0x03 (three bombs needed) and 0x06 then failed.
 * What fixed 0x07 was ranking bomb states by clearance (bomb_rank), not this.
 *
 * Bullets held: 2 each (half a tile). Since F11 (the ST re-arms the gun every
 * frame), FIRE+UP runs in a path fire for real; without a value the beam spent
 * 0x0D's last bullet on nothing and met enemy 155 unarmed. A kill (CREDIT 8)
 * still pays for its bullet; a crate from 1 to 6 is worth 10.
 */
#define BULLET_VALUE 2

/* this submap's placements done (killed, collected, triggered once) */
static int
marks_done(void)
{
  U16 m;
  int n = 0;

  for (m = map_submaps[g_submap].mark; map_marks[m].row != 0xff; m++)
    n += (map_marks[m].ent & MAP_MARK_NACT) != 0;
  return n;
}

/*
 * live entities defused: spawned lethal (ENT_FLG_LETHALR, which ent_actvis turns
 * into ENT_LETHAL) and lethal no more. Counted on the live state, not against
 * the start: such traps are usually spawned later, by scrolling.
 */
static int
defused(void)
{
  int i, n = 0;

  for (i = 0; i < ENT_ENTSNUM; i++)
    if (i != 1 && i != 2 && i != 3 && ent_ents[i].n &&
	(ent_ents[i].flags & ENT_FLG_LETHALR) && !(ent_ents[i].n & ENT_LETHAL))
      n++;
  return n;
}

/* rick's anchor tile (see FP_W); -1 off the submap */
static int
rick_cell(void)
{
  int r = (E_RICK_ENT.y >> 3) + map_frow, c = (E_RICK_ENT.x + 4) >> 3;

  if (c < 0) c = 0;
  if (c > 0x20 - FP_W) c = 0x20 - FP_W;
  if (r < 0 || r >= d_rows)
    return -1;
  return r * 0x20 + c;
}

/* rick's distance to the exit, in tiles */
static int
field_rick(void)
{
  int cell = rick_cell(), k;
  U16 *f = fk[walls_present()];

  if (cell < 0)
    return 1000;
  /* k: 0 on the ground or a ladder; in the air it is not known from the state
     here (offsy is e_rick.c's), so half a jump */
  k = grounded(cell / 0x20, cell % 0x20) ? 0 : JUMP_ROWS / 2;
  if (f[cell * NK + k] == DIST_INF)
    return 1000;
  return f[cell * NK + k];
}

/* ----------------------------------------------------------------------- */
/* the search                                                              */

/*
 * actions: small programs of (mask, steps) segments.
 * - every mask of act_mask held 2, 4 or 8 steps
 * - dynamite: stand (no control, 2 steps), drop it (FIRE+DOWN), run left or right
 *   12 or 24 steps, then stand until 36 steps in all -- just before the blast
 *   (ST e_bomb.h: 34 fuse ticks, then 20 of explosion, lethal for 7). The search
 *   takes over there: submap 0x07's wall slides along its row when blown and has
 *   to be jumped over right then (retreat 18-24, jump at 36-39: found by hand);
 *   an earlier version stood still to step 58 and was crushed. A beam that scores
 *   by distance would never keep the retreat on its own: it moves away from the
 *   exit. The first 2 steps let rick
 *   stand up: e_rick.c only fires when he was not crawling the step before
 *   (`if (scrawl || !FIRE) goto firing_not`), and FIRE+DOWN from a crawl just
 *   crawls on -- submap 0x03's stone head is reached crawling.
 */
static const U8 act_mask[] = {
  0, CONTROL_LEFT, CONTROL_RIGHT, CONTROL_UP, CONTROL_DOWN,
  CONTROL_LEFT|CONTROL_UP, CONTROL_RIGHT|CONTROL_UP,
  CONTROL_LEFT|CONTROL_DOWN, CONTROL_RIGHT|CONTROL_DOWN,
  CONTROL_FIRE|CONTROL_UP,  /* FIRE|DOWN (dynamite) only through the bomb programs */
  CONTROL_FIRE|CONTROL_LEFT, CONTROL_FIRE|CONTROL_RIGHT
};
static const U8 act_len[] = { 2, 4, 8 };
#define N_MASK ((int)sizeof(act_mask))
#define N_LEN ((int)sizeof(act_len))
#define BOMB_STEPS 36

typedef struct {
  U8 n;          /* segments */
  U8 mask[4];
  U8 len[4];
  U8 steps;      /* sum of len */
} prog_t;


#define N_ACT (N_MASK * N_LEN + 8)
/* (drop-and-escape programs, run up/down/sideways then hand back, were tried for
   submap 0x07: with them 0x06 and 0x07 failed, without them both solve) */
static prog_t progs[N_ACT];

static void
progs_init(void)
{
  int a = 0, m, l, d, r, e;
  static const U8 dirs[2] = { CONTROL_LEFT, CONTROL_RIGHT };
  static const U8 runs[2] = { 12, 24 };
  static const U8 ends[2] = { BOMB_STEPS, 58 };  /* before the blast, after it */

  for (l = 0; l < N_LEN; l++)
    for (m = 0; m < N_MASK; m++, a++)
    {
      progs[a].n = 1;
      progs[a].mask[0] = act_mask[m];
      progs[a].len[0] = act_len[l];
      progs[a].steps = act_len[l];
    }
  for (e = 0; e < 2; e++)  /* 0x07 needs the short one, 0x03 the long one */
  for (d = 0; d < 2; d++)
    for (r = 0; r < 2; r++, a++)
    {
      progs[a].n = 4;
      progs[a].mask[0] = 0;                         progs[a].len[0] = 2;
      progs[a].mask[1] = CONTROL_FIRE|CONTROL_DOWN; progs[a].len[1] = 2;
      progs[a].mask[2] = dirs[d];                   progs[a].len[2] = runs[r];
      progs[a].mask[3] = 0;
      progs[a].len[3] = (U8)(ends[e] - 4 - runs[r]);
      progs[a].steps = ends[e];
    }
}

/* the mask program <a> holds at its step k */
static U8
prog_mask(int a, int k)
{
  int s;

  for (s = 0; s < progs[a].n; s++)
  {
    if (k < progs[a].len[s])
      return progs[a].mask[s];
    k -= progs[a].len[s];
  }
  return progs[a].mask[progs[a].n - 1];
}

typedef struct {
  int parent;   /* node index, -1 for the root */
  U8 act;       /* program that led here, 0xff for the root */
  U8 len;       /* steps it took */
  U8 mask;      /* the last mask held, for counting control changes */
  U32 g;        /* steps from the start */
  U32 toggles;  /* control changes from the start */
  U32 f;
  U16 cell;     /* rick's tile, row * 0x20 + col, for beam diversity */
  U8 bomb;      /* a bomb in play (ticking or exploding): its own quota, see bucket_put */
  U8 bclear;    /* then: tiles between rick and the bomb, at most 8 */
} node_t;

/* at most this many states per rick tile in a beam, so it cannot collapse onto
   one spot (a local minimum of the tile distance) */
#define CELL_CAP 4

static node_t *nodes;
static int n_nodes, cap_nodes;

static int
node_new(int parent, U8 act, U8 len, U32 g, U32 toggles, U32 f)
{
  if (n_nodes == cap_nodes)
  {
    cap_nodes = cap_nodes ? cap_nodes * 2 : 1 << 16;
    nodes = realloc(nodes, (size_t)cap_nodes * sizeof(node_t));
  }
  nodes[n_nodes].parent = parent;
  nodes[n_nodes].act = act;
  nodes[n_nodes].len = len;
  nodes[n_nodes].mask = act == 0xff ? 0xff : prog_mask(act, len - 1);
  nodes[n_nodes].g = g;
  nodes[n_nodes].toggles = toggles;
  nodes[n_nodes].f = f;
  nodes[n_nodes].cell = 0;
  nodes[n_nodes].bomb = 0;
  nodes[n_nodes].bclear = 0;
  return n_nodes++;
}

/* visited states: open addressing over hl_stateKey */
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

/* write the path to <node>, plus the first <extra> steps of program <act>, into seq */
static int
path(int node, int act, int extra, U8 *seq, int max)
{
  int n = (int)nodes[node].g + extra, i, k;

  if (n > max)
    return -1;
  for (i = (int)nodes[node].g, k = 0; k < extra; k++)
    seq[i + k] = prog_mask(act, k);
  for (; node > 0; node = nodes[node].parent)
    for (k = 0; k < nodes[node].len; k++)
      seq[(int)nodes[nodes[node].parent].g + k] = prog_mask(nodes[node].act, k);
  return n;
}

/* control changes program <act> adds after a node holding <prev> */
static U32
prog_toggles(int act, U8 prev)
{
  U32 t = progs[act].mask[0] != prev;
  int s;

  for (s = 1; s < progs[act].n; s++)
    t += progs[act].mask[s] != progs[act].mask[s - 1];
  return t;
}

/*
 * one time-synchronous beam search from <start>.
 *
 * States wait in buckets by g, their step count, and buckets are taken in order
 * of g. Within a bucket every state has spent the same time, so they are ranked
 * by f = 4 * tile distance + 2 * control changes - credits alone, the <beam> best
 * kept (at most CELL_CAP per rick tile), and each is expanded into the buckets
 * g + program length. Standing still or a 56-step dynamite program then competes
 * with other states of the same age -- in a plain beam they always lost to
 * whatever had spent fewer steps.
 *
 * A bucket holds at most BUCKET_CAP * beam states, the worst f evicted first.
 * Returns the solution length, or -1 when every bucket is empty, the step budget
 * runs out, or the best tile distance has not improved for STUCK_STEPS steps.
 * Deaths are counted per tile as it goes.
 */
#define STUCK_STEPS 400
#define BUCKET_CAP 4

typedef struct {
  int n, cap;
  int *node;
  U8 **snap;
} bucket_t;

/*
 * rank within a class: f, and for bomb-in-play states f - BOMB_CLEAR per tile
 * clear of the bomb (compared only with each other, see search)
 */
static long
bomb_rank(int node)
{
  return (long)nodes[node].f - (nodes[node].bomb ? (long)BOMB_CLEAR * nodes[node].bclear : 0L);
}

static void
bucket_put(bucket_t *b, int node, size_t sz)
{
  int i, w;

  int nb, cls;

  if (b->n < b->cap)
  {
    b->node[b->n] = node;
    b->snap[b->n] = malloc(sz);
    hl_stateSave(b->snap[b->n]);
    b->n++;
    return;
  }
  /*
   * full: evict the worst f -- of the newcomer's class while that class holds
   * its quota (bomb-in-play states: a quarter), else of the other class. A bomb
   * state is usually far from the exit (it ran from the blast) and would lose to
   * every state idling at the wall; submap 0x07 needs one to live on to jump.
   */
  for (nb = 0, i = 0; i < b->n; i++)
    nb += nodes[b->node[i]].bomb;
  cls = nodes[node].bomb ? (nb >= b->cap / 4) : (b->n - nb >= b->cap - b->cap / 4);
  cls = cls ? nodes[node].bomb : !nodes[node].bomb;  /* the class to evict from */
  for (w = -1, i = 0; i < b->n; i++)
    if (nodes[b->node[i]].bomb == cls &&
	(w < 0 || bomb_rank(b->node[i]) > bomb_rank(b->node[w])))
      w = i;
  if (w < 0)
    return;
  if (nodes[b->node[w]].bomb == nodes[node].bomb && bomb_rank(node) >= bomb_rank(b->node[w]))
    return;
  b->node[w] = node;
  hl_stateSave(b->snap[w]);
}

static int
search(const hl_solveopt_t *o, const U8 *start, U8 *seq, int max, int attempt)
{
  size_t sz = hl_stateSize();
  int nb = o->maxsteps + 58 + 1;  /* + the longest program */
  bucket_t *bk = calloc((size_t)nb, sizeof(bucket_t));
  int *order = malloc((size_t)o->beam * BUCKET_CAP * sizeof(int));
  int *keep = malloc((size_t)o->beam * BUCKET_CAP * sizeof(int));
  U8 *cellcount = malloc((size_t)d_rows * 0x20);
  int g, i, k, a, j, r = R_RUN, found = -1, h, cell, n_keep, root;
  int n_fail = 0, n_dup = 0, best_h = 1 << 30, best_g = 0, bh;
  long f;

  for (g = 0; g < nb; g++)
  {
    bk[g].cap = o->beam * BUCKET_CAP;
    bk[g].node = malloc((size_t)bk[g].cap * sizeof(int));
    bk[g].snap = malloc((size_t)bk[g].cap * sizeof(U8 *));
  }
  seen = calloc((size_t)1 << SEEN_BITS, sizeof(unsigned long long));
  n_nodes = 0;
  root = node_new(-1, 0xff, 0, 0, 0, 0);
  g_stuck_cell = -1;
  hl_stateLoad(start);
  seen_add(hl_stateKey());
  bucket_put(&bk[0], root, sz);

  for (g = 0; g <= o->maxsteps && found < 0; g++)
  {
    bucket_t *b = &bk[g];
    if (!b->n)
      continue;

    /* the <beam> best of this bucket, at most CELL_CAP per rick tile */
    for (i = 0; i < b->n; i++) order[i] = i;
    for (i = 1; i < b->n; i++)  /* insertion sort on f: buckets are small */
    {
      int x = order[i];
      for (k = i; k > 0 && nodes[b->node[order[k - 1]]].f > nodes[b->node[x]].f; k--)
	order[k] = order[k - 1];
      order[k] = x;
    }
    memset(cellcount, 0, (size_t)d_rows * 0x20);
    /* first up to a quarter of the beam for bomb-in-play states (see bucket_put),
       then the rest by f */
    for (n_keep = 0; n_keep < o->beam / 4; )
    {
      /* the best bomb_rank not taken yet, cell cap respected */
      int best = -1, c;
      for (i = 0; i < b->n; i++)
      {
	int nd = b->node[i];
	if (nodes[nd].bomb != 1 || cellcount[nodes[nd].cell] >= CELL_CAP) continue;
	if (best < 0 || bomb_rank(nd) < bomb_rank(b->node[best]))
	  best = i;
      }
      if (best < 0) break;
      c = nodes[b->node[best]].cell;
      cellcount[c]++;
      keep[n_keep++] = best;
      nodes[b->node[best]].bomb = 2;  /* taken */
    }
    for (i = 0; i < b->n && n_keep < o->beam; i++)
    {
      int c = nodes[b->node[order[i]]].cell;
      if (nodes[b->node[order[i]]].bomb == 2 || cellcount[c] >= CELL_CAP) continue;
      cellcount[c]++;
      keep[n_keep++] = order[i];
    }

    bh = 1 << 30;
    for (i = 0; i < n_keep && found < 0; i++)
    {
      int par = b->node[keep[i]];
      U8 *ps = b->snap[keep[i]];

      for (a = 0; a < N_ACT && found < 0; a++)
      {
	U8 len = progs[a].steps;

	hl_stateLoad(ps);
	if (progs[a].n > 1 && (E_BOMB_ENT.n || !env_bombs))
	  continue;  /* a bomb program with one ticking, or none left: a no-op run */
	g_died = 0;
	for (j = 1; j <= len; j++)
	{
	  r = judge(game_hlStep(prog_mask(a, j - 1)));
	  if (r != R_RUN) break;
	}
	if (r == R_GOAL)
	{
	  found = path(par, a, j, seq, max);
	  break;
	}
	if (r == R_FAIL)
	{
	  n_fail++;
	  if (g_died && (cell = rick_cell()) >= 0 && deaths[cell] < 0xffff)
	    deaths[cell]++;
	  continue;
	}
	if (g + len >= nb) continue;
	if (!seen_add(hl_stateKey())) { n_dup++; continue; }
	h = field_rick();
	if (h < bh) bh = h;
	k = node_new(par, (U8)a, len, (U32)(g + len),
		     nodes[par].toggles + prog_toggles(a, nodes[par].mask), 0);
	/* progress events -- a placement done, a trap defused -- earn credits:
	   what dynamite buys is otherwise invisible to the distance */
	f = 4L * h + 2L * (long)nodes[k].toggles - (long)CREDIT * marks_done() -
	  (long)CREDIT_LETHAL * defused() - (long)BULLET_VALUE * env_bullets + 0x10000L;
	/*
	 * a bomb in play: how clear of it rick is, for ranking bomb states AMONG
	 * THEMSELVES (bomb_rank). Submap 0x07: the states that stayed by the wall
	 * filled the bomb quota and all died in the blast; the one that ran 24 steps,
	 * and jumps the sliding wall, ranked below them. Folded into f instead, it
	 * made any bomb dropped anywhere look good, and the one bomb was spent early.
	 */
	if (E_BOMB_ENT.n)
	{
	  int dx = (E_RICK_ENT.x - E_BOMB_ENT.x) / 8, dy = (E_RICK_ENT.y - E_BOMB_ENT.y) / 8;
	  int t = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
	  nodes[k].bclear = (U8)(t > 8 ? 8 : t);
	}
	nodes[k].f = (U32)(f < 0 ? 0 : f);
	cell = rick_cell();
	nodes[k].cell = (U16)(cell < 0 ? 0 : cell);
	nodes[k].bomb = E_BOMB_ENT.n != 0;
	if (visits[nodes[k].cell] < 0xffff)
	  visits[nodes[k].cell]++;
	bucket_put(&bk[g + len], k, sz);
      }
    }

    if (bh < best_h)
    {
      best_h = bh;
      best_g = g;
    }
    if (o->verbose > 1 || (o->verbose && g % 100 == 0))
    {
      int nbk = 0;
      for (i = 0; i < n_keep; i++)
      {
	nbk += nodes[b->node[keep[i]]].bomb != 0;
	if (o->verbose > 2 && nodes[b->node[keep[i]]].bomb)
	{
	  hl_stateLoad(b->snap[keep[i]]);
	  fprintf(stderr, "  bomb state: x %d row %d f %lu act %d\n", (int)E_RICK_ENT.x,
		  (E_RICK_ENT.y >> 3) + map_frow, (unsigned long)nodes[b->node[keep[i]]].f,
		  nodes[b->node[keep[i]]].act);
	}
      }
      hl_stateLoad(b->snap[order[0]]);
      fprintf(stderr, "solve: attempt %d step %d, %d states (%d kept, %d with a bomb), "
	      "best dist %d, rick x %d row %d\n", attempt, g, b->n, n_keep, nbk, best_h,
	      (int)E_RICK_ENT.x, (E_RICK_ENT.y >> 3) + map_frow);
    }
    if (found < 0 && g - best_g > STUCK_STEPS)
    {
      if (o->verbose)
	fprintf(stderr, "solve: attempt %d step %d: stuck at distance %d "
		"(%d died or left, %d seen before)\n", attempt, g, best_h, n_fail, n_dup);
      /* the best state of this bucket: where the search got stuck */
      hl_stateLoad(b->snap[order[0]]);
      g_stuck_cell = rick_cell();
      if (o->verbose > 2)
	hl_dump(stderr);
      if (o->stuck)
      {
	FILE *fs = fopen(o->stuck, "wb");  /* for xrick-core -load */
	if (fs)
	{
	  fwrite(b->snap[order[0]], 1, sz, fs);
	  fclose(fs);
	}
      }
      break;
    }
  }
  if (found < 0 && g > o->maxsteps && o->verbose)
    fprintf(stderr, "solve: attempt %d: step budget spent, best distance %d\n",
	    attempt, best_h);

  for (g = 0; g < nb; g++)
  {
    for (i = 0; i < bk[g].n; i++)
      free(bk[g].snap[i]);
    free(bk[g].node);
    free(bk[g].snap);
  }
  free(bk); free(order); free(keep); free(cellcount);
  free(seen); seen = NULL;
  return found;
}


/*
 * hl_solve
 *
 * search; when a search fails, close the tiles where rick died DEATH_BLOCK times
 * or more and in at least half the expansions that ended there, rebuild the tile
 * distance and search again from scratch, at most
 * MAX_ATTEMPTS times. Stops early when an attempt closes no new tile.
 */
#define DEATH_BLOCK 32
#define MAX_ATTEMPTS 8

int
hl_solve(const hl_solveopt_t *o, U8 *seq, int max)
{
  size_t sz = hl_stateSize();
  U8 *start = malloc(sz);
  int found = -1, attempt, k, n, closed;

  hl_stateSave(start);
  goal_set(o->target);
  progs_init();
  blocked = NULL;
  field_build();
  if (o->verbose > 2)
    field_print();
  n = d_rows * 0x20;
  deaths = calloc((size_t)n, sizeof(U16));
  visits = calloc((size_t)n, sizeof(U16));
  blocked = calloc((size_t)n, 1);
  if (g_n_forbid)
  {
    /* hl_solveForbid: closed from the start, as a death closure would */
    int q, r, c;
    for (q = 0; q < g_n_forbid; q++)
      for (r = g_forbid[q][0]; r <= g_forbid[q][2]; r++)
	for (c = g_forbid[q][1]; c <= g_forbid[q][3]; c++)
	  if (r >= 0 && r < d_rows && c >= 0 && c < 0x20)
	    blocked[r * 0x20 + c] = 1;
    field_build();
  }

  for (attempt = 0; attempt < (o->closures ? MAX_ATTEMPTS : 1); attempt++)
  {
    found = search(o, start, seq, max, attempt);
    if (found >= 0)
      break;
    /*
     * escalation: after the first failure only the dead end is closed (below);
     * from the second on, deadly tiles too. Deadly = a trap kills nearly everyone
     * who gets there (>= 80% of the expansions that end on the tile); an enemy's
     * beat only some -- submap 0x01's bottom corridor was closed at 50% and that
     * cut the way on. 0x01 needs these closures (its trap shaft); 0x06 fails if
     * they come first (111 tiles closed, the start cut off).
     */
    for (k = 0, closed = 0; k < n; k++)
      if (attempt >= 1 && !blocked[k] && deaths[k] >= DEATH_BLOCK && deaths[k] >= 4 * visits[k] &&
	  dist[k] != 0)
      {
	blocked[k] = 2;  /* 2 = closed by this attempt */
	closed++;
      }
    /*
     * and the dead end itself: the anchors around the tile where the search got
     * stuck (submap 0x06: a ledge 4 rows up the field counts on but rick never
     * lands) -- the next attempt has to find another way
     */
    if (g_stuck_cell >= 0)
    {
      int sr = g_stuck_cell / 0x20, sc = g_stuck_cell % 0x20, r, c;
      for (r = sr - 1; r <= sr + 1; r++)
	for (c = sc - 1; c <= sc + 1; c++)
	  if (r >= 0 && r < d_rows && c >= 0 && c < 0x20 &&
	      !blocked[r * 0x20 + c] && dist[r * 0x20 + c] != 0)
	  {
	    blocked[r * 0x20 + c] = 2;
	    closed++;
	  }
    }
    if (closed)
    {
      /*
       * cut off = no tile the search visited reaches the exit any more. (Testing
       * the stuck tile instead undid submap 0x01's closure: that tile sits in the
       * very trap being closed.)
       */
      int reach = 0;
      field_build();
      for (k = 0; k < n && !reach; k++)
	reach = visits[k] && !blocked[k] && dist[k] != DIST_INF;
      if (o->verbose > 1)
      {
	/* # wall/no footprint, X closed, E exit, + visited and reaching, v visited
	   cut off, . open */
	int r, c;
	for (r = 0; r < d_rows; r++)
	{
	  fprintf(stderr, "solve: %3d ", r);
	  for (c = 0; c < 0x20; c++)
	  {
	    k = r * 0x20 + c;
	    fputc(blocked[k] ? 'X' : !passable(r, c) ? '#' : dist[k] == 0 ? 'E' :
		  visits[k] ? (dist[k] != DIST_INF ? '+' : 'v') : '.', stderr);
	  }
	  fputc('\n', stderr);
	}
      }
      if (!reach)
      {
	/* that cut rick off from the exit: undo it and give up */
	for (k = 0; k < n; k++)
	  if (blocked[k] == 2) blocked[k] = 0;
	field_build();
	closed = -closed;
      }
      else
	for (k = 0; k < n; k++)
	  if (blocked[k] == 2) blocked[k] = 1;
    }
    if (o->verbose)
      fprintf(stderr, "solve: attempt %d failed, %d tile(s) closed for deaths%s\n",
	      attempt, closed < 0 ? -closed : closed,
	      closed < 0 ? " -- would cut rick off, undone" : "");
    if (closed <= 0)
      break;
  }

  hl_stateLoad(start);
  free(start);
  free(deaths); deaths = NULL;
  free(visits); visits = NULL;
  free(blocked); blocked = NULL;
  return found;
}

/* ----------------------------------------------------------------------- */
/* replay and polish                                                       */

int
hl_solveReplay(const U8 *seq, int n, int target)
{
  int i, r;

  goal_set(target);
  for (i = 0; i < n; i++)
  {
    r = judge(game_hlStep(seq[i]));
    if (r == R_GOAL) return i + 1;
    if (r == R_FAIL) return -1;
  }
  return -1;
}

static int
toggles(const U8 *seq, int n)
{
  int i, t = 0;

  for (i = 1; i < n; i++)
    t += seq[i] != seq[i - 1];
  return t;
}

/*
 * make the play look less like a machine's (T43 D3): try to give each run of one
 * mask, shortest first, its neighbour's mask; keep a change when the replay still
 * reaches the goal in no more steps and with fewer control changes.
 */
int
hl_solvePolish(U8 *seq, int n, int target)
{
  size_t sz = hl_stateSize();
  U8 *start = malloc(sz), *try = malloc((size_t)n);
  int improved = 1, i, j, k, len, best_len, side, m;
  int starts[4096], lens[4096], runs;

  hl_stateSave(start);
  while (improved)
  {
    improved = 0;
    /* runs */
    for (runs = 0, i = 0; i < n && runs < 4096; i = j)
    {
      for (j = i; j < n && seq[j] == seq[i]; j++) ;
      starts[runs] = i; lens[runs] = j - i; runs++;
    }
    /* shortest first */
    for (len = 1; len <= n && !improved; len++)
      for (k = 0; k < runs && !improved; k++)
      {
	if (lens[k] != len) continue;
	for (side = 0; side < 3 && !improved; side++)
	{
	  if (side == 0 && k == 0) continue;
	  if (side == 1 && k == runs - 1) continue;
	  m = side == 0 ? seq[starts[k - 1]] : side == 1 ? seq[starts[k + 1]] : 0;
	  if (m == seq[starts[k]]) continue;
	  memcpy(try, seq, (size_t)n);
	  memset(try + starts[k], m, (size_t)lens[k]);
	  hl_stateLoad(start);
	  best_len = hl_solveReplay(try, n, target);
	  if (best_len > 0 && best_len <= n &&
	      toggles(try, best_len) < toggles(seq, n))
	  {
	    memcpy(seq, try, (size_t)best_len);
	    n = best_len;
	    improved = 1;
	  }
	}
      }
  }
  hl_stateLoad(start);
  free(start); free(try);
  return n;
}


/*
 * rick's tile distance to <target>'s exit in the current state, the fields
 * built for it; *walls: whether a wall entity still stands (the wall field is
 * the one used). For -distance and the MCP observation.
 */
int
hl_solveDistance(int target, int *walls)
{
  goal_set(target);
  blocked = NULL;
  field_build();
  if (walls)
    *walls = walls_present();
  return field_rick();
}


/*
 * reaching the target submap counts only while rick still holds <n> bombs (a
 * map exit refills them to 6, game.c NEXT_SUBMAP, so it is not checked there).
 * The chain sets it when the next submap failed without dynamite (T43 phase 9:
 * "arrive with >= k bombs"). Applies to hl_solve, hl_solvePolish, hl_solveReplay.
 */
void
hl_solveMinBombs(int n)
{
  g_minbombs = n;
}


/*
 * hints from outside the search (the MCP server, an LLM reading the state):
 * a waypoint -- solve to reach anchor (row, col), submap rows and columns as
 * -dump / -distance give them, instead of the exit; -1 clears it -- and anchor
 * rectangles closed from the start (at most FORBID_MAX; clear with r0 < 0).
 */
void
hl_solveWaypoint(int row, int col)
{
  g_wp_row = row;
  g_wp_col = col;
}

void
hl_solveForbid(int r0, int c0, int r1, int c1)
{
  if (r0 < 0)
  {
    g_n_forbid = 0;
    return;
  }
  if (g_n_forbid == FORBID_MAX)
    return;
  g_forbid[g_n_forbid][0] = r0;
  g_forbid[g_n_forbid][1] = c0;
  g_forbid[g_n_forbid][2] = r1;
  g_forbid[g_n_forbid][3] = c1;
  g_n_forbid++;
}

/* eof */
