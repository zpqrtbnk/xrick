/*
 * xrick/src/headless/rd2/hl2_obs.h
 *
 * xrick2-core only (branch `solver`, never shipped): what the RD2 game state means,
 * for the solver and the MCP server (PLAN.md T47 phase 5, kb2/demo-solver.md).
 *
 * Coordinates: x = Rick's [$1695c] (0..$e8); a world tile row is counted from the
 * top of the submap, row = ((scroll & ~7) + y) >> 3 for a record's screen y. Rick's
 * "feet row" is the row of y + $14, the row the trigger table compares
 * (level-tables.md §2) and the one his probe reads as the floor (algo-collision.md §2).
 */

#ifndef _HL2_OBS_H
#define _HL2_OBS_H

#include <stdio.h>

#include "system.h"

/* tile attribute bits (rd2_tileattr.h, algo-player.md §10) */
#define HL2_T_SURFACE 0x01
#define HL2_T_SOLID   0x02
#define HL2_T_FLOOR   0x04
#define HL2_T_LADDER  0x08
#define HL2_T_LTOP    0x10
#define HL2_T_LETHAL  0x20

#define HL2_COLS 32

typedef struct {
	int side;     /* 1 left (x clamped at 0), 2 right (x clamped at $e8) */
	int row;      /* feet row */
	int target;   /* submap */
	int entry;    /* b3: Rick's feet row in the target submap */
	int done;     /* completes the map (level-tables.md §2) */
	int tunnel;   /* b0 bit 5 */
	U8 b0;
} hl2_exit_t;

#define HL2_EXITS_MAX 16

extern int hl2_rows(void);                 /* tile rows of the current submap */
extern U8 hl2_attr(int row, int col);       /* tile attribute, 0 off the submap */
extern int hl2_exits(hl2_exit_t *, int max);
extern int hl2_submaps(void);              /* submaps of the loaded map */
extern int hl2_rowsOf(int s);              /* the same for any submap s of the loaded map */
extern U8 hl2_attrOf(int s, int row, int col);
extern int hl2_exitsOf(int s, hl2_exit_t *, int max);
extern int hl2_rickRow(void);              /* feet row */
extern int hl2_rickCol(void);              /* (x + 4) >> 3 */
extern int hl2_rickDead(void);
extern void hl2_dump(FILE *);
extern void hl2_tiles(FILE *, int s);     /* any submap, one numbered row per line */              /* JSON */

#endif

/* eof */
