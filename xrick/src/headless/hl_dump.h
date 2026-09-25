/*
 * xrick/src/headless/hl_dump.h
 *
 * xrick-core only (branch `solver`, never shipped): the RD1 game state as JSON,
 * for the solver and the MCP server (PLAN.md T43 phase 5, kb/demo-solver.md §9).
 */

#ifndef _HL_DUMP_H
#define _HL_DUMP_H

#include <stdio.h>

extern void hl_dump(FILE *);

#endif

/* eof */
