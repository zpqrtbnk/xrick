/*
 * xrick/src/headless/rd2/hl2_state.h
 *
 * xrick2-core only (branch `solver`, never shipped): snapshot, restore and hash of the
 * RD2 game state, taken between two hl2_step calls (PLAN.md T47 phase 4).
 */

#ifndef _HL2_STATE_H
#define _HL2_STATE_H

#include <stddef.h>

#include "system.h"

extern size_t hl2_stateSize(void);              /* bytes a snapshot takes */
extern void hl2_stateSave(U8 *);                /* copy the state out */
extern void hl2_stateLoad(const U8 *);          /* and back in */
extern unsigned long long hl2_stateHash(void);  /* FNV-1a 64 over every region */
extern unsigned long long hl2_stateKey(void);   /* search key: the regions that affect play */
extern void hl2_stateRender(int);              /* 0: leave the drawing buffers out (search) */
extern int hl2_stateRegions(void);
extern void hl2_stateRegion(int, U32 *, U32 *, const char **);

#endif

/* eof */
