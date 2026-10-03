/*
 * xrick/src/headless/rd2/hl2_obs.c
 *
 * xrick2-core only (branch `solver`, never shipped): the RD2 state read back out of
 * emulated RAM -- submap tiles, exits, Rick, the record chain (PLAN.md T47 phase 5).
 * Every address comes from kb2/ (cited per item) and the transliterated routines.
 */

#include <stdlib.h>

#include "rd2_mem.h"
#include "rd2_sys.h"
#include "hl2.h"
#include "hl2_obs.h"
#include "hl2_solve.h"
#include "hl2_switch.h"

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
	/* the switches as hl2_switches lists them (nearest first: the index -switch takes)
	   and the exit hl2_solveRoute picks (-1: none) */
	{
		hl2_switch_t sw[32];
		int nsw = hl2_switches(sw, 32);
		fprintf(f, "],\n  \"switches\": [");
		for (i = 0; i < nsw; i++)
			fprintf(f, "%s\n    {\"index\": %d, \"record\": %lu, \"x\": %d, \"row\": %d, \"w\": %d, "
			        "\"h\": %d, \"mask\": %d, \"actor\": %d, \"spawned\": %d, \"fired\": %d}",
			        i ? "," : "", i, (unsigned long)sw[i].rec, sw[i].x, sw[i].row, sw[i].w,
			        sw[i].h, sw[i].mask, sw[i].actor, sw[i].spawned, hl2_switchFired(&sw[i]));
		fprintf(f, "%s],\n  \"route_exit\": %d", nsw ? "\n  " : "", hl2_solveRoute());
	}
	fprintf(f, ",\n  \"tiles\": [");
	for (r = 0; r < hl2_rows(); r++) {
		fprintf(f, "%s\n    \"", r ? "," : "");
		for (c = 0; c < HL2_COLS; c++)
			fputc(tchar(hl2_attr(r, c)), f);
		fprintf(f, "\"");
	}
	fprintf(f, "\n  ],\n  \"legend\": \"rows top down, 32 columns of 8 px; ^ lethal, # solid, "
	        "H ladder, T ladder top, = floor, ~ surface, . empty\"\n}\n");
}

/*
 * view: the tiles around Rick, rows <up> above his feet row to <down> below, with
 * what moves drawn in: R Rick (2 columns, 3 rows standing, 2 crouched), o objects
 * (slots 0-3), * the shot, B the bomb, A-F actors (slots 11-16) over their box
 * (x, y, +$26 w, +$28 h), a-z switch boxes (hl2_switches order: -switch's index)
 * and : boxes Rick sets off by touching them (mask bit 0, records not spawned yet:
 * traps, spawners) where the tile is empty, < > exits in the margins. A legend follows.
 */
static void
put(char *g, int r0, int nr, int row, int col, char ch, int over)
{
	char *p;
	if (row < r0 || row >= r0 + nr || col < 0 || col >= HL2_COLS)
		return;
	p = &g[(row - r0) * HL2_COLS + col];
	if (over || *p == '.')
		*p = ch;
}

void
hl2_view(FILE *f, int up, int down)
{
	hl2_exit_t e[HL2_EXITS_MAX];
	hl2_switch_t sw[32];
	int ne = hl2_exits(e, HL2_EXITS_MAX), nsw = hl2_switches(sw, 32);
	int rr = hl2_rickRow(), rc = hl2_rickCol(), r0 = rr - up, r1 = rr + down, nr, i, r, c, k;
	char *g;
	U32 a;

	if (r0 < 0) r0 = 0;
	if (r1 > hl2_rows() - 1) r1 = hl2_rows() - 1;
	nr = r1 - r0 + 1;
	if (nr <= 0)
		return;
	g = malloc((size_t)nr * HL2_COLS);
	for (r = 0; r < nr; r++)
		for (c = 0; c < HL2_COLS; c++)
			g[r * HL2_COLS + c] = tchar(hl2_attr(r0 + r, c));
	for (i = 0; i < nsw && i < 26; i++)
		for (r = sw[i].row; r < sw[i].row + sw[i].h / 8; r++)
			for (c = sw[i].x / 8; c < (sw[i].x + sw[i].w) / 8; c++)
				put(g, r0, nr, r, c, (char)('a' + i), 0);
	/* touch boxes: the spawn table (header word 3), records not spawned */
	for (a = 0x54c00u + rd2_rw(0x54c06u + 8u * (U32)rd2_rw(HL2_SUBMAP)); rd2_rb(a) != 0;
	     a += 4 + 4u * (U32)(rd2_rb(a + 3) & 3)) {
		if (rd2_rb(a) & 0x80)
			continue;
		for (k = 0; k < (rd2_rb(a + 3) & 3); k++) {
			U32 d = a + 4 + 4u * (U32)k;
			if (!(rd2_rb(d + 3) & 1))
				continue;
			for (r = rd2_rb(d + 1); r < rd2_rb(d + 1) + ((rd2_rb(d + 2) >> 4) + 1); r++)
				for (c = rd2_rb(d) / 8; c < rd2_rb(d) / 8 + (rd2_rb(d + 2) & 15) + 1; c++)
					put(g, r0, nr, r, c, ':', 0);
		}
	}
	for (i = 0, a = 0x167a2u; i < 17; i++, a += 0x58) {
		int x, y, w, h;
		char ch;
		if (rd2_rws(a) <= 0 || i == 5 || (i >= 6 && i <= 9))
			continue;
		x = rd2_rws(a + 2); y = scroll8() + rd2_rws(a + 6);
		w = i >= 11 ? rd2_rws(a + 0x26) : 0; h = i >= 11 ? rd2_rws(a + 0x28) : 0;
		ch = i < 4 ? 'o' : i == 4 ? '*' : i == 10 ? 'B' : (char)('A' + i - 11);
		for (r = y >> 3; r <= (y + (h > 0 ? h - 1 : 0)) >> 3; r++)
			for (c = x >> 3; c <= (x + (w > 0 ? w - 1 : 0)) >> 3; c++)
				put(g, r0, nr, r, c, ch, 1);
	}
	for (r = rr - (rd2_rw(0x12e18u) ? 1 : 2); r <= rr; r++)
		for (c = rc; c < rc + 2; c++)
			put(g, r0, nr, r, c, 'R', 1);

	fprintf(f, "map %u submap %u, Rick feet row %d col %d (x %d), %s%s%s, laser %u bombs %u lives %u\n",
	        rd2_rw(HL2_MAP_PLAYING), rd2_rw(HL2_SUBMAP), rr, rc, rd2_rws(HL2_RICK_X),
	        rd2_rw(0x12e1au) ? "on a ladder" : rd2_rw(0x12e1cu) ? "on the ground" : "in the air",
	        rd2_rw(0x12e18u) ? ", crouched" : "", rd2_rw(0x1697eu) == 1 ? ", facing left" : ", facing right",
	        rd2_rw(HL2_LASER), rd2_rw(HL2_BOMBS), rd2_rw(HL2_LIVES));
	fprintf(f, "     ");
	for (c = 0; c < HL2_COLS; c++) fputc(c % 10 == 0 ? '0' + (c / 10) : ' ', f);
	fprintf(f, "\n     ");
	for (c = 0; c < HL2_COLS; c++) fputc('0' + c % 10, f);
	fputc(10, f);
	for (r = 0; r < nr; r++) {
		char lm = ' ', rm = ' ';
		for (k = 0; k < ne; k++)
			if (e[k].row == r0 + r) {
				if (e[k].side == 1) lm = '<';
				else rm = '>';
			}
		fprintf(f, "%3d %c%.*s%c\n", r0 + r, lm, HL2_COLS, g + r * HL2_COLS, rm);
	}
	free(g);
	for (k = 0; k < ne; k++)
		fprintf(f, "exit %d: %s row %d -> %s%d (entry row %d)%s\n", k, e[k].side == 1 ? "left" : "right",
		        e[k].row, e[k].done ? "map done, " : "submap ", e[k].target, e[k].entry,
		        k == hl2_solveRoute() ? "  <- route" : "");
	for (i = 0, a = 0x167a2u; i < 17; i++, a += 0x58) {
		if (rd2_rws(a) <= 0 || i == 5 || (i >= 6 && i <= 9))
			continue;
		fprintf(f, "%c slot %d %s x %d row %d", i < 4 ? 'o' : i == 4 ? '*' : i == 10 ? 'B' : 'A' + i - 11,
		        i, rec_name(i), rd2_rws(a + 2), (scroll8() + rd2_rws(a + 6)) >> 3);
		if (i >= 11)
			fprintf(f, " %dx%d, spawn %lu, hit class %d", rd2_rws(a + 0x26), rd2_rws(a + 0x28),
			        (unsigned long)rd2_rl(a + 0x2a), rd2_rb(a + 1) & 0x1c);
		fputc(10, f);
	}
	for (i = 0; i < nsw && i < 26; i++)
		fprintf(f, "%c switch %d: box x %d row %d %dx%d, %s%s%s, record %lu%s%s\n", 'a' + i, i,
		        sw[i].x, sw[i].row, sw[i].w, sw[i].h, sw[i].mask & 8 ? "melee " : "",
		        sw[i].mask & 2 ? "shot " : "", sw[i].mask & 4 ? "bomb" : "", (unsigned long)sw[i].rec,
		        sw[i].actor ? (sw[i].spawned ? ", actor out" : ", actor not spawned") : ", trigger",
		        hl2_switchFired(&sw[i]) ? ", FIRED" : "");
	for (a = 0x54c00u + rd2_rw(0x54c06u + 8u * (U32)rd2_rw(HL2_SUBMAP)); rd2_rb(a) != 0;
	     a += 4 + 4u * (U32)(rd2_rb(a + 3) & 3)) {
		if (rd2_rb(a) & 0x80)
			continue;
		for (k = 0; k < (rd2_rb(a + 3) & 3); k++) {
			U32 d = a + 4 + 4u * (U32)k;
			int br = rd2_rb(d + 1), bh = (rd2_rb(d + 2) >> 4) + 1;
			if (!(rd2_rb(d + 3) & 1) || br + bh <= r0 || br > r1)
				continue;
			fprintf(f, ": touch box x %d row %d %dx%d mask %d, record %lu type %d%s\n", rd2_rb(d), br,
			        ((rd2_rb(d + 2) & 15) + 1) * 8, bh * 8, rd2_rb(d + 3) & 0x7f, (unsigned long)a,
			        rd2_rb(a) & 0x7f, rd2_rb(a + 2) & 0x80 ? " (actor)" : "");
		}
	}
}

/* the ST screen last shown (video base [$18edb]:[$18edc], $19234) as a PPM, scaled
   <z> times; palette = the hardware registers as last set (not in a snapshot) */
int
hl2_shot(const char *path, int z)
{
	U32 base = ((U32)rd2_rb(0x18edbu) << 16) | ((U32)rd2_rb(0x18edcu) << 8);
	FILE *f = fopen(path, "wb");
	int x, y, i, j;
	U8 *line;

	if (!f)
		return 0;
	if (z < 1) z = 1;
	line = malloc((size_t)(320 * z * 3));
	fprintf(f, "P6\n%d %d\n255\n", 320 * z, 200 * z);
	for (y = 0; y < 200; y++) {
		for (x = 0; x < 320; x++) {
			U32 ad = base + (U32)y * 160u + (U32)(x >> 4) * 8u;
			U16 m = (U16)(0x8000 >> (x & 15));
			int ci = ((rd2_rw(ad) & m) ? 1 : 0) | ((rd2_rw(ad + 2) & m) ? 2 : 0) |
			         ((rd2_rw(ad + 4) & m) ? 4 : 0) | ((rd2_rw(ad + 6) & m) ? 8 : 0);
			U16 cv = rd2_hw_pal[ci];
			for (i = 0; i < z; i++) {
				U8 *p = line + (size_t)((x * z + i) * 3);
				p[0] = (U8)(((cv >> 8) & 7) * 255 / 7);
				p[1] = (U8)(((cv >> 4) & 7) * 255 / 7);
				p[2] = (U8)((cv & 7) * 255 / 7);
			}
		}
		for (j = 0; j < z; j++)
			fwrite(line, 3, (size_t)(320 * z), f);
	}
	free(line);
	fclose(f);
	return 1;
}

/* eof */
