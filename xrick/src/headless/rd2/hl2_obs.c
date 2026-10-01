/*
 * xrick/src/headless/rd2/hl2_obs.c
 *
 * xrick2-core only (branch `solver`, never shipped): the RD2 state read back out of
 * emulated RAM -- submap tiles, exits, Rick, the record chain (PLAN.md T47 phase 5).
 * Every address comes from kb2/ (cited per item) and the transliterated routines.
 */

#include "rd2_mem.h"
#include "hl2.h"
#include "hl2_obs.h"

/* the current submap, as $14458 left it (algo-flow.md §5): block map [$1646a],
   max scroll [$16468] = w1 * 8, trigger table [$1435c] */
int
hl2_rows(void)
{
	return hl2_rowsOf(rd2_rw(HL2_SUBMAP));
}

U8
hl2_attr(int row, int col)
{
	return hl2_attrOf(rd2_rw(HL2_SUBMAP), row, col);
}

/* any submap of the loaded map, from its header at $54c00 + 8 * s: block map
   $56400 + w0, max scroll w1 * 8 (graphics.md §3a); the rows the 40-row tile
   window can show at the max scroll */
int
hl2_rowsOf(int s)
{
	return rd2_rw(0x54c02u + 8u * (U32)s) + 40;
}

/* block map rows of 8 block ids; block = 4 x 4 tile ids at $56d00 + 16 * id;
   attributes at $65200 + tile id (graphics.md §3a, algo-collision.md) */
U8
hl2_attrOf(int s, int row, int col)
{
	U32 bm = 0x56400u + rd2_rw(0x54c00u + 8u * (U32)s);
	U8 blk, tile;

	if (row < 0 || row >= hl2_rowsOf(s) || col < 0 || col >= HL2_COLS)
		return 0;
	blk = rd2_rb(bm + (U32)((row >> 2) * 8 + (col >> 2)));
	tile = rd2_rb(0x56d00u + (U32)blk * 16u + (U32)((row & 3) * 4 + (col & 3)));
	return rd2_rb(0x65200u + tile) & 0x7f;  /* bit 7: masked by every consumer */
}

/* exits of submap <s>: its trigger table, header word 2 */
int
hl2_exitsOf(int s, hl2_exit_t *e, int max)
{
	U32 a = 0x54c00u + rd2_rw(0x54c04u + 8u * (U32)s);
	int n = 0;
	U8 b0;

	while ((b0 = rd2_rb(a)) != 0 && n < max) {
		e[n].b0 = b0;
		e[n].side = b0 & 3;
		e[n].row = rd2_rb(a + 1);
		e[n].target = rd2_rb(a + 2);
		e[n].entry = rd2_rb(a + 3);
		e[n].done = (b0 & 0x90) == 0x90;
		e[n].tunnel = (b0 & 0x20) != 0;
		n++;
		a += 4;
	}
	return n;
}

int
hl2_submaps(void)
{
	return rd2_rw(0x54c04u) / 8;            /* first trigger offset / 8 (level-tables.md §1) */
}

/* the current submap's trigger table ([$1435c], set from the same header word by
   $14458): 4-byte records, a 0 byte ends it (level-tables.md §2); "done" as $14362
   tests it with [$17990] == 0 (the normal game) */
int
hl2_exits(hl2_exit_t *e, int max)
{
	return hl2_exitsOf(rd2_rw(HL2_SUBMAP), e, max);
}

static int
scroll8(void)
{
	return rd2_rw(HL2_SCROLL) & ~7;
}

int hl2_rickRow(void) { return (scroll8() + rd2_rws(HL2_RICK_Y) + 0x14) >> 3; }
int hl2_rickCol(void) { return (rd2_rws(HL2_RICK_X) + 4) >> 3; }
int hl2_rickDead(void) { return rd2_rw(HL2_RICK_DEAD) != 0 || rd2_rw(HL2_RICK_HIT) != 0; }

/* one tile as a character: lethal > solid > ladder > floor > surface > empty */
static char
tchar(U8 a)
{
	if (a & HL2_T_LETHAL) return '^';
	if (a & HL2_T_SOLID) return '#';
	if (a & HL2_T_LADDER) return (a & HL2_T_LTOP) ? 'T' : 'H';
	if (a & HL2_T_FLOOR) return '=';
	if (a & HL2_T_SURFACE) return '~';
	return '.';
}

/* submap <s>'s tiles, one row per line, numbered */
void
hl2_tiles(FILE *f, int s)
{
	int r, c;
	for (r = 0; r < hl2_rowsOf(s); r++) {
		fprintf(f, "%3d ", r);
		for (c = 0; c < HL2_COLS; c++)
			fputc(tchar(hl2_attrOf(s, r, c)), f);
		fputc(10, f);
	}
}

/* the 17-record chain $167a2, 88 bytes each (algo-player.md §1) */
static const char *
rec_name(int i)
{
	if (i < 4) return "object";
	if (i == 4) return "laser";
	if (i == 5) return "rick";
	if (i < 10) return "debris";
	if (i == 10) return "bomb";
	return "actor";
}

void
hl2_dump(FILE *f)
{
	hl2_exit_t e[HL2_EXITS_MAX];
	int n = hl2_exits(e, HL2_EXITS_MAX), i, r, c, first = 1;
	U32 a;

	fprintf(f, "{\n  \"map\": %u, \"submap\": %u, \"tick\": %lu, \"status\": %d,\n",
	        rd2_rw(HL2_MAP_PLAYING), rd2_rw(HL2_SUBMAP), (unsigned long)hl2_tick(), hl2_status());
	fprintf(f, "  \"lives\": %u, \"laser\": %u, \"bombs\": %u, \"score\": \"%02x%02x%02x\",\n",
	        rd2_rw(HL2_LIVES), rd2_rw(HL2_LASER), rd2_rw(HL2_BOMBS),
	        rd2_rb(HL2_SCORE), rd2_rb(HL2_SCORE + 1), rd2_rb(HL2_SCORE + 2));
	fprintf(f, "  \"scroll\": %u, \"max_scroll\": %u, \"rows\": %d,\n",
	        rd2_rw(HL2_SCROLL), rd2_rw(0x16468u), hl2_rows());
	fprintf(f, "  \"rick\": {\"x\": %d, \"y\": %d, \"row\": %d, \"col\": %d, \"vy\": %d, "
	        "\"dead\": %d, \"ladder\": %d, \"crouch\": %d, \"ground\": %d, \"tunnel\": %d, "
	        "\"facing_left\": %d},\n",
	        rd2_rws(HL2_RICK_X), rd2_rws(HL2_RICK_Y), hl2_rickRow(), hl2_rickCol(),
	        rd2_rws(0x16966u), hl2_rickDead(), rd2_rw(0x12e1au) != 0, rd2_rw(0x12e18u) != 0,
	        rd2_rw(0x12e1cu) != 0, rd2_rw(0x12e14u) != 0, rd2_rw(0x1697eu) == 1);
	/* submap header table $54c00, 8 bytes each: block map offset, max scroll / 8,
	   trigger and spawn table offsets (graphics.md §3a); count = first trigger offset / 8 */
	fprintf(f, "  \"headers\": [");
	for (i = 0; i < rd2_rw(0x54c04u) / 8; i++)
		fprintf(f, "%s[%u, %u, %u, %u]", i ? ", " : "", rd2_rw(0x54c00u + 8u * (U32)i),
		        rd2_rw(0x54c02u + 8u * (U32)i), rd2_rw(0x54c04u + 8u * (U32)i),
		        rd2_rw(0x54c06u + 8u * (U32)i));
	fprintf(f, "],\n");
	fprintf(f, "  \"exits\": [");
	for (i = 0; i < n; i++)
		fprintf(f, "%s{\"side\": \"%s\", \"row\": %d, \"to\": %d, \"done\": %d, \"tunnel\": %d}",
		        i ? ", " : "", e[i].side == 1 ? "left" : e[i].side == 2 ? "right" : "?",
		        e[i].row, e[i].target, e[i].done, e[i].tunnel);
	fprintf(f, "],\n  \"records\": [");
	for (i = 0, a = 0x167a2u; i < 17; i++, a += 0x58) {
		S16 st = rd2_rws(a);
		if (st <= 0 || i == 5)
			continue;
		fprintf(f, "%s\n    {\"slot\": %d, \"what\": \"%s\", \"state\": %d, \"flags\": %d, "
		        "\"x\": %d, \"y\": %d, \"row\": %d, \"w\": %d, \"h\": %d, \"frame\": %d",
		        first ? "" : ",", i, rec_name(i), st, rd2_rb(a + 1), rd2_rws(a + 2),
		        rd2_rws(a + 6), (scroll8() + rd2_rws(a + 6)) >> 3, rd2_rws(a + 0x26),
		        rd2_rws(a + 0x28), rd2_rws(a + 0xe));
		/* actors: the spawn record they came from (+$2a) and F & $1c, the hit class
		   of update_actor_ai (algo-actors.md §3; $0c: despawns when a box of that
		   record fires) */
		if (i >= 11)
			fprintf(f, ", \"spawn\": %lu, \"hitclass\": %d", (unsigned long)rd2_rl(a + 0x2a),
			        rd2_rb(a + 1) & 0x1c);
		fprintf(f, "}");
		first = 0;
	}
	/* the submap's spawn table (header word 3): 4 bytes + 4 per detail box, a 0 byte
	   ends it (level-tables.md §3); boxes = trigger boxes (algo-actors.md §4) */
	fprintf(f, "%s],\n  \"spawns\": [", first ? "" : "\n  ");
	a = 0x54c00u + rd2_rw(0x54c06u + 8u * (U32)rd2_rw(HL2_SUBMAP));
	for (first = 1; rd2_rb(a) != 0; first = 0) {
		U8 b0 = rd2_rb(a), b2 = rd2_rb(a + 2), b3 = rd2_rb(a + 3);
		int nb = b3 & 3, k;
		fprintf(f, "%s\n    {\"addr\": %lu, \"type\": %d, \"spawned\": %d, \"row\": %d, \"x\": %d, "
		        "\"actor\": %d, \"b3\": %d, \"boxes\": [", first ? "" : ",", (unsigned long)a,
		        b0 & 0x7f, b0 >> 7, rd2_rb(a + 1), (b2 & 0x1f) * 8 + ((b2 & 0x20) ? 4 : 0),
		        b2 >> 7, b3);
		for (k = 0; k < nb; k++) {
			U32 d = a + 4 + 4u * (U32)k;
			fprintf(f, "%s{\"x\": %d, \"row\": %d, \"w\": %d, \"h\": %d, \"mask\": %d, "
			        "\"latched\": %d}", k ? ", " : "", rd2_rb(d), rd2_rb(d + 1),
			        ((rd2_rb(d + 2) & 15) + 1) * 8, ((rd2_rb(d + 2) >> 4) + 1) * 8,
			        rd2_rb(d + 3) & 0x7f, rd2_rb(d + 3) >> 7);
		}
		fprintf(f, "]}");
		a += 4 + 4u * (U32)nb;
	}
	first = 1;
	fprintf(f, "%s],\n  \"tiles\": [", first ? "" : "\n  ");
	for (r = 0; r < hl2_rows(); r++) {
		fprintf(f, "%s\n    \"", r ? "," : "");
		for (c = 0; c < HL2_COLS; c++)
			fputc(tchar(hl2_attr(r, c)), f);
		fprintf(f, "\"");
	}
	fprintf(f, "\n  ],\n  \"legend\": \"rows top down, 32 columns of 8 px; ^ lethal, # solid, "
	        "H ladder, T ladder top, = floor, ~ surface, . empty\"\n}\n");
}

/* eof */
