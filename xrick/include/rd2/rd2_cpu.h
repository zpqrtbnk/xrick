/*
 * xrick/include/rd2/rd2_cpu.h
 *
 * Rick Dangerous 2 -- register-level helpers for routines transliterated with their 68000
 * data registers kept as 32-bit C variables (used where the code depends on register
 * width: swap, ext, byte/word writes that keep the upper bits).
 */

#ifndef _RD2_CPU_H
#define _RD2_CPU_H

#include "system.h"

#define RW(r)        ((U16)(r))                                   /* Dn.w */
#define RWS(r)       ((S16)(r))                                   /* Dn.w, signed */
#define RB(r)        ((U8)(r))                                    /* Dn.b */
#define SETW(r, v)   ((r) = ((r) & 0xffff0000u) | (U16)(v))       /* move.w ...,Dn */
#define SETB(r, v)   ((r) = ((r) & 0xffffff00u) | (U8)(v))        /* move.b ...,Dn */
#define SWAP(r)      ((r) = ((r) << 16) | ((r) >> 16))            /* swap Dn */
#define EXTL(r)      ((r) = (U32)(S32)(S16)(r))                   /* ext.l Dn */
#define EXTW(r)      SETW(r, (U16)(S16)(S8)(r))                   /* ext.w Dn */

#endif /* _RD2_CPU_H */

/* eof */
