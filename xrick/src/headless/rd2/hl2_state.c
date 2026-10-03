/*
 * xrick/src/headless/rd2/hl2_state.c
 *
 * xrick2-core only (branch `solver`, never shipped): snapshot, restore and hash of
 * the RD2 game state between two hl2_step calls (PLAN.md T47 phase 4).
 *
 * The state is the emulated RAM the game writes during play: the regions of
 * kb2/demo-solver.md §2, found by the write audit (xrick2-core -audit, phase 1).
 * Everything else in rd2_ram is the pristine program or what the map loader copies
 * in (rd2_load.c), so a snapshot also records the map loaded ([$1239e]); a restore
 * into another map copies that map's files back first, as rd2_123ca does. Plus
 * hl2_game.c's own counters.
 */

#include <string.h>

#include "rd2_mem.h"
#include "rd2_game.h"
#include "dat_rd2_demo.h"
#include "dat_rd2_levelimg.h"
#include "hl2.h"
#include "hl2_state.h"

/* the write audit's regions, [lo, hi) -- kb2/demo-solver.md §2 */
static const struct { U32 lo, hi; int render; const char *name; } regions[] = {
#include "hl2_regions.h"
};
#define N_REGIONS ((int)(sizeof(regions) / sizeof(regions[0])))

/* with the render regions or not (hl2_stateRender) */
static int with_render = 1;
#define SKIP(i) (!with_render && regions[i].render)

void
hl2_stateRender(int on)
{
	with_render = on;
}

/* the part of the snapshot that is not RAM */
typedef struct {
	U16 map_loaded;
	int status;
	U32 tick;
} hdr_t;

size_t
hl2_stateSize(void)
{
	size_t n = sizeof(hdr_t);
	int i;
	for (i = 0; i < N_REGIONS; i++)
		if (!SKIP(i))
			n += regions[i].hi - regions[i].lo;
	return n;
}

void
hl2_stateSave(U8 *s)
{
	hdr_t h;
	int i;

	h.map_loaded = rd2_rw(RD2_MAP_LOADED);
	hl2_gameGet(&h.status, &h.tick);
	memcpy(s, &h, sizeof h);
	s += sizeof h;
	for (i = 0; i < N_REGIONS; i++) {
		if (SKIP(i))
			continue;
		memcpy(s, rd2_ram + regions[i].lo, regions[i].hi - regions[i].lo);
		s += regions[i].hi - regions[i].lo;
	}
}

void
hl2_stateLoad(const U8 *s)
{
	hdr_t h;
	int i, d0;

	memcpy(&h, s, sizeof h);
	s += sizeof h;
	if (h.map_loaded != rd2_rw(RD2_MAP_LOADED) && h.map_loaded >= 1 && h.map_loaded <= 4) {
		d0 = h.map_loaded - 1;                  /* rd2_123ca's copies */
		memcpy(rd2_ram + 0x3efc0u, rd2_demo[d0], RD2_DEMO_BYTES);
		memcpy(rd2_ram + 0x65300u, rd2_stage1[d0], rd2_stage1_size[d0]);
		memcpy(rd2_ram + 0x53400u, rd2_levelimg[d0], RD2_LEVELIMG_SIZE);
	}
	for (i = 0; i < N_REGIONS; i++) {
		if (SKIP(i))
			continue;
		memcpy(rd2_ram + regions[i].lo, s, regions[i].hi - regions[i].lo);
		s += regions[i].hi - regions[i].lo;
	}
	hl2_gameSet(h.status, h.tick);
}

/* FNV-1a 64 over the regions (those a snapshot holds); <key> leaves out the render
   regions whatever the mode: they do not make two states play differently */
static unsigned long long
fnv(int key)
{
	unsigned long long h = 0xcbf29ce484222325ULL;
	U32 a;
	int i;

	for (i = 0; i < N_REGIONS; i++) {
		if (SKIP(i) || (key && regions[i].render))
			continue;
		for (a = regions[i].lo; a < regions[i].hi; a++) {
			h ^= rd2_ram[a];
			h *= 0x100000001b3ULL;
		}
	}
	return h;
}

/* the search key, 8 bytes at a time (the byte-wise FNV was about a fifth of a
   search's time); same regions as fnv(1) */
static unsigned long long
key64(void)
{
	unsigned long long h = 0x9e3779b97f4a7c15ULL, w;
	U32 a;
	int i;

	for (i = 0; i < N_REGIONS; i++) {
		if (regions[i].render)
			continue;
		for (a = regions[i].lo; a + 8 <= regions[i].hi; a += 8) {
			memcpy(&w, rd2_ram + a, 8);
			h = (h ^ w) * 0xff51afd7ed558ccdULL;
			h ^= h >> 29;
		}
		for (; a < regions[i].hi; a++)
			h = (h ^ rd2_ram[a]) * 0x100000001b3ULL;
	}
	return h ^ (h >> 32);
}

unsigned long long hl2_stateHash(void) { return fnv(0); }
unsigned long long hl2_stateKey(void) { return key64(); }

int
hl2_stateRegions(void)
{
	return N_REGIONS;
}

void
hl2_stateRegion(int i, U32 *lo, U32 *hi, const char **name)
{
	*lo = regions[i].lo;
	*hi = regions[i].hi;
	*name = regions[i].name;
}

/* eof */
