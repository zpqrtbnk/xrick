/*
 * xrick/src/headless/rd2/hl2_sys.c
 *
 * xrick2-core only (branch `solver`, never shipped): the host layer RD2's game code
 * gets from rd2_sys.c and rd2_snd.c, minus video, sound, input and timing (PLAN.md
 * T47 phase 3). The VBL waits do what they leave in RAM at a frame head: $19216
 * flips the screens, $191e6 clears the VBL counter. Sound is silent; the game only
 * reads the engine's [$1aa08] in the title and game over, which xrick2-core never
 * runs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "system.h"
#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_snd.h"
#include "rd2_game.h"
#include "hl2.h"

U16 rd2_hw_pal[16];

/* system.c */
void
sys_printf(char *msg, ...)
{
	va_list argptr;
	va_start(argptr, msg);
	vfprintf(stderr, msg, argptr);
	va_end(argptr);
}

void
sys_panic(char *err, ...)
{
	va_list argptr;
	va_start(argptr, err);
	vfprintf(stderr, err, argptr);
	va_end(argptr);
	exit(1);
}

U32 sys_gettime(void) { return 0; }
void sys_sleep(int s) { (void)s; }

/* rd2_sys.c */
void rd2_sys_init(void) { }
void rd2_sys_setcolor(U16 i, U16 v) { rd2_hw_pal[i & 15] = v; }
void rd2_sys_setbase(U8 hi, U8 mid) { (void)hi; (void)mid; }
void rd2_sys_pump(void) { }
void rd2_dbg_load(void) { }
void rd2_dbg_frame(void) { }
void rd2_sys_joyresync(void) { }
void rd2_sys_paused(U8 on) { (void)on; }
void rd2_sys_info(U8 on) { (void)on; }
U8 rd2_sys_endreq(void) { return 0; }
U8 rd2_sys_pausekey(void) { return 0; }

/* $11fb8: the map 5 load, red border forever (port-rd2.md §7) */
void
rd2_sys_hang_red(void)
{
	hl2_hang();
}

/* $19216: wait for the VBL, then flip */
void
rd2_19216(void)
{
	rd2_19234();
}

/* $19234: toggle bit 7 of $18edc / $18ee0 (the two screen pointers) */
void
rd2_19234(void)
{
	rd2_wb(0x18edc, rd2_rb(0x18edc) ^ 0x80);
	rd2_wb(0x18ee0, rd2_rb(0x18ee0) ^ 0x80);
}

/* $191e6: wait for the VBL, then clear its counter */
void
rd2_191e6(void)
{
	rd2_ww(0x19232, 0);
}

/* rd2_demo.c and sysarg.c: rd2_game_run is linked but never called (hl2_game.c) */
int sysarg_args_mapset = 0;
int sysarg_args_map = 0;
void rd2_demo_init(void) { }
void rd2_demo_level(void) { }
void rd2_demo_frame(void) { }
void rd2_demo_stop(void) { }

/* rd2_snd.c */
void rd2_snd_init(void) { }
void rd2_snd_shutdown(void) { }
void rd2_1a6aa(U16 d0, U16 d1) { (void)d0; (void)d1; }
void rd2_1a5d0(void) { }
void rd2_1a866(void) { }
U16 rd2_snd_rw(U32 a) { return rd2_rw(a); }

/* eof */
