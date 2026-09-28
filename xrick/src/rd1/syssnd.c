/*
 * xrick/src/syssnd.c
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 *
 * T19 / audio-sndh.md: rewritten to drive the game's real ST sound engine (lifted
 * verbatim into dat_sndh_engine.c) through AtariAudio's AtariMachine emulator
 * (src/audio_engine/, vendored from arnaud-carre/AtariAudio, MIT) instead of mixing
 * WAV buffers. One AtariMachine instance runs for the whole session -- see
 * audio-sndh.md S2 for why SndhRenderer's one-subtune-at-a-time model doesn't fit a
 * game that layers music, SFX and two digidrums through one live engine.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syssnd.h"

#ifdef ENABLE_SOUND

#include "sysarg.h"
#include "game.h"
#include "debug.h"
#include "dat_sndh_engine.h"
#include "audio_engine/AtariMachineC.h"

static U8 isAudioActive = FALSE;

static AtariMachineHandle *machine = NULL;
static U32 samplesPerTick;	/* host samples per 50 Hz engine tick */
static U32 tickCountdown;

static U8 sndUVol = SYSSND_MAXVOL;	/* user-selected volume */
static U8 sndMute = FALSE;		/* mute flag */

static SDL_AudioStream *audio_stream; /* SDL3: owns the device it was opened with */
static SDL_Mutex *sndlock;

/*
 * The D1 trampolines -- play_music()'s calling convention is D0.b=track, D1.b=variant
 * (audio-sndh.md S5), but AtariMachine::Jsr(addr, d0) only ever sets D0. Every call
 * site in the original game uses D1 = 0 or D1 = 1 (audio-sndh.md S7's census; no third
 * value appears anywhere), so two 8-byte stubs cover every case -- D0 is left exactly
 * as Jsr() set it and the stub tail-jumps (not jsr's, so the stack is untouched) into
 * play_music.
 *
 * Both opcodes are verified encodings, not guessed:
 *   7200/7201 = "moveq #0,d1"/"moveq #1,d1" -- re/build_sndh.py's own ENCODINGS table
 *               already cites 0x7200 (as "moveq #0,d1") and a real 0x7201 instance at
 *               atari_ram.bin 0x4C534 ("moveq #1,d1").
 *   4EF9      = "jmp (xxx).l" -- the standard 68000 encoding one bit above JSR (xxx).l,
 *               which re/build_sndh.py's ENCODINGS table already verified as 0x4EB9
 *               (JSR/JMP differ only in bit 6 of the opcode word: 0x4EB9 | 0x0040).
 * Re-verified after this build by disassembling the uploaded bytes with the vendored
 * Musashi core (P4 smoke test) rather than trusted from the derivation alone.
 */
#define TRAMP_D1_0 (SNDH_ENGINE_END)		/* moveq #0,d1 ; jmp play_music */
#define TRAMP_D1_1 (SNDH_ENGINE_END + 8u)	/* moveq #1,d1 ; jmp play_music */

static const U8 trampolines[16] = {
	0x72, 0x00, 0x4E, 0xF9, (SNDH_FN_PLAY >> 24) & 0xFF, (SNDH_FN_PLAY >> 16) & 0xFF,
	(SNDH_FN_PLAY >> 8) & 0xFF, SNDH_FN_PLAY & 0xFF,
	0x72, 0x01, 0x4E, 0xF9, (SNDH_FN_PLAY >> 24) & 0xFF, (SNDH_FN_PLAY >> 16) & 0xFF,
	(SNDH_FN_PLAY >> 8) & 0xFF, SNDH_FN_PLAY & 0xFF,
};

/*
 * SDL3 audio streams are a pull model: instead of handing us a fixed device buffer
 * to fill, the callback is told how many bytes are wanted (additional_amount) and
 * must push exactly that much via SDL_PutAudioStreamData. Generate it through a
 * fixed-size scratch buffer since additional_amount is not bounded by SYSSND_MIXSAMPLES.
 */
static void
syssnd_callback(UNUSED(void *userdata), SDL_AudioStream *stream, int additional_amount, UNUSED(int total_amount))
{
	S16 buf[SYSSND_MIXSAMPLES];
	U32 n, i;

	while (additional_amount > 0) {
		n = (U32)additional_amount / sizeof(S16);
		if (n > SYSSND_MIXSAMPLES)
			n = SYSSND_MIXSAMPLES;

		SDL_LockMutex(sndlock);
		for (i = 0; i < n; i++) {
			if (--tickCountdown == 0) {
				atari_machine_jsr(machine, SNDH_FN_TICK, 0);
				tickCountdown = samplesPerTick;
			}
			if (sndMute) {
				buf[i] = 0;
			}
			else {
				S32 s = atari_machine_next_sample(machine);
				buf[i] = (S16)((s * (S32)sndUVol) / SYSSND_MAXVOL);
			}
		}
		SDL_UnlockMutex(sndlock);

		SDL_PutAudioStreamData(stream, buf, (int)(n * sizeof(S16)));
		additional_amount -= (int)(n * sizeof(S16));
	}
}

void
syssnd_init(void)
{
	SDL_AudioSpec desired;
	char framesbuf[16];

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		IFDEBUG_AUDIO(
			sys_printf("xrick/audio: can not initialize audio subsystem\n");
		);
		return;
	}

	desired.freq = SYSSND_FREQ;
	desired.format = SDL_AUDIO_S16;
	desired.channels = SYSSND_CHANNELS;

	/* SDL3 dropped AudioSpec.samples; the buffer-size equivalent is a hint,
	   which must be set before the device is opened. */
	(void)snprintf(framesbuf, sizeof framesbuf, "%d", SYSSND_MIXSAMPLES);
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, framesbuf);

	/* SDL3's single-call migration path: opens the device, creates a stream in
	   our chosen format (SDL converts to the device's native format itself, so
	   there is no SDL2-style "obtained" spec to read back), and binds them.
	   The device starts paused -- see SDL_ResumeAudioStreamDevice() below. */
	audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, syssnd_callback, NULL);
	if (!audio_stream) {
		IFDEBUG_AUDIO(
			sys_printf("xrick/audio: can not open audio (%s)\n", SDL_GetError());
		);
		return;
	}

#ifndef __EMSCRIPTEN__
	sndlock = SDL_CreateMutex();
	if (sndlock == NULL) {
		IFDEBUG_AUDIO(sys_printf("xrick/audio: can not create lock\n"););
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
		return;
	}
#endif

	if (sysarg_args_vol != 0)
		sndUVol = sysarg_args_vol;

	machine = atari_machine_create((unsigned int)SYSSND_FREQ);
	if (!machine) {
		IFDEBUG_AUDIO(sys_printf("xrick/audio: can not create ST audio engine\n"););
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
		return;
	}
	atari_machine_upload(machine, sndh_engine_blob, SNDH_ENGINE_BASE, sndh_engine_blob_len);
	atari_machine_upload(machine, trampolines, TRAMP_D1_0, sizeof(trampolines));

	/* T19 P8: a freshly-Startup()'d AtariMachine's very first Jsr() can misbehave
	   (measured: reset_sound_chip's first call returns false though harmless;
	   calling a DIFFERENT function first instead segfaults) -- confirmed specific to
	   the first pulse_reset ever executed on an instance, not to any particular
	   target. One throwaway call absorbs it; every call after is reliable, including
	   over a full simulated gameplay session. Discard this result on purpose. */
	atari_machine_jsr(machine, SNDH_FN_RESET, 0);

	atari_machine_jsr(machine, SNDH_FN_RESET, 0);
	/* Our blob is a snapshot of a LIVE, mid-play engine (title music playing), not a
	   clean boot -- reset_sound_chip alone doesn't clear the per-voice envelope/note
	   state that carries over, which rings under the first type-1/2 track played
	   (re/build_sndh.py's own "superimposed ding" fix, same root cause here). */
	atari_machine_jsr(machine, SNDH_FN_SILENCE, 0);
	atari_machine_jsr(machine, SNDH_FN_TIMERA, 0);	/* required for both digidrums */

	samplesPerTick = (U32)SYSSND_FREQ / 50u;
	if (samplesPerTick == 0)
		samplesPerTick = 1;
	tickCountdown = samplesPerTick;

	isAudioActive = TRUE;
	/* SDL3 split SDL2's SDL_PauseAudioDevice(dev, bool) into two functions. */
	SDL_ResumeAudioStreamDevice(audio_stream);

	IFDEBUG_AUDIO(sys_printf("xrick/audio: initialized (%d Hz)\n", SYSSND_FREQ););
}

void
syssnd_shutdown(void)
{
	if (!isAudioActive)
		return;

	SDL_DestroyAudioStream(audio_stream); /* also closes the device it opened */
	SDL_DestroyMutex(sndlock);
	if (machine) {
		atari_machine_destroy(machine);
		machine = NULL;
	}
	isAudioActive = FALSE;
}

void
syssnd_toggleMute(void)
{
	SDL_LockMutex(sndlock);
	sndMute = !sndMute;
	SDL_UnlockMutex(sndlock);
}

void
syssnd_vol(S8 d)
{
	if ((d < 0 && sndUVol > 0) ||
		(d > 0 && sndUVol < SYSSND_MAXVOL)) {
		SDL_LockMutex(sndlock);
		sndUVol += d;
		SDL_UnlockMutex(sndlock);
	}
}

/*
 * Play a track through the live engine, exactly as the original game does:
 * play_music() itself arbitrates the 3 shared PSG voices (audio-sndh.md S2) -- there
 * is no per-sound "channel" here to pick or stop.
 */
void
syssnd_play_track(U8 track, S8 d1)
{
	if (!isAudioActive)
		return;

	SDL_LockMutex(sndlock);
	atari_machine_jsr(machine, d1 != 0 ? TRAMP_D1_1 : TRAMP_D1_0, track);
	SDL_UnlockMutex(sndlock);

	IFDEBUG_AUDIO(sys_printf("xrick/sound: play_music(%d, %d)\n", track, d1););
}

void
syssnd_play(const sound_t *sound)
{
	if (!sound)
		return;
	syssnd_play_track(sound->track, sound->d1);
}

/*
 * Pause
 *
 * pause: TRUE or FALSE
 * clear: TRUE to also silence the engine immediately (reset_sound_chip), not just
 *        stop the SDL callback
 */
void
syssnd_pause(U8 pause, U8 clear)
{
	if (!isAudioActive)
		return;

	if (clear == TRUE) {
		SDL_LockMutex(sndlock);
		atari_machine_jsr(machine, SNDH_FN_RESET, 0);
		SDL_UnlockMutex(sndlock);
	}

	/* SDL3 split SDL2's SDL_PauseAudioDevice(dev, bool) into two functions. */
	if (pause == TRUE)
		SDL_PauseAudioStreamDevice(audio_stream);
	else
		SDL_ResumeAudioStreamDevice(audio_stream);
}

#endif /* ENABLE_SOUND */

/* eof */
