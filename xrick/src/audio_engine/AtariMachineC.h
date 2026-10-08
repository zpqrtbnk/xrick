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
 * Thin extern "C" wrapper around AtariAudio's AtariMachine class (audio-sndh.md S8),
 * so the port's plain-C syssnd.c can drive it. Not part of the vendored library --
 * the only hand-written glue this integration needs.
 */

#ifndef _ATARI_MACHINE_C_H
#define _ATARI_MACHINE_C_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AtariMachineHandle AtariMachineHandle;

/* Creates and Startup()s a machine at the given host sample rate (e.g. 44100). */
AtariMachineHandle *atari_machine_create(unsigned int hostReplayRate);
void atari_machine_destroy(AtariMachineHandle *m);

/* Copies size bytes into emulated RAM at addr. Returns 1 on success, 0 on failure. */
int atari_machine_upload(AtariMachineHandle *m, const void *src, unsigned int addr, unsigned int size);

/* Calls the 68000 routine at addr with D0=d0, waits for its rts. 1=ok, 0=crashed/timed out. */
int atari_machine_jsr(AtariMachineHandle *m, unsigned int addr, unsigned int d0);

/* Advances the YM2149/MFP emulation by one host sample; ticks any fired MFP timer ISR. */
short atari_machine_next_sample(AtariMachineHandle *m);

/* Direct RAM poke -- used to pass play_music's D1 around Jsr()'s single-register limit
   (audio-sndh.md S5's scratch-cell trampoline). */
void atari_machine_mem_write16(AtariMachineHandle *m, unsigned int addr, unsigned int value);

/* Direct RAM peek (rd2: the game reads engine state cells such as [$1aa08]). */
unsigned int atari_machine_mem_read16(AtariMachineHandle *m, unsigned int addr);

#ifdef __cplusplus
}
#endif

#endif /* _ATARI_MACHINE_C_H */

/* eof */
