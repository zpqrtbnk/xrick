/*
 * xrick/src/audio_engine/AtariMachineC.cpp
 *
 * Implementation of the extern "C" wrapper declared in AtariMachineC.h.
 */

#include "AtariMachineC.h"
#include "AtariMachine.h"

struct AtariMachineHandle
{
	AtariMachine machine;
};

AtariMachineHandle *atari_machine_create(unsigned int hostReplayRate)
{
	AtariMachineHandle *h = new AtariMachineHandle();
	h->machine.Startup(hostReplayRate);
	return h;
}

void atari_machine_destroy(AtariMachineHandle *m)
{
	delete m;
}

int atari_machine_upload(AtariMachineHandle *m, const void *src, unsigned int addr, unsigned int size)
{
	return m->machine.Upload(src, addr, size) ? 1 : 0;
}

int atari_machine_jsr(AtariMachineHandle *m, unsigned int addr, unsigned int d0)
{
	return m->machine.Jsr(addr, d0) ? 1 : 0;
}

short atari_machine_next_sample(AtariMachineHandle *m)
{
	return m->machine.ComputeNextSample();
}

void atari_machine_mem_write16(AtariMachineHandle *m, unsigned int addr, unsigned int value)
{
	m->machine.memWrite16(addr, value);
}

/* eof */
