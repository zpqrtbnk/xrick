/*
 * xrick/include/rd2/rd2_mem.h
 *
 * Rick Dangerous 2 -- the emulated 68000 address space of the RAM-model port (port-rd2.md §7).
 *
 * Every game routine is transliterated onto these accessors at the ORIGINAL addresses
 * (kb2 algo-*.md, prg2-ram.bin / pristine program numbering), so word/byte overlaps, pointers
 * into the program or the level image, and runtime bits written into data all behave as
 * on the ST. Big-endian, like the 68000. Addresses are masked to the 1 MB of RAM the game
 * uses ($0..$fffff); hardware registers ($ff8240 palette, $ff8800 PSG, $fffa.. MFP) are
 * NOT in here -- the transliterated code calls the host bridge (rd2_sys.h) for those.
 */

#ifndef _RD2_MEM_H
#define _RD2_MEM_H

#include "system.h"

#define RD2_RAM_SIZE 0x100000u
#define RD2_RAM_MASK 0x0fffffu

extern U8 rd2_ram[RD2_RAM_SIZE];

static inline U8  rd2_rb(U32 a) { return rd2_ram[a & RD2_RAM_MASK]; }
static inline U16 rd2_rw(U32 a) { return (U16)((rd2_ram[a & RD2_RAM_MASK] << 8) | rd2_ram[(a + 1) & RD2_RAM_MASK]); }
static inline U32 rd2_rl(U32 a) { return ((U32)rd2_rw(a) << 16) | rd2_rw(a + 2); }
static inline S8  rd2_rbs(U32 a) { return (S8)rd2_rb(a); }
static inline S16 rd2_rws(U32 a) { return (S16)rd2_rw(a); }
static inline S32 rd2_rls(U32 a) { return (S32)rd2_rl(a); }
#ifdef HL2_WATCH
/* xrick2-audit only (branch `solver`, PLAN.md T47 phase 1): mark every byte written */
extern U8 hl2_wmap[RD2_RAM_SIZE];
static inline void rd2_wb(U32 a, U8 v)  { hl2_wmap[a & RD2_RAM_MASK] = 1; rd2_ram[a & RD2_RAM_MASK] = v; }
static inline void rd2_ww(U32 a, U16 v) { hl2_wmap[a & RD2_RAM_MASK] = 1; hl2_wmap[(a + 1) & RD2_RAM_MASK] = 1;
                                          rd2_ram[a & RD2_RAM_MASK] = (U8)(v >> 8); rd2_ram[(a + 1) & RD2_RAM_MASK] = (U8)v; }
#else
static inline void rd2_wb(U32 a, U8 v)  { rd2_ram[a & RD2_RAM_MASK] = v; }
static inline void rd2_ww(U32 a, U16 v) { rd2_ram[a & RD2_RAM_MASK] = (U8)(v >> 8); rd2_ram[(a + 1) & RD2_RAM_MASK] = (U8)v; }
#endif
static inline void rd2_wl(U32 a, U32 v) { rd2_ww(a, (U16)(v >> 16)); rd2_ww(a + 2, (U16)v); }

/* sign-extension helpers mirroring ext.w / ext.l */
static inline S16 rd2_extb(U8 b)  { return (S16)(S8)b; }
static inline S32 rd2_extw(U16 w) { return (S32)(S16)w; }

/*
 * Load the pristine program image at $f8b8 (dat_rd2_program.c). Everything else starts
 * at 0, as the boot code leaves $53400-$7ffff after $18fbe (kb2/algo-flow.md §13).
 * Checked 2026-09-24 with Ghidra search_instructions over all 7707 decoded instructions:
 * the only absolute operands below $f8b8 are $43e (in the $7000 disk loader, which the
 * port replaces) and hardware registers. No absolute operand lands in $3efd4-$533ff, and
 * demo data is loaded at $3efc0. UNVERIFIED: code that Ghidra has not decoded. A raw byte
 * scan found only candidate hits that were not classified.
 */
void rd2_mem_init(void);

#endif /* _RD2_MEM_H */

/* eof */
