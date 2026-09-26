/*
 * xrick/src/headless/xrick_core.c
 *
 * xrick-core only (branch `solver`, never shipped): the RD1 game logic with no
 * video, sound or timing, one logic step at a time (PLAN.md T43,
 * kb/demo-solver.md). Built by `make core`.
 *
 *   xrick-core [-submap <n>] [-demo] [-trace <file>] [-steps <n>] [-scramble <n>]
 *   xrick-core -fuzz <rounds> [-submap <n>] [-seed <n>] [-log <file>]
 *   xrick-core [-submap <n>] -inputs <file>
 *   xrick-core -list
 *
 * -submap    start at submap <n>, 1..47, as `xrick -submap` (default: a new game).
 * -demo      play the built-in scripts (src/rd1/dat_demo.c); stops when the demo
 *            hands control back. without it, no control is held.
 * -trace     same trace as `xrick -trace`, for diffing against the SDL build.
 * -steps     stop after <n> logic steps (default 100000).
 * -scramble  step the random generator <n> times before the game starts. With
 *            -demo, every segment entry reseeds it (T43 D1), so the trace must
 *            not change.
 * -fuzz      snapshot/restore check (T43 phase 4), see fuzz() below. every submap,
 *            or only -submap's.
 * -log       with -fuzz: before each round, write the inputs a plain game would
 *            play to reach the end of that round (one CONTROL_* byte per step).
 * -inputs    play <file>, one CONTROL_* byte per step (e.g. a -log file).
 * -record    as xrick -record: write the controls played as src/rd1/dat_demo.c
 *            (with -reseed -inputs <solution>: the solver's demo, T43 phase 10).
 * -dump      at the end, print the game state as JSON on stdout (T43 phase 5,
 *            hl_dump.c) instead of the summary line.
 * -list      print the snapshot regions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "system.h"
#include "sysarg.h"
#include "control.h"
#include "game.h"
#include "env.h"
#include "maps.h"
#include "ents.h"
#include "e_them.h"
#include "demo.h"
#include "hl_state.h"
#include "hl_dump.h"
#include "hl_solve.h"

static void
usage(void)
{
  fprintf(stderr,
	  "usage: xrick-core [-submap <n>] [-demo] [-trace <file>] [-steps <n>] [-scramble <n>]\n"
	  "       xrick-core -fuzz <rounds> [-submap <n>] [-seed <n>] [-log <file>]\n"
	  "       xrick-core [-submap <n>] -inputs <file>\n"
	  "       any run above but -fuzz: add -dump for the final state as JSON\n"
	  "       xrick-core -list\n");
  exit(2);
}

/*
 * -submap <n>: the same mapping as sysarg.c
 */
static void
set_submap(int n)
{
  sysarg_args_submap = n - 1;
  if (sysarg_args_submap < 0 || sysarg_args_submap >= MAP_NBR_SUBMAPS)
    usage();
  sysarg_args_map = 0;
  if (sysarg_args_submap > 0 && sysarg_args_submap < 9)
    sysarg_args_map = 0;
  if (sysarg_args_submap >= 9 && sysarg_args_submap < 20)
    sysarg_args_map = 1;
  if (sysarg_args_submap >= 20 && sysarg_args_submap < 38)
    sysarg_args_map = 2;
  if (sysarg_args_submap >= 38)
    sysarg_args_map = 3;
  if (sysarg_args_submap == 9 ||
      sysarg_args_submap == 20 ||
      sysarg_args_submap == 38)
    sysarg_args_submap = 0;
}

/*
 * fuzz: random inputs, xorshift32
 */
static U32 rnd_s = 0x2545F491u;

static U32
rnd(void)
{
  rnd_s ^= rnd_s << 13;
  rnd_s ^= rnd_s >> 17;
  rnd_s ^= rnd_s << 5;
  return rnd_s;
}

/* a random control mask, held for 1 to 16 steps (see fuzz_run) */
static U8
rnd_ctrl(void)
{
  static const U8 c[] = {
    0, CONTROL_LEFT, CONTROL_RIGHT, CONTROL_UP, CONTROL_DOWN, CONTROL_FIRE,
    CONTROL_LEFT|CONTROL_UP, CONTROL_RIGHT|CONTROL_UP,
    CONTROL_LEFT|CONTROL_FIRE, CONTROL_RIGHT|CONTROL_FIRE,
    CONTROL_UP|CONTROL_FIRE, CONTROL_DOWN|CONTROL_FIRE
  };
  return c[rnd() % (sizeof(c) / sizeof(c[0]))];
}

#define FUZZ_LEN 64

static void
fuzz_inputs(U8 *in, int n)
{
  int i = 0, k;
  U8 c;

  while (i < n)
  {
    c = rnd_ctrl();
    for (k = (int)(rnd() % 16) + 1; k > 0 && i < n; k--)
      in[i++] = c;
  }
}

/*
 * play <in>, the hash after each step into <h> and, when <s> is not NULL, the
 * snapshot after each step into <s> (hl_stateSize bytes each). returns the last
 * game_hlStep result.
 */
static U8
fuzz_run(const U8 *in, unsigned long long *h, U8 *s, int n)
{
  int i;
  U8 r = GAME_HL_STEP;

  for (i = 0; i < n; i++)
  {
    r = game_hlStep(in[i]);
    h[i] = hl_stateHash();
    if (s)
      hl_stateSave(s + (size_t)i * hl_stateSize());
  }
  return r;
}

static double
now(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/*
 * fuzz
 *
 * per round, at the current step boundary: snapshot S and hash H0; play random
 * inputs I (FUZZ_LEN steps) hashing every step; play other random inputs J
 * (FUZZ_LEN steps) so the state goes elsewhere -- deaths and game overs
 * included; restore S, check the hash is H0 again, replay I and check every
 * hash matches the first pass. then carry on from the end of I. a game over
 * starts a new game at the same submap.
 */
static const char *fuzz_log = NULL;  /* -log: timeline to replay with -inputs */
#define FUZZ_NEWGAME 0xFF             /* in a -log file: game_hlStart again */
static int fuzz_verbose = 0;          /* -v: name each submap on stderr */
static U8 tl[1 << 20];                /* the inputs played since the game started */
static size_t tl_n;

static int minbombs;  /* -minbombs */

static int
fuzz(int rounds, int only)
{
  static U8 in[FUZZ_LEN], other[FUZZ_LEN];
  static unsigned long long h1[FUZZ_LEN], h2[FUZZ_LEN];
  U8 *snap = malloc(hl_stateSize());
  U8 *s1 = malloc(FUZZ_LEN * hl_stateSize()), *s2 = malloc(FUZZ_LEN * hl_stateSize());
  unsigned long long h0;
  unsigned long steps = 0, restores = 0, bad = 0, overs = 0;
  double t0, tsave = 0, tload = 0, t;
  int sm, sm0, sm1, r, i, j;
  U16 seen[MAP_NBR_SUBMAPS];

  memset(seen, 0, sizeof(seen));
  sm0 = only ? only : 1;
  sm1 = only ? only : MAP_NBR_SUBMAPS;
  t0 = now();
  for (sm = sm0; sm <= sm1; sm++)
  {
    set_submap(sm);
    tl_n = 0;
    if (fuzz_verbose)
      fprintf(stderr, "fuzz: submap %d\n", sm);
    game_hlStart();
    for (r = 0; r < rounds; r++)
    {
      t = now(); hl_stateSave(snap); tsave += now() - t;
      h0 = hl_stateHash();
      fuzz_inputs(in, FUZZ_LEN);
      fuzz_inputs(other, FUZZ_LEN);
      if (fuzz_log)
      {
	/* the timeline so far, as a plain game would play it: I_0 .. I_r, J_r */
	FILE *f = fopen(fuzz_log, "wb");
	if (f)
	{
	  fwrite(tl, 1, tl_n, f);
	  fwrite(in, 1, FUZZ_LEN, f);
	  fwrite(other, 1, FUZZ_LEN, f);
	  fclose(f);
	}
      }
      fuzz_run(in, h1, s1, FUZZ_LEN);
      fuzz_run(other, h2, NULL, FUZZ_LEN);
      t = now(); hl_stateLoad(snap); tload += now() - t;
      restores++;
      if (hl_stateHash() != h0)
      {
	printf("MISMATCH submap %d round %d: hash after restore\n", sm, r);
	bad++;
      }
      i = fuzz_run(in, h2, s2, FUZZ_LEN);
      steps += 3 * FUZZ_LEN;
      for (j = 0; j < FUZZ_LEN; j++)
	if (h1[j] != h2[j])
	{
	  printf("MISMATCH submap %d round %d: step %d of the replay\n", sm, r, j);
	  hl_stateDiff(s1 + (size_t)j * hl_stateSize(), s2 + (size_t)j * hl_stateSize());
	  bad++;
	  break;
	}
      seen[env_submap < MAP_NBR_SUBMAPS ? env_submap : 0]++;
      if (tl_n + FUZZ_LEN <= sizeof(tl))
      {
	memcpy(tl + tl_n, in, FUZZ_LEN);
	tl_n += FUZZ_LEN;
      }
      if (i != GAME_HL_STEP)
      {
	if (tl_n < sizeof(tl))
	  tl[tl_n++] = FUZZ_NEWGAME;  /* game_hlStart again, same process */
	overs++;
	set_submap(sm);
	game_hlStart();
      }
    }
  }
  t = now() - t0;
  printf("fuzz: %lu restores, %lu steps, %lu game overs, %lu mismatches\n",
	 restores, steps, overs, bad);
  printf("fuzz: submaps reached:");
  for (i = 0; i < MAP_NBR_SUBMAPS; i++)
    if (seen[i])
      printf(" %02x", i);
  printf("\n");
  printf("fuzz: %.3f s, %.0f steps/s incl. a hash per step; snapshot %lu bytes, "
	 "save %.2f us, restore %.2f us\n",
	 t, (double)steps / t, (unsigned long)hl_stateSize(),
	 tsave / (double)restores * 1e6, tload / (double)restores * 1e6);
  free(snap); free(s1); free(s2);
  return bad ? 1 : 0;
}

/*
 * T43 phase 6: from a new game (or -load), solve <chain> submaps in a row with
 * the generator reseeded at each entry, as a demo plays them. Per submap: search,
 * polish, replay -- the replay moves the game on to the next submap's tick 0.
 * -out writes every step's control mask, a file `xrick-core -reseed -inputs`
 * replays. See solve() below.
 */
#define SOLVE_MAX 0x4000

/* read / write a snapshot file (same build only) */
static int
snap_file(const char *path, int write)
{
  U8 *snap = malloc(hl_stateSize());
  FILE *f = fopen(path, write ? "wb" : "rb");
  int ok;

  if (write) hl_stateSave(snap);
  ok = f && (write ? fwrite(snap, 1, hl_stateSize(), f) :
	     fread(snap, 1, hl_stateSize(), f)) == hl_stateSize();
  if (f) fclose(f);
  if (ok && !write) hl_stateLoad(snap);
  free(snap);
  if (!ok)
    fprintf(stderr, "xrick-core: cannot %s '%s'\n", write ? "write" : "read", path);
  return ok;
}


/*
 * solve
 *
 * submaps in a row, each started from the state the previous one's solution
 * left (T43 D6). When submap s cannot be solved, the previous one is solved
 * again keeping one more bomb at its exit (hl_solveMinBombs), up to 6, and the
 * chain goes on from there -- T43 phase 9's "arrive with >= k bombs". Submap
 * 0x06 needs dynamite; a chain that spent every bomb on 0x03 reached it with none.
 */
#define SEG_MAX 64
#define BACKTRACK_MAX 12

static int
solve(int chain, hl_solveopt_t *o, const char *out, const char *load, const char *save)
{
  static U8 *seg_snap[SEG_MAX + 1], *seg_seq[SEG_MAX];
  static int seg_n[SEG_MAX], seg_need[SEG_MAX + 1];  /* zero: static */
  size_t sz = hl_stateSize(), n_all = 0;
  int s = 0, n, n2, r, target, runs, jitter, i, k, backtracks = 0;
  U16 sm;
  double t0, t1;
  FILE *f;

  if (chain > SEG_MAX)
    chain = SEG_MAX;
  game_hlReseed(TRUE);
  game_hlStart();
  game_hlSettle();
  /* -load: start from a submap's tick 0, e.g. what an earlier -save wrote */
  if (load && !snap_file(load, 0))
    return 2;
  seg_snap[0] = malloc(sz);
  hl_stateSave(seg_snap[0]);
  seg_need[0] = minbombs;  /* -minbombs: what the first submap must keep */

  while (s < chain)
  {
    hl_stateLoad(seg_snap[s]);
    hl_solveMinBombs(seg_need[s]);
    sm = env_submap;
    target = o->target == HL_SOLVE_AUTO ? hl_solveTarget() : o->target;
    seg_seq[s] = seg_seq[s] ? seg_seq[s] : malloc(SOLVE_MAX);
    t0 = now();
    n = hl_solve(o, seg_seq[s], SOLVE_MAX);
    t1 = now();
    if (n < 0)
    {
      printf("solve: submap %#04x -> %d: NOT FOUND (keeping %d bomb(s), beam %d, %.1f s)\n",
	     (unsigned int)sm, target, seg_need[s], o->beam, t1 - t0);
      /* the previous submap leaves one more bomb, and on from there */
      if (s == 0 || backtracks == BACKTRACK_MAX || seg_need[s - 1] >= 6)
	break;
      backtracks++;
      s--;
      if (seg_need[s] < seg_need[s + 1])  /* what s+1 must keep, s must keep too */
	seg_need[s] = seg_need[s + 1];
      seg_need[s]++;
      continue;
    }
    n2 = hl_solvePolish(seg_seq[s], n, target);
    /* runs of one mask; jitter = runs under 4 steps, a first "natural look" measure
       (a hand on a joystick rarely changes it faster than every 4 logic steps) */
    for (runs = 1, jitter = 0, k = 0, i = 1; i <= n2; i++)
      if (i == n2 || seg_seq[s][i] != seg_seq[s][i - 1])
      {
	if (i - k < 4) jitter++;
	if (i < n2) runs++;
	k = i;
      }
    r = hl_solveReplay(seg_seq[s], n2, target);
    printf("solve: submap %#04x -> %d: %d steps (%d before polish), %d runs "
	   "(%d under 4 steps)%s, search %.1f s, polish %.1f s%s\n",
	   (unsigned int)sm, target, n2, n, runs, jitter,
	   seg_need[s] ? " keeping bombs" : "", t1 - t0, now() - t1,
	   r == n2 ? "" : " -- REPLAY FAILED");
    if (r != n2)
      return 1;
    seg_n[s] = n2;
    s++;
    if (!seg_snap[s])
      seg_snap[s] = malloc(sz);
    hl_stateSave(seg_snap[s]);  /* the next submap's tick 0 */
    /* seg_need[s] is kept, not reset: a requirement raised by a backtrack holds
       for every submap in between (arriving at 0x06 with a bomb means 0x04 and
       0x05 must keep it too). Resetting it made the chain spend the bomb again
       and cycle through the same four submaps until out of backtracks. */
    if (game_hlStatus() != GAME_HL_STEP)
      break;  /* game completed */
  }
  hl_solveMinBombs(0);

  for (i = 0; i < s; i++)
    n_all += (size_t)seg_n[i];
  printf("solve: %d submap(s), %lu steps, %d backtrack(s), now at submap %#04x, "
	 "lives %u, bombs %u, score %lu\n", s, (unsigned long)n_all, backtracks,
	 (unsigned int)env_submap, (unsigned int)env_lives, (unsigned int)env_bombs,
	 (unsigned long)env_score);
  if (out && (f = fopen(out, "wb")))
  {
    for (i = 0; i < s; i++)
      fwrite(seg_seq[i], 1, (size_t)seg_n[i], f);
    fclose(f);
  }
  /* -save: the state reached -- the next submap's tick 0 when all went well */
  if (save && !snap_file(save, 1))
    return 2;
  return 0;
}


int
main(int argc, char *argv[])
{
  unsigned long steps = 100000, scramble = 0, i;
  int a, rounds = 0, only = 0, c, dump = 0, chain = -1, distance = 0;
  const char *load = NULL, *save = NULL;
  U8 r = GAME_HL_STEP;
  const char *why, *inputs = NULL, *out = NULL;
  hl_solveopt_t sopt = { 128, 3000, HL_SOLVE_AUTO, 0, 1, NULL };
  FILE *f = NULL;

  for (a = 1; a < argc; a++)
  {
    if (!strcmp(argv[a], "-demo"))
      sysarg_args_demo = 1;
    else if (!strcmp(argv[a], "-record") && a + 1 < argc)
      sysarg_args_record = argv[++a];  /* demo.c writes the C file at exit */
    else if (!strcmp(argv[a], "-trace") && a + 1 < argc)
      sysarg_args_trace = argv[++a];
    else if (!strcmp(argv[a], "-steps") && a + 1 < argc)
      steps = strtoul(argv[++a], NULL, 0);
    else if (!strcmp(argv[a], "-scramble") && a + 1 < argc)
      scramble = strtoul(argv[++a], NULL, 0);
    else if (!strcmp(argv[a], "-submap") && a + 1 < argc)
      set_submap(only = atoi(argv[++a]));
    else if (!strcmp(argv[a], "-fuzz") && a + 1 < argc)
      rounds = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-seed") && a + 1 < argc)
      rnd_s = (U32)strtoul(argv[++a], NULL, 0) | 1u;
    else if (!strcmp(argv[a], "-v"))
      fuzz_verbose++;
    else if (!strcmp(argv[a], "-log") && a + 1 < argc)
      fuzz_log = argv[++a];
    else if (!strcmp(argv[a], "-inputs") && a + 1 < argc)
      inputs = argv[++a];
    else if (!strcmp(argv[a], "-distance"))
      distance = 1;
    else if (!strcmp(argv[a], "-dump"))
      dump = 1;
    else if (!strcmp(argv[a], "-reseed"))
      game_hlReseed(TRUE);
    else if (!strcmp(argv[a], "-solve"))
      chain = 1;
    else if (!strcmp(argv[a], "-chain") && a + 1 < argc)
      chain = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-beam") && a + 1 < argc)
      sopt.beam = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-maxsteps") && a + 1 < argc)
      sopt.maxsteps = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-to") && a + 1 < argc)
      sopt.target = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-stuck") && a + 1 < argc)
      sopt.stuck = argv[++a];
    else if (!strcmp(argv[a], "-save") && a + 1 < argc)
      save = argv[++a];
    else if (!strcmp(argv[a], "-load") && a + 1 < argc)
      load = argv[++a];
    else if (!strcmp(argv[a], "-waypoint") && a + 1 < argc)
    {
      int wr, wc;
      if (sscanf(argv[++a], "%d,%d", &wr, &wc) != 2) usage();
      hl_solveWaypoint(wr, wc);
    }
    else if (!strcmp(argv[a], "-forbid") && a + 1 < argc)
    {
      int r0, c0, r1, c1;
      if (sscanf(argv[++a], "%d,%d,%d,%d", &r0, &c0, &r1, &c1) != 4) usage();
      hl_solveForbid(r0, c0, r1, c1);
    }
    else if (!strcmp(argv[a], "-minbombs") && a + 1 < argc)
      minbombs = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-noclosures"))
      sopt.closures = 0;
    else if (!strcmp(argv[a], "-out") && a + 1 < argc)
      out = argv[++a];
    else if (!strcmp(argv[a], "-list"))
    {
      hl_stateList();
      return 0;
    }
    else
      usage();
  }

  if (rounds)
    return fuzz(rounds, only);
  if (chain >= 0)  /* -chain 0: settle at tick 0 (then -save), the MCP new_game */
  {
    sopt.verbose = fuzz_verbose;
    return solve(chain, &sopt, out, load, save);
  }

  for (i = 0; i < scramble; i++)
  {
#ifdef PLATFORM_ST
    e_them_rndstep();
#else
    e_them_rndseed++;
#endif
  }

  game_hlStart();
  if (load)
  {
    /* -load: carry on from a snapshot (e.g. a solver's -stuck), same build only */
    U8 *snap = malloc(hl_stateSize());
    FILE *fl = fopen(load, "rb");
    game_hlSettle();
    if (!fl || fread(snap, 1, hl_stateSize(), fl) != hl_stateSize())
    {
      fprintf(stderr, "xrick-core: cannot load '%s'\n", load);
      return 2;
    }
    fclose(fl);
    hl_stateLoad(snap);
    free(snap);
    steps += game_hlSteps();  /* -steps counts from the loaded state */
  }

  if (inputs && !(f = fopen(inputs, "rb")))
  {
    fprintf(stderr, "xrick-core: cannot read '%s'\n", inputs);
    return 2;
  }

  why = "step limit";
  while (game_hlSteps() < steps)
  {
    if (f)
    {
      /* a -log file: play on to its end, through game overs and new games */
      if ((c = fgetc(f)) == EOF) { why = "end of inputs"; break; }
      if (c == FUZZ_NEWGAME)
	game_hlStart();
      else
	game_hlStep((U8)c);
      continue;
    }
    r = game_hlStep(0);
    if (r == GAME_HL_OVER) { why = "game over"; break; }
    if (r == GAME_HL_END) { why = "game completed"; break; }
    if (sysarg_args_demo && !demo_active) { why = "end of demo"; break; }
  }

  if (save && !snap_file(save, 1))  /* -save after a plain run too */
    return 2;
  if (distance)
  {
    int w, t = sopt.target == HL_SOLVE_AUTO ? hl_solveTarget() : sopt.target;
    int d = hl_solveDistance(t, &w);
    printf("distance to %d: %d%s (rick x %d y %d)\n", t, d, w ? ", a wall stands" : "",
	   (int)ent_ents[1].x, (int)ent_ents[1].y);
  }
  if (dump)
  {
    hl_dump(stdout);  /* stdout is the JSON alone */
    fprintf(stderr, "xrick-core: %s\n", why);
    return 0;
  }
  printf("xrick-core: %s after %lu steps, submap %#04x, lives %u, score %lu\n",
	 why, (unsigned long)game_hlSteps(), (unsigned int)env_submap,
	 (unsigned int)env_lives, (unsigned long)env_score);
  return 0;
}

/* eof */
