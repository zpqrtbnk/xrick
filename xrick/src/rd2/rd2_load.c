/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

/*
 * Rick Dangerous 2 -- load_map_if_changed ($123b0) and the spawn re-arm $144d6/$14500
 * (algo-flow.md §13, algo-spawn.md §9; disassembly read 2026-09-24).
 *
 * The disk part is replaced: the FAT loader $11e76 brings the demo file to $3efc0 and the
 * packed level file to $65300, and $1795c depacks $65300 into $53400. The port copies the
 * files it embeds instead:
 *   - demo: dat_rd2_demo.c. The snapshot's $3efc0 holds map1_demo.bin byte for byte.
 *   - $65300: stage1 (dat_rd2_levelimg.c), as graphics.md §3 measured it after load.
 *   - $53400: the depacked level image (dat_rd2_levelimg.c).
 * Map 5: the demo file descriptor $11e5e = (0013, 0002) sums to $15, which is not among the
 * sums $11f86 accepts, so the first load never returns ($11fb8: red border, forever).
 * The loader's MFP save/restore and the obfuscated tail are hardware; their one RAM
 * effect is clr.b [$1a4fb] ($11f7a, and the tail per algo-flow.md §8/§13).
 */

#include <string.h>

#include "rd2_mem.h"
#include "rd2_game.h"
#include "rd2_sys.h"
#include "dat_rd2_demo.h"
#include "dat_rd2_levelimg.h"

/* $14500: clear the spawned bit of every record of submap d0's spawn table */
static void
rd2_14500(U16 d0)
{
	U32 a0 = 0x54c00u + (U32)rd2_extw((U16)(d0 << 3));     /* lsl.w #3 ; adda.w */
	a0 = 0x54c00u + (U32)rd2_extw(rd2_rw(a0 + 6));          /* adda.w */
	while (rd2_rb(a0) != 0) {
		rd2_wb(a0, rd2_rb(a0) & 0x7f);          /* bclr #7,(a0) */
		a0 += (U32)((rd2_rb(a0 + 3) & 3) << 2);
		a0 += 4;
	}
}

/* $144d6: N = word[$144cc + 2*([$1239c]-1)]; dbf: submaps N-1 .. 0 */
static void
rd2_144d6(void)
{
	S16 d0 = rd2_rws(0x144ccu + (U32)rd2_extw((U16)((rd2_rw(RD2_MAP_PLAYING) - 1) * 2)));
	while (--d0 != -1)
		rd2_14500((U16)d0);
}

/* $123ca: loader past its banner (also the boot's silent map-1 load) */
void
rd2_123ca(void)
{
	S16 d0;

	rd2_1a5d0();
	d0 = rd2_rws(RD2_MAP_PLAYING);
	rd2_ww(RD2_MAP_LOADED, (U16)d0);
	d0 -= 1;
	if (d0 > 4)
		d0 = 4;
	if (d0 == 4)
		rd2_sys_hang_red();                     /* map 5: $11f86 -> $11fb8 */

	/* the port has only maps 1..4 (d0 0..3) past this point */
	memcpy(rd2_ram + 0x3efc0u, rd2_demo[d0], RD2_DEMO_BYTES);
	memcpy(rd2_ram + 0x65300u, rd2_stage1[d0], rd2_stage1_size[d0]);
	memcpy(rd2_ram + 0x53400u, rd2_levelimg[d0], RD2_LEVELIMG_SIZE);
	rd2_wb(0x1a4fb, 0);                         /* $11f7a / loader tail */
}

/* $123b0 */
void
rd2_123b0(void)
{
	if (rd2_rw(RD2_MAP_PLAYING) == rd2_rw(RD2_MAP_LOADED)) {
		rd2_144d6();                            /* $123be */
		return;
	}
	rd2_194ce(3);
	rd2_123ca();
}

/* eof */
