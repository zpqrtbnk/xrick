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
#include "e_them.h"
#include "demo.h"
#include "hl_state.h"

static void
usage(void)
{
  fprintf(stderr,
	  "usage: xrick-core [-submap <n>] [-demo] [-trace <file>] [-steps <n>] [-scramble <n>]\n"
	  "       xrick-core -fuzz <rounds> [-submap <n>] [-seed <n>] [-log <file>]\n"
	  "       xrick-core [-submap <n>] -inputs <file>\n"
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
static U8 tl[1 << 20];                /* the inputs played since the game started */
static size_t tl_n;

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

int
main(int argc, char *argv[])
{
  unsigned long steps = 100000, scramble = 0, i;
  int a, rounds = 0, only = 0, c;
  U8 r = GAME_HL_STEP;
  const char *why, *inputs = NULL;
  FILE *f = NULL;

  for (a = 1; a < argc; a++)
  {
    if (!strcmp(argv[a], "-demo"))
      sysarg_args_demo = 1;
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
    else if (!strcmp(argv[a], "-log") && a + 1 < argc)
      fuzz_log = argv[++a];
    else if (!strcmp(argv[a], "-inputs") && a + 1 < argc)
      inputs = argv[++a];
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

  for (i = 0; i < scramble; i++)
  {
#ifdef PLATFORM_ST
    e_them_rndstep();
#else
    e_them_rndseed++;
#endif
  }

  game_hlStart();

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

  printf("xrick-core: %s after %lu steps, submap %#04x, lives %u, score %lu\n",
	 why, (unsigned long)game_hlSteps(), (unsigned int)env_submap,
	 (unsigned int)env_lives, (unsigned long)env_score);
  return 0;
}

/* eof */
