/*
 * xrick/src/headless/rd2/hl2_game.c
 *
 * xrick2-core only (branch `solver`, never shipped): rd2_game_run (src/rd2/rd2_game.c)
 * from PICKED on, as a state machine one frame at a time. The frame itself is the
 * game's rd2_frame_step; boot, LOAD and MAPDONE are copied from rd2_game_run without
 * its host hooks -- keep them in step with it.
 */

#include <setjmp.h>

#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_game.h"
#include "hl2.h"

static int status = HL2_STEP;
static U32 tick;
static jmp_buf hang_jmp;
static int hang_armed;

/* rd2_boot ($10000), minus the host's own init */
void
hl2_boot(void)
{
	rd2_mem_init();
	rd2_ww(0x18ed8, 2);             /* $1901a */
	rd2_19106();                    /* palette $18ee6 */
	rd2_ww(RD2_MAP_PLAYING, 1);
	rd2_123ca();                    /* silent map-1 load */
}

/* LOAD ($10a30-$10a4e) */
static void
load(void)
{
	rd2_19388();                                /* $10a30 */
	rd2_123b0();                                /* $10a36 */
	rd2_17760();                                /* $10a3c */
	rd2_ww(RD2_MAPDONE, 0);                     /* $10a42 */
	rd2_19388();                                /* $10a48 */
	rd2_142a0();                                /* $10a4e */
	tick = 0;
}

void
hl2_hang(void)
{
	if (hang_armed)
		longjmp(hang_jmp, 1);
	for (;;) ;
}

void
hl2_start(int map)
{
	hl2_boot();
	hl2_newgame(map);
}

/* hl2_start after the boot (the write audit starts between the two) */
void
hl2_newgame(int map)
{
	rd2_wb(RD2_JOY, 0);                         /* $10a12 */
	rd2_ww(RD2_PICKER_CHOICE, (U16)map);        /* -map N: as if N were picked */
	rd2_1771c();                                /* $10a24 */
	rd2_123a0();                                /* $10a2a */
	load();
	status = HL2_STEP;
}

/* MAPDONE ($10ad4): HL2_MAP after NEXT's load, else where the run ends */
static int
mapdone(void)
{
	if (rd2_rw(RD2_MAP_PLAYING) == 4) {
		if (rd2_rw(RD2_PICKER_ROWS) != 5) {
			if (rd2_rw(RD2_PICKER_CHOICE) != 1) {
				rd2_17bf4();
				return HL2_END;
			}
			rd2_ww(RD2_PICKER_ROWS, 5);         /* $10af2 */
		}
	} else if (rd2_rw(RD2_MAP_PLAYING) == 5)
		return HL2_END;                         /* never reached: map 5 hangs */
	rd2_ww(RD2_MAP_PLAYING, (U16)(rd2_rw(RD2_MAP_PLAYING) + 1));   /* NEXT $10b1a */
	rd2_17782();
	if (setjmp(hang_jmp)) {
		hang_armed = 0;
		return HL2_HANG;
	}
	hang_armed = 1;
	load();
	hang_armed = 0;
	return HL2_MAP;
}

int
hl2_step(U8 joy)
{
	if (status != HL2_STEP && status != HL2_MAP)
		return status;
	rd2_wb(RD2_JOY, joy);                       /* as rd2_demo_frame plays it */
	tick++;
	switch (rd2_frame_step()) {
	case RD2_STEP_FRAME:
		status = HL2_STEP;
		break;
	case RD2_STEP_MAPDONE:
		status = mapdone();
		break;
	default:                                    /* END_OF_RUN; TITLE/PICK need keys */
		status = HL2_OVER;
		break;
	}
	return status;
}

int hl2_status(void) { return status; }

/* hl2_state.c: the driver's own part of a snapshot */
void hl2_gameGet(int *s, U32 *t) { *s = status; *t = tick; }
void hl2_gameSet(int s, U32 t) { status = s; tick = t; }
U32 hl2_tick(void) { return tick; }

/* eof */
