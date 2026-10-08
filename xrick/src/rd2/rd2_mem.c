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
 * Rick Dangerous 2 -- emulated RAM of the RAM-model port (port-rd2.md §7).
 */

#include <string.h>

#include "rd2_mem.h"
#include "dat_rd2_program.h"

U8 rd2_ram[RD2_RAM_SIZE];

void
rd2_mem_init(void)
{
	memset(rd2_ram, 0, sizeof(rd2_ram));
	memcpy(rd2_ram + RD2_PROG_BASE, rd2_program, RD2_PROG_SIZE);
}

/* eof */
