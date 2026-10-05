/*
 * xrick/src/rd2/rd2_snd.c
 *
 * Rick Dangerous 2 -- sound (P5). The game's own YM2149 engine (sound-ref.md) runs
 * unmodified on the emulated 68000 + YM2149 + MFP of src/audio_engine (the same AtariMachine
 * rd1 uses). Its code, tables and state are uploaded from the PRISTINE program image
 * rd2_program ($19e96-$31eb0, sound-ref.md §1/§9), so the snapshot blob dat_rd2_sndh_engine.c is
 * not used and nothing is patched: from the pristine state the engine is consistent
 * (sound-ref.md §9), and the demo flag it tests is the game's real one.
 *
 * The machine has its own RAM, so the cells the two sides share are copied across. Found
 * 2026-09-24 by listing every absolute-long operand in Ghidra's decoded code: the engine
 * reads $3efb6 (demo flag, $1a70a/$1a7fa, both in the dispatcher) and otherwise only
 * hardware; the game writes $1a5ce ($10b52, S key) and reads $1aa08 ($178f6/$17c56/$17c94).
 *
 * Timing: the VBL ISR's TICK ($1a866) runs at 50 Hz in the audio thread, as rd1 does, instead
 * of inside rd2_sys_pump (sample-accurate playback; rd2_1a866 is therefore empty). Timer A
 * (type-2 samples) is the machine's own MFP, armed by the engine's installer $1a978.
 */

#include <SDL3/SDL.h>

#include "system.h"
#include "rd2_mem.h"
#include "rd2_game.h"
#include "rd2_snd.h"
#include "dat_rd2_program.h"
#include "sysarg.h"
#include "syssnd.h"
#include "audio_engine/AtariMachineC.h"

#define SND_BASE  0x19e96u
#define SND_END   0x31eb0u
#define TRAMP     SND_END          /* moveq #d1,d1 ; jmp $1a6aa (the machine's RAM is empty here) */
#define FREQ      44100
#define MIXSAMPLES 2048

static AtariMachineHandle *machine;
static SDL_AudioStream *stream;
static SDL_Mutex *lock;
static U32 per_tick, countdown;
static U8 uvol = SYSSND_MAXVOL;     /* user volume and mute, as rd1's sndUVol/sndMute (syssnd.c) */
static U8 mute = FALSE;

static void
callback(void *ud, SDL_AudioStream *s, int want, int total)
{
	S16 buf[MIXSAMPLES];
	(void)ud;
	(void)total;
	while (want > 0) {
		U32 n = (U32)want / sizeof(S16), i;
		if (n > MIXSAMPLES)
			n = MIXSAMPLES;
		SDL_LockMutex(lock);
		for (i = 0; i < n; i++) {
			if (--countdown == 0) {
				atari_machine_jsr(machine, 0x1a866u, 0);      /* TICK, the VBL ISR's sound part */
				countdown = per_tick;
			}
			if (mute)
				buf[i] = 0;
			else {
				S32 v = atari_machine_next_sample(machine);
				buf[i] = (S16)((v * (S32)uvol) / SYSSND_MAXVOL);
			}
		}
		SDL_UnlockMutex(lock);
		SDL_PutAudioStreamData(s, buf, (int)(n * sizeof(S16)));
		want -= (int)(n * sizeof(S16));
	}
}

void
rd2_snd_init(void)
{
	SDL_AudioSpec spec;

	if (sysarg_args_vol >= 0)  /* -vol given: 0 (silence) .. SYSSND_MAXVOL */
		uvol = (U8)sysarg_args_vol;
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
		return;
	spec.freq = FREQ;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 1;
	stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, NULL);
	if (!stream)
		return;
	lock = SDL_CreateMutex();
	machine = atari_machine_create(FREQ);
	if (!lock || !machine) {
		SDL_DestroyAudioStream(stream);
		stream = NULL;
		return;
	}
	/* from the pristine image itself, not rd2_ram: sys_init (xrick.c) runs this before
	   rd2_game_run's rd2_mem_init has filled rd2_ram, which then still reads all zero */
	atari_machine_upload(machine, rd2_program + (SND_BASE - RD2_PROG_BASE), SND_BASE, SND_END - SND_BASE);
	atari_machine_mem_write16(machine, RD2_DEMO, rd2_rw(RD2_DEMO));
	/* rd1 measured that the first Jsr() on a fresh machine can misbehave (syssnd.c, T19 P8);
	   the same throwaway call is made here, to the PSG silence routine */
	atari_machine_jsr(machine, 0x1a5d0u, 0);
	atari_machine_jsr(machine, 0x1a978u, 0);                  /* Timer A installer, as the boot code does */
	per_tick = FREQ / 50;
	countdown = per_tick;
	SDL_ResumeAudioStreamDevice(stream);
}

void
rd2_snd_shutdown(void)
{
	if (!stream)
		return;
	SDL_DestroyAudioStream(stream);
	SDL_DestroyMutex(lock);
	atari_machine_destroy(machine);
	stream = NULL;
	machine = NULL;
}

/* F4 / F5 / F6, as rd1's syssnd_toggleMute / syssnd_vol */
void
rd2_snd_toggleMute(void)
{
	if (!machine)
		return;
	SDL_LockMutex(lock);
	mute = !mute;
	SDL_UnlockMutex(lock);
}

void
rd2_snd_vol(S8 d)
{
	if (!machine)
		return;
	if ((d < 0 && uvol > 0) || (d > 0 && uvol < SYSSND_MAXVOL)) {
		SDL_LockMutex(lock);
		uvol = (U8)(uvol + d);
		SDL_UnlockMutex(lock);
	}
}

/* $1a6aa play_sound(d0 = id, d1 = param) */
void
rd2_1a6aa(U16 d0, U16 d1)
{
	U8 tramp[8];
	if (!machine)
		return;
	tramp[0] = 0x72;                                         /* moveq #d1,d1 */
	tramp[1] = (U8)d1;
	tramp[2] = 0x4e;                                         /* jmp (xxx).l */
	tramp[3] = 0xf9;
	tramp[4] = 0x00;
	tramp[5] = 0x01;
	tramp[6] = 0xa6;
	tramp[7] = 0xaa;
	SDL_LockMutex(lock);
	atari_machine_mem_write16(machine, RD2_DEMO, rd2_rw(RD2_DEMO));
	atari_machine_mem_write16(machine, RD2_ID_REMAP, rd2_rw(RD2_ID_REMAP));
	atari_machine_upload(machine, tramp, TRAMP, sizeof(tramp));
	atari_machine_jsr(machine, TRAMP, d0);
	SDL_UnlockMutex(lock);
}

/* $1a5d0 stop all sound */
void
rd2_1a5d0(void)
{
	if (!machine)
		return;
	SDL_LockMutex(lock);
	atari_machine_jsr(machine, 0x1a5d0u, 0);
	SDL_UnlockMutex(lock);
}

/* TICK runs in the audio thread (see the file comment) */
void
rd2_1a866(void)
{
}

/* game-side reads of an engine cell ([$1aa08]) */
U16
rd2_snd_rw(U32 a)
{
	U16 v;
	if (!machine)
		return rd2_rw(a);
	SDL_LockMutex(lock);
	v = (U16)atari_machine_mem_read16(machine, a);
	SDL_UnlockMutex(lock);
	return v;
}

/* eof */
