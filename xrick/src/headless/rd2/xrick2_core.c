/*
 * xrick/src/headless/rd2/xrick2_core.c
 *
 * xrick2-core only (branch `solver`, never shipped): RD2's game logic with no video,
 * sound or timing, one game_main frame at a time (PLAN.md T47, kb2/demo-solver.md).
 * Built by `make core2`.
 *
 *   xrick2-core [-map <n>] [-inputs <file>] [-steps <n>] [-trace <dir> [-n <frames>]]
 *
 * -map     start a new game on map <n>, 1..4, as `xrick -game 2 -map <n>` (default 1).
 * -inputs  one joystick byte per frame ([$1a4fb]: bit 0 up, 1 down, 2 left, 3 right,
 *          7 fire), the RD2_JOYSEQ / .joy format. Without it, nothing is held.
 * -steps   stop after <n> frames (default 100000).
 * -trace   as the SDL build's RD2_TRACE: <dir>/f<k>.bin = RAM $12e00-$17800,
 *          $54c00-$56400, $70000-$7ffff at frame head k (k = 1..), before frame k's
 *          input is written. -n: frames to write (default 1000).
 * -dump    at the end, the state as JSON on stdout (hl2_obs.c) instead of the summary.
 * -solve / -chain <n>: after -load / -inputs, solve one / <n> exits in a row (hl2_solve.c);
 *          -exit <i> (index in -dump's exits; default: the route's), -waypoint <row>,<col>,
 *          -beam, -maxsteps, -minbombs, -minlaser, -v; -out <file> appends the frames.
 *          -jobs <n>: <n> worker processes expand the beam (same result as 1); -stall <n>:
 *          give up after <n> frames with no new closest distance (default 600).
 *          When an exit is not found, the submap's switches are tried (-noswitches: not).
 *          -onemap: the chain stops when the map is done (maps are solved one by one).
 * -load / -save <file>: start from / write a snapshot (same build only).
 * -distance: Rick's tile distance to the goal, and the route's to the map's end. -tiles <s>: submap s's tiles.
 * -view <up>,<down>: the tiles from <up> rows above Rick's feet to <down> below, with
 *          Rick, actors, objects, switch boxes and exits drawn in, and a legend.
 * -shot <file.ppm> [-zoom <z>]: the screen last shown (default zoom 2).
 * -switch <i>: list the submap's switches, fire the i-th (-out: append its frames).
 * -steplog one line per frame on stdout: input, map, submap, scroll, rick, counters.
 *
 *   xrick2-core -fuzz <rounds> [-map <n>] [-seed <n>] [-norender]
 *   xrick2-core -audit <frames> [-seed <n>]      (build with -DHL2_WATCH: make audit2)
 *
 * -fuzz    snapshot/restore check (T47 phase 4), see fuzz() below. Every map, or
 *          only -map's. -norender: snapshots without the drawing buffers, as the
 *          search takes them (the hashes then leave them out too).
 * -audit   T47 phase 1: random play on every map, <frames> frames each, then print
 *          the RAM ranges written after the boot (rd2_mem.h HL2_WATCH).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "system.h"
#include "rd2_mem.h"
#include "hl2.h"
#include "hl2_state.h"
#include "hl2_obs.h"
#include "hl2_solve.h"
#include "hl2_switch.h"

static void
usage(void)
{
	fprintf(stderr,
	        "usage: xrick2-core [-map <n>] [-inputs <file>] [-steps <n>] [-trace <dir> [-n <frames>]]\n");
	exit(2);
}

static double
now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* the RD2_TRACE windows of frame head <k> */
static void
trace(const char *dir, unsigned long k)
{
	char path[1024];
	FILE *f;

	snprintf(path, sizeof path, "%s/f%lu.bin", dir, k);
	if (!(f = fopen(path, "wb")))
		return;
	fwrite(rd2_ram + 0x12e00, 1, 0x17800 - 0x12e00, f);
	fwrite(rd2_ram + 0x54c00, 1, 0x56400 - 0x54c00, f);
	fwrite(rd2_ram + 0x70000, 1, 0x10000, f);
	fclose(f);
}

/* random inputs, xorshift32 */
static U32 rnd_s = 0x2545F491u;

static U32
rnd(void)
{
	rnd_s ^= rnd_s << 13;
	rnd_s ^= rnd_s >> 17;
	rnd_s ^= rnd_s << 5;
	return rnd_s;
}

/* a joystick byte held 1 to 16 frames; up = jump/climb, fire+up = laser,
   fire+down = bomb, fire+left/right = melee */
static void
rnd_inputs(U8 *in, int n)
{
	static const U8 c[] = {
		0, 0x01, 0x02, 0x04, 0x08, 0x80, 0x05, 0x09, 0x06, 0x0a,
		0x81, 0x82, 0x84, 0x88, 0x86, 0x8a
	};
	int i = 0, k;
	U8 v;

	while (i < n) {
		v = c[rnd() % sizeof c];
		for (k = (int)(rnd() % 16) + 1; k > 0 && i < n; k--)
			in[i++] = v;
	}
}

#ifdef HL2_WATCH
U8 hl2_wmap[RD2_RAM_SIZE];

/*
 * audit: every byte written after the boot, over random play on each map, games
 * restarted at game over. Prints [lo, hi) ranges, gaps under 16 bytes merged.
 */
static int
audit(unsigned long frames)
{
	static U8 in[4096];
	unsigned long f;
	U32 a, lo = 0;
	int m, in_r = 0, gap = 0, r;

	for (m = 1; m <= 4; m++) {
		hl2_boot();
		memset(hl2_wmap, 0, sizeof hl2_wmap);   /* boot writes are the same in every game */
		hl2_newgame(m);
		for (f = 0; f < frames; ) {
			int i;
			rnd_inputs(in, (int)sizeof in);
			for (i = 0; i < (int)sizeof in && f < frames; i++, f++) {
				r = hl2_step(in[i]);
				if (r != HL2_STEP && r != HL2_MAP) {
					printf("audit: map %d, frame %lu: run over (%d), new game\n", m, f, r);
					hl2_newgame(m);
				}
			}
		}
		printf("audit: map %d done, now at map %u submap %u\n", m, rd2_rw(HL2_MAP_PLAYING),
		       rd2_rw(HL2_SUBMAP));
		/* keep this map's marks: the next boot must not clear them */
		{
			static U8 acc[RD2_RAM_SIZE];
			for (a = 0; a < RD2_RAM_SIZE; a++)
				acc[a] |= hl2_wmap[a];
			if (m == 4)
				memcpy(hl2_wmap, acc, sizeof acc);
		}
	}
	for (a = 0; a <= RD2_RAM_SIZE; a++) {
		int w = a < RD2_RAM_SIZE && hl2_wmap[a];
		if (w) {
			if (!in_r) { lo = a; in_r = 1; }
			gap = 0;
		} else if (in_r && (++gap >= 16 || a == RD2_RAM_SIZE)) {
			printf("	{ 0x%05xu, 0x%05xu, \"\" },  /* %u bytes */\n", lo, a - (U32)gap + 1,
			       a - (U32)gap + 1 - lo);
			in_r = 0;
		}
	}
	return 0;
}
#endif

/*
 * fuzz
 *
 * per round, at the current frame boundary: snapshot S and hash H0; play random
 * inputs I (FUZZ_LEN frames) hashing every frame; play other inputs J so the state
 * goes elsewhere, and every 4th round also start a new game on another map (the
 * restore must then bring the map's files back); restore S, check the hash is H0
 * again, replay I and check every hash. Then carry on from the end of I; a run that
 * ends starts a new game on the same map.
 */
#define FUZZ_LEN 64

static int
fuzz(int rounds, int only)
{
	static U8 in[FUZZ_LEN], other[FUZZ_LEN];
	static unsigned long long h1[FUZZ_LEN];
	size_t sz = hl2_stateSize();
	U8 *snap = malloc(sz);
	unsigned long long h0;
	unsigned long frames = 0, restores = 0, bad = 0, overs = 0, maps = 0;
	double t0 = now(), ts = 0, tl = 0, t;
	int m, r, i, j, rr;
	U8 seen[5][32];

	memset(seen, 0, sizeof seen);
	for (m = only ? only : 1; m <= (only ? only : 4); m++) {
		hl2_start(m);
		for (r = 0; r < rounds; r++) {
			t = now(); hl2_stateSave(snap); ts += now() - t;
			h0 = hl2_stateHash();
			rnd_inputs(in, FUZZ_LEN);
			rnd_inputs(other, FUZZ_LEN);
			for (i = 0; i < FUZZ_LEN; i++) {
				hl2_step(in[i]);
				h1[i] = hl2_stateHash();
			}
			for (i = 0; i < FUZZ_LEN; i++)
				hl2_step(other[i]);
			if (r % 4 == 3) {
				hl2_start(m % 4 + 1);
				for (i = 0; i < FUZZ_LEN; i++)
					hl2_step(other[i]);
			}
			t = now(); hl2_stateLoad(snap); tl += now() - t;
			restores++;
			if (hl2_stateHash() != h0) {
				printf("MISMATCH map %d round %d: hash after restore\n", m, r);
				bad++;
			}
			for (i = 0, rr = HL2_STEP; i < FUZZ_LEN; i++) {
				rr = hl2_step(in[i]);
				if (hl2_stateHash() != h1[i]) {
					printf("MISMATCH map %d round %d: frame %d of the replay\n", m, r, i);
					bad++;
					break;
				}
			}
			frames += 3 * FUZZ_LEN;
			j = rd2_rw(HL2_MAP_PLAYING);
			seen[j <= 4 ? j : 0][rd2_rw(HL2_SUBMAP) & 31] = 1;
			if (rr == HL2_MAP)
				maps++;
			if (rr != HL2_STEP && rr != HL2_MAP) {
				overs++;
				hl2_start(m);
			}
		}
	}
	t = now() - t0;
	printf("fuzz: %lu restores, %lu frames, %lu runs over, %lu maps done, %lu mismatches\n",
	       restores, frames, overs, maps, bad);
	printf("fuzz: submaps reached:");
	for (m = 1; m <= 4; m++)
		for (i = 0; i < 32; i++)
			if (seen[m][i])
				printf(" %d.%d", m, i);
	printf("\nfuzz: %.2f s, %.0f frames/s incl. a hash per frame; snapshot %lu bytes, "
	       "save %.1f us, restore %.1f us\n", t, (double)frames / t, (unsigned long)sz,
	       ts / (double)restores * 1e6, tl / (double)restores * 1e6);
	free(snap);
	return bad ? 1 : 0;
}

/* read / write a snapshot file, with its drawing buffers (same build only) */
static int
snap_file(const char *path, int write)
{
	size_t sz = hl2_stateSize();
	U8 *snap = malloc(sz);
	FILE *f = fopen(path, write ? "wb" : "rb");
	int ok;

	if (write) hl2_stateSave(snap);
	ok = f && (write ? fwrite(snap, 1, sz, f) : fread(snap, 1, sz, f)) == sz;
	if (f) fclose(f);
	if (ok && !write) hl2_stateLoad(snap);
	free(snap);
	if (!ok)
		fprintf(stderr, "xrick2-core: cannot %s '%s'\n", write ? "write" : "read", path);
	return ok;
}

/*
 * solve: <chain> legs in a row, each from the state the previous one left. Per leg:
 * search, polish, replay -- the replay moves the game on to the next submap's (or
 * map's) first frame. -out appends every frame's joystick byte to <file>, a file
 * `xrick2-core -inputs` replays after the same prefix.
 */
#define SOLVE_MAX 0x8000

static int switches = 1;  /* -noswitches: 0 */
static int onemap = 0;    /* -onemap: the chain stops when the map is done */

/* append frames to -out */
static void
emit(const char *out, const U8 *seq, int n)
{
	FILE *f;
	if (out && (f = fopen(out, "ab"))) {
		fwrite(seq, 1, (size_t)n, f);
		fclose(f);
	}
}

/*
 * one leg: search the exit. Not found: commit the search's closest safe state
 * (hl2_solve staging) while that gets Rick closer, else try the switches not tried
 * yet in this leg (hl2_switch.c), nearest to where the search got closest first:
 * fire one, search again, and keep the switch only if that search finds the exit
 * or gets closer than before it -- otherwise undo it (map 2 submap 1: the lift,
 * fired again after its trip, took Rick back down). What is committed is played
 * (the state moves on) and appended to -out.
 */
#define STAGES_MAX 64
#define TRIED_MAX 64

/*
 * a bomb still in flight blocks the next one (one at a time): the switches' presses
 * would all be ignored (map 3 submap 2: a stage ended just after a drop). Idle until
 * it is gone -- at most 50 frames, well within the 150 a stage survives idle -- if Rick lives
 * through it; else leave the state as it is.
 */
static void
settle(const char *out)
{
	static U8 z[50];
	U8 *back;
	int i, lives = rd2_rw(HL2_LIVES), sub = rd2_rw(HL2_SUBMAP);

	if (rd2_rw(0x16b12u) == 0)
		return;
	back = malloc(hl2_stateSize());
	hl2_stateSave(back);
	for (i = 0; i < 50 && rd2_rw(0x16b12u) != 0; i++)
		if (hl2_step(0) != HL2_STEP || hl2_rickDead() || rd2_rw(HL2_LIVES) < lives ||
		    rd2_rw(HL2_SUBMAP) != sub)
			break;
	if (rd2_rw(0x16b12u) == 0 && !hl2_rickDead() && rd2_rw(HL2_LIVES) >= lives) {
		emit(out, z, i);
		printf("solve:   %d idle frames: the bomb in flight is gone\n", i);
	} else
		hl2_stateLoad(back);
	free(back);
}

static int
attempt(hl2_solveopt_t *o, U8 *seq, const char *out)
{
	static U8 stage[SOLVE_MAX], swseq[SOLVE_MAX];
	U32 tried[TRIED_MAX];
	int stage_n = -1, n = -1, stages = 0, n_tried = 0, i, d0, d1, si, nsw, fired, have = 0, best0;
	hl2_solveopt_t so = *o;
	hl2_solveres_t res;
	hl2_switch_t sw[32];
	U8 *back = malloc(hl2_stateSize());

	so.stage = stage;
	so.stage_n = &stage_n;
	so.res = &res;
	for (;;) {
		if (!have)
			n = hl2_solve(&so, seq, SOLVE_MAX);
		have = 0;
		if (n >= 0)
			break;
		best0 = res.best_h;
		d0 = hl2_solveDistance(&so);
		if (stage_n > 0 && stages < STAGES_MAX) {
			hl2_stateSave(back);
			for (i = 0; i < stage_n; i++)
				hl2_step(stage[i]);
			d1 = hl2_solveDistance(&so);
			if (d1 < d0) {
				printf("solve:   stage %d: %d frames, distance %d -> %d\n", ++stages, stage_n, d0, d1);
				emit(out, stage, stage_n);
				settle(out);
				continue;
			}
			hl2_stateLoad(back);
		}
		if (o->wp_row >= 0 || !switches)
			break;
		settle(out);
		nsw = hl2_switches(sw, 32);
		hl2_switchesSort(sw, nsw, res.row, res.col);
		printf("solve:   closest distance %d at row %d col %d; switches\n", best0, res.row, res.col);
		for (fired = 0, si = 0; si < nsw && !fired; si++) {
			int ns, k;
			if (hl2_switchFired(&sw[si]))
				continue;
			for (k = 0; k < n_tried && tried[k] != sw[si].box; k++) ;
			if (k < n_tried || n_tried == TRIED_MAX)
				continue;
			tried[n_tried++] = sw[si].box;
			ns = hl2_switchFire(o, &sw[si], swseq, SOLVE_MAX);
			printf("solve:   switch %lu box x %d row %d %dx%d mask %d: %s\n",
			       (unsigned long)sw[si].rec, sw[si].x, sw[si].row, sw[si].w, sw[si].h,
			       sw[si].mask, ns < 0 ? "not fired" : "fired");
			if (ns < 0)
				continue;
			hl2_stateSave(back);
			for (i = 0; i < ns; i++)
				hl2_step(swseq[i]);
			n = hl2_solve(&so, seq, SOLVE_MAX);
			if (n >= 0 || res.best_h < best0) {
				printf("solve:   switch kept: %s\n", n >= 0 ? "exit found" : "closer");
				if (n < 0)
					printf("solve:   closest distance %d -> %d\n", best0, res.best_h);
				emit(out, swseq, ns);
				fired = 1;
				have = 1;
			} else {
				printf("solve:   switch undone: closest distance %d, not under %d\n", res.best_h, best0);
				hl2_stateLoad(back);
			}
		}
		if (!fired)
			break;
	}
	free(back);
	return n;
}

static int
solve(int chain, hl2_solveopt_t *o, const char *out)
{
	static U8 seq[SOLVE_MAX];
	int leg, n, n2, rr, runs, jitter, i, kk, ex;
	double t0, t1;
	FILE *f;

	for (leg = 0; leg < chain; leg++) {
		hl2_exit_t e[HL2_EXITS_MAX];
		int ne = hl2_exits(e, HL2_EXITS_MAX), sm = rd2_rw(HL2_SUBMAP), mp = rd2_rw(HL2_MAP_PLAYING);
		ex = o->exit >= 0 ? o->exit : hl2_solveRoute();
		t0 = now();
		n = attempt(o, seq, out);
		t1 = now();
		if (n < 0) {
			printf("solve: map %d submap %d exit %d (%s row %d -> %d): NOT FOUND (beam %d, %.1f s)\n",
			       mp, sm, ex, ex >= 0 && ex < ne && e[ex].side == 1 ? "left" : "right",
			       ex >= 0 && ex < ne ? e[ex].row : -1, ex >= 0 && ex < ne ? e[ex].target : -1,
			       o->beam, t1 - t0);
			return 1;
		}
		n2 = hl2_solvePolish(o, seq, n);
		for (runs = 1, jitter = 0, kk = 0, i = 1; i <= n2; i++)
			if (i == n2 || seq[i] != seq[i - 1]) {
				if (i - kk < 4) jitter++;
				if (i < n2) runs++;
				kk = i;
			}
		rr = hl2_solveReplay(o, seq, n2);
		printf("solve: map %d submap %d exit %d -> %s: %d frames (%d before polish), %d runs "
		       "(%d under 4 frames), search %.1f s, polish %.1f s%s\n", mp, sm, ex,
		       ex >= 0 && ex < ne ? (e[ex].done ? "map done" : "") : "?", n2, n, runs, jitter,
		       t1 - t0, now() - t1, rr == n2 ? "" : " -- REPLAY FAILED");
		if (ex >= 0 && ex < ne && !e[ex].done)
			printf("solve:   now submap %u\n", rd2_rw(HL2_SUBMAP));
		if (rr != n2)
			return 1;
		if (out && (f = fopen(out, "ab"))) {
			fwrite(seq, 1, (size_t)n2, f);
			fclose(f);
		}
		if (o->wp_row >= 0)
			break;                          /* a waypoint is one leg */
		if (hl2_status() == HL2_HANG || hl2_status() == HL2_END)
			break;
		if (onemap && rd2_rw(HL2_MAP_PLAYING) != (U16)mp) {
			printf("solve: map %d done\n", mp);
			break;
		}
	}
	printf("solve: now map %u submap %u tick %lu, lives %u, laser %u, bombs %u, score %02x%02x%02x\n",
	       rd2_rw(HL2_MAP_PLAYING), rd2_rw(HL2_SUBMAP), (unsigned long)hl2_tick(),
	       rd2_rw(HL2_LIVES), rd2_rw(HL2_LASER), rd2_rw(HL2_BOMBS),
	       rd2_rb(HL2_SCORE), rd2_rb(HL2_SCORE + 1), rd2_rb(HL2_SCORE + 2));
	return 0;
}

static const char *why[] = { "step limit", "map done", "game over", "map 5 hang", "run ended" };

int
main(int argc, char *argv[])
{
	unsigned long steps = 100000, ntrace = 1000, k = 0;
	int a, map = 1, c, r = HL2_STEP, rounds = 0, mapset = 0, steplog = 0, dump = 0;
	unsigned long auditf = 0;
	const char *inputs = NULL, *tdir = NULL, *stop = NULL, *out = NULL, *load = NULL, *save = NULL;
	int chain = 0, distance = 0, tiles = -1, swi = -1, view_up = -1, view_down = 0, zoom = 2;
	const char *shot = NULL;
	hl2_solveopt_t sopt;
	FILE *f = NULL;
	double t0;

	setvbuf(stdout, NULL, _IOLBF, 0);        /* progress lines through a pipe */
	hl2_solveDefaults(&sopt);
	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-map") && a + 1 < argc)
			map = atoi(argv[++a]), mapset = 1;
		else if (!strcmp(argv[a], "-inputs") && a + 1 < argc)
			inputs = argv[++a];
		else if (!strcmp(argv[a], "-steps") && a + 1 < argc)
			steps = strtoul(argv[++a], NULL, 0);
		else if (!strcmp(argv[a], "-trace") && a + 1 < argc)
			tdir = argv[++a];
		else if (!strcmp(argv[a], "-dump"))
			dump = 1;
		else if (!strcmp(argv[a], "-norender"))
			hl2_stateRender(0);
		else if (!strcmp(argv[a], "-steplog"))
			steplog = 1;
		else if (!strcmp(argv[a], "-n") && a + 1 < argc)
			ntrace = strtoul(argv[++a], NULL, 0);
		else if (!strcmp(argv[a], "-fuzz") && a + 1 < argc)
			rounds = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-seed") && a + 1 < argc)
			rnd_s = (U32)strtoul(argv[++a], NULL, 0) | 1u;
		else if (!strcmp(argv[a], "-audit") && a + 1 < argc)
			auditf = strtoul(argv[++a], NULL, 0);
		else if (!strcmp(argv[a], "-solve"))
			chain = 1;
		else if (!strcmp(argv[a], "-chain") && a + 1 < argc)
			chain = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-beam") && a + 1 < argc)
			sopt.beam = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-jobs") && a + 1 < argc)
			sopt.jobs = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-stall") && a + 1 < argc)
			sopt.stall = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-maxsteps") && a + 1 < argc)
			sopt.maxsteps = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-exit") && a + 1 < argc)
			sopt.exit = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-waypoint") && a + 1 < argc) {
			if (sscanf(argv[++a], "%d,%d", &sopt.wp_row, &sopt.wp_col) != 2)
				usage();
		} else if (!strcmp(argv[a], "-minbombs") && a + 1 < argc)
			sopt.minbombs = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-minlaser") && a + 1 < argc)
			sopt.minlaser = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-out") && a + 1 < argc)
			out = argv[++a];
		else if (!strcmp(argv[a], "-load") && a + 1 < argc)
			load = argv[++a];
		else if (!strcmp(argv[a], "-save") && a + 1 < argc)
			save = argv[++a];
		else if (!strcmp(argv[a], "-tiles") && a + 1 < argc)
			tiles = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-switch") && a + 1 < argc)
			swi = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-noswitches"))
			switches = 0;
		else if (!strcmp(argv[a], "-onemap"))
			onemap = 1;
		else if (!strcmp(argv[a], "-stuck") && a + 1 < argc)
			sopt.stuck = argv[++a];
		else if (!strcmp(argv[a], "-distance"))
			distance = 1;
		else if (!strcmp(argv[a], "-view") && a + 1 < argc) {
			if (sscanf(argv[++a], "%d,%d", &view_up, &view_down) != 2)
				usage();
		} else if (!strcmp(argv[a], "-shot") && a + 1 < argc)
			shot = argv[++a];
		else if (!strcmp(argv[a], "-zoom") && a + 1 < argc)
			zoom = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-v"))
			sopt.verbose = 1;
		else
			usage();
	}
	if (map < 1 || map > 4)
		usage();
	if (auditf) {
#ifdef HL2_WATCH
		return audit(auditf);
#else
		fprintf(stderr, "xrick2-core: -audit needs the HL2_WATCH build (make audit2)\n");
		return 2;
#endif
	}
	if (rounds)
		return fuzz(rounds, mapset ? map : 0);
	if (inputs && !(f = fopen(inputs, "rb"))) {
		fprintf(stderr, "xrick2-core: cannot read '%s'\n", inputs);
		return 2;
	}

	hl2_start(map);
	if (load && !snap_file(load, 0))
		return 2;
	t0 = now();
	while (k < steps) {
		if (tdir && k < ntrace)
			trace(tdir, k + 1);
		c = 0;
		if (f && (c = fgetc(f)) == EOF) {
			stop = "end of inputs";
			break;
		}
		if (!f && chain > 0)
			break;                              /* nothing to play before the search */
		k++;
		r = hl2_step((U8)c);
		if (steplog)
			printf("%lu joy %02x map %u sub %u scroll %u x %d y %d dead %d lives %u laser %u "
			       "bombs %u bomb %d shot %d\n", k, c, rd2_rw(HL2_MAP_PLAYING),
			       rd2_rw(HL2_SUBMAP), rd2_rw(HL2_SCROLL), rd2_rws(HL2_RICK_X),
			       rd2_rws(HL2_RICK_Y), rd2_rws(HL2_RICK_DEAD), rd2_rw(HL2_LIVES),
			       rd2_rw(HL2_LASER), rd2_rw(HL2_BOMBS), rd2_rws(0x16b12), rd2_rws(0x16902));
		if (r != HL2_STEP && r != HL2_MAP)
			break;
	}
	if (chain > 0) {
		int res = solve(chain, &sopt, out);
		if (save && !snap_file(save, 1))
			return 2;
		if (dump)
			hl2_dump(stdout);
		return res;
	}
	if (tiles >= 0)
		hl2_tiles(stdout, tiles);
	if (swi >= 0) {                             /* -switch <i>: list the switches, fire the i-th */
		static U8 sseq[SOLVE_MAX];
		hl2_switch_t sw[32];
		int nsw = hl2_switches(sw, 32), i, ns;
		for (i = 0; i < nsw; i++)
			printf("switch %d: record %lu box x %d row %d %dx%d mask %d actor %d spawned %d fired %d\n",
			       i, (unsigned long)sw[i].rec, sw[i].x, sw[i].row, sw[i].w, sw[i].h, sw[i].mask,
			       sw[i].actor, sw[i].spawned, hl2_switchFired(&sw[i]));
		if (swi < nsw) {
			ns = hl2_switchFire(&sopt, &sw[swi], sseq, SOLVE_MAX);
			printf("switch %d: %d frames\n", swi, ns);
			if (ns > 0) {                       /* play it, so -save / -dump see the result */
				int i;
				for (i = 0; i < ns; i++)
					hl2_step(sseq[i]);
				emit(out, sseq, ns);
			}
		}
	}
	if (distance) {
		int ex = hl2_solveRoute(), rc = hl2_solveRouteCost();
		printf("distance: %d (exit %d), route %d\n", hl2_solveDistance(&sopt),
		       sopt.exit >= 0 ? sopt.exit : ex, rc);
	}
	if (view_up >= 0)
		hl2_view(stdout, view_up, view_down);
	if (shot && !hl2_shot(shot, zoom))
		fprintf(stderr, "xrick2-core: cannot write '%s'\n", shot);
	if (save && !snap_file(save, 1))
		return 2;
	if (dump) {
		hl2_dump(stdout);
		return 0;
	}
	printf("xrick2-core: %s after %lu frames (%.0f frames/s), map %u, submap %u, tick %lu, "
	       "lives %u, laser %u, bombs %u, score %02x%02x%02x\n",
	       stop ? stop : why[r == HL2_MAP ? 0 : r], k, (double)k / (now() - t0),
	       rd2_rw(HL2_MAP_PLAYING), rd2_rw(HL2_SUBMAP), (unsigned long)hl2_tick(),
	       rd2_rw(HL2_LIVES), rd2_rw(HL2_LASER), rd2_rw(HL2_BOMBS),
	       rd2_rb(HL2_SCORE), rd2_rb(HL2_SCORE + 1), rd2_rb(HL2_SCORE + 2));
	return 0;
}

/* eof */
