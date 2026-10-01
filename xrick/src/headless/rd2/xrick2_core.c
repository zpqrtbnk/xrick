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

static const char *why[] = { "step limit", "map done", "game over", "map 5 hang", "run ended" };

int
main(int argc, char *argv[])
{
	unsigned long steps = 100000, ntrace = 1000, k = 0;
	int a, map = 1, c, r = HL2_STEP, rounds = 0, mapset = 0, steplog = 0;
	unsigned long auditf = 0;
	const char *inputs = NULL, *tdir = NULL, *stop = NULL;
	FILE *f = NULL;
	double t0;

	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-map") && a + 1 < argc)
			map = atoi(argv[++a]), mapset = 1;
		else if (!strcmp(argv[a], "-inputs") && a + 1 < argc)
			inputs = argv[++a];
		else if (!strcmp(argv[a], "-steps") && a + 1 < argc)
			steps = strtoul(argv[++a], NULL, 0);
		else if (!strcmp(argv[a], "-trace") && a + 1 < argc)
			tdir = argv[++a];
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
	t0 = now();
	while (k < steps) {
		if (tdir && k < ntrace)
			trace(tdir, k + 1);
		c = 0;
		if (f && (c = fgetc(f)) == EOF) {
			stop = "end of inputs";
			break;
		}
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
	printf("xrick2-core: %s after %lu frames (%.0f frames/s), map %u, submap %u, tick %lu, "
	       "lives %u, laser %u, bombs %u, score %02x%02x%02x\n",
	       stop ? stop : why[r == HL2_MAP ? 0 : r], k, (double)k / (now() - t0),
	       rd2_rw(HL2_MAP_PLAYING), rd2_rw(HL2_SUBMAP), (unsigned long)hl2_tick(),
	       rd2_rw(HL2_LIVES), rd2_rw(HL2_LASER), rd2_rw(HL2_BOMBS),
	       rd2_rb(HL2_SCORE), rd2_rb(HL2_SCORE + 1), rd2_rb(HL2_SCORE + 2));
	return 0;
}

/* eof */
