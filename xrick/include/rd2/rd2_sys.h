/*
 * xrick/include/rd2/rd2_sys.h
 *
 * Rick Dangerous 2 -- host bridge of the RAM-model port (port-rd2.md §7): the ST hardware
 * the game touches, done on the host.
 *   - VBL: the ISR $1902e ([$19232] += 1, then the music tick $1a866) runs once per 20 ms
 *     (50 Hz) of host time. It runs inside rd2_sys_pump(), i.e. at the game's own busy-wait
 *     loops ($19216/$191e6 and the others), not between arbitrary instructions as on the ST.
 *   - IKBD: host input becomes the IKBD byte stream, fed to the transliterated ACIA
 *     handler $1a546: joystick 1 as ($ff, byte) packets on change, keys as make codes and
 *     break codes (|$80), no auto-repeat.
 *   - Shifter: palette registers $ff8240.. (rd2_sys_setcolor) and the video base
 *     $ff8201/$ff8203. At every VBL the screen at the base is converted into fb and shown.
 */

#ifndef _RD2_SYS_H
#define _RD2_SYS_H

#include "system.h"

extern U16 rd2_hw_pal[16];   /* $ff8240..$ff825e */

void rd2_sys_init(void);
void rd2_sys_setcolor(U16 i, U16 v);              /* move.w v,$ff8240+2i */
void rd2_sys_setbase(U8 hi, U8 mid);              /* move.b hi,$ff8201 ; move.b mid,$ff8203 */
void rd2_sys_pump(void);                          /* one pass of a busy-wait loop: run due VBLs, input, display */
void rd2_dbg_load(void);                          /* debug map force hook (env RD2_FORCE_MAP) */
void rd2_dbg_frame(void);                         /* debug trace hook (env RD2_TRACE) */
void rd2_sys_hang_red(void);                      /* $11fb8: move.w #$700,$ffff8240 ; bra $11fb8 */
void rd2_sys_joyresync(void);                     /* resend the host joystick at the next pump */

/* demo mode adapter (rd2_demo.c, include/demo.h) */
void rd2_demo_init(void);                         /* -demo / -record */
void rd2_demo_level(void);                        /* level start $10a4e */
void rd2_demo_frame(void);                        /* frame head $10a54 */
void rd2_demo_stop(void);                         /* end of run / back to the title */

void rd2_1a546(U8 b);   /* ACIA handler, one received IKBD byte */
void rd2_1a866(void);   /* TICK: runs in the audio thread, rd2_snd.c */

#endif /* _RD2_SYS_H */

/* eof */
