/*
 * xrick/src/rd2/rd2_player.c
 *
 * Rick Dangerous 2 -- Rick, the laser shot, the bomb and the input reader, transliterated
 * at register level from the disassembly ($13096-$13e02, $13e14-$14220; read 2026-09-24;
 * kb2/algo-player.md). d0 = input byte (low byte), d1 = y as 16.16 (swapped as in the
 * original), d7 = probe result byte. Labels are the original addresses.
 */

#include "rd2_mem.h"
#include "rd2_cpu.h"
#include "rd2_game.h"
#include "rd2_collide.h"

#define RX     0x1695cu   /* Rick x */
#define RY     0x16960u   /* Rick y (long with the fraction at $16962) */
#define RVY    0x16966u
#define RFRAME 0x16968u
#define RWALK  0x1697cu
#define RFACE  0x1697eu
#define MAP    rd2_rw(RD2_MAP_PLAYING)

#define CROUCH   0x12e18u
#define LADDER   0x12e1au
#define ONFLOOR  0x12e1cu
#define TUNNEL   0x12e14u

static void snd(U16 id) { rd2_1a6aa(id, 0); }

/* $141cc read_player_input: live -> [$1a4fb]; demo -> the (count, state) stream */
static U32
rd2_141cc(U32 d0)
{
	U32 a0;
	if (rd2_rw(RD2_DEMO) == 0) {
		SETB(d0, rd2_rb(RD2_JOY));
		return d0;
	}
	a0 = rd2_rl(0x3efba);
	if (rd2_rb(0x3efbe) == 0) {
		SETB(d0, rd2_rb(a0));
		a0++;
		if (RB(d0) == 0) {
			rd2_ww(RD2_DEMO_ENDED, 0xffff);
			return d0;
		}
		rd2_wb(0x3efbe, RB(d0));
		rd2_wb(0x3efbf, rd2_rb(a0));
		a0++;
		rd2_wl(0x3efba, a0);
	}
	rd2_wb(0x3efbe, (U8)(rd2_rb(0x3efbe) - 1));
	SETB(d0, rd2_rb(0x3efbf));
	return d0;
}

/* $13d58 fire the laser */
static void
rd2_13d58(void)
{
	snd(0x10);
	rd2_ww(0x16902, 1);
	rd2_ww(0x176f4, (U16)(rd2_rw(0x176f4) - 1));
	rd2_ww(0x176f2, 0xffff);
	rd2_ww(0x16908, (U16)(rd2_rw(RY) + 7));
	if (rd2_rw(RFACE) == 0) {
		rd2_ww(0x16926, 8);
		rd2_ww(0x16904, rd2_rw(RX));
		rd2_ww(0x12e26, 0xffff);
		rd2_ww(0x16910, 0x1f);
		if (rd2_rw(TUNNEL) != 0)
			rd2_ww(0x16910, 0x92);
	} else {
		rd2_ww(0x16926, 0xfff8);
		rd2_ww(0x16904, rd2_rw(RX));
		rd2_ww(0x12e26, 0xffff);
		rd2_ww(0x16910, 0x1e);
		if (rd2_rw(TUNNEL) != 0)
			rd2_ww(0x16910, 0x93);
	}
}

/* $13d0a fire in the tunnel */
static void
rd2_13d0a(U32 d0)
{
	if (!(d0 & 0x80)) {
		rd2_ww(0x12e26, 0);
		return;
	}
	rd2_ww(0x12e24, 1);
	if (rd2_rw(0x12e26) != 0)
		return;
	if (rd2_rw(0x176f4) == 0) {
		snd(0x30);
		rd2_ww(0x12e26, 1);
		return;
	}
	if (rd2_rw(0x16902) != 0)
		return;
	rd2_13d58();
}

static void
probe(void)
{
	if (rd2_rw(CROUCH) != 0)
		rd2_15f1e();
	else
		rd2_15fba();
}

/* $13096 update_player_rick */
void
rd2_13096(void)
{
	U32 d0 = 0, d1 = 0, d2 = 0, d5 = 0, d6 = 0, a0 = 0;
	U8 d7 = 0;

	rd2_ww(0x12e10, rd2_rw(RY));
	rd2_ww(0x12e12, rd2_rw(RX));
	rd2_ww(0x14360, 0);
	rd2_ww(0x12e2e, 0);
	d0 = rd2_141cc(d0);
	if (rd2_rw(0x12ef4) != 0)
		rd2_ww(0x12ef4, (U16)(rd2_rw(0x12ef4) - 1));
	if (RB(d0) != rd2_rb(0x12ef2)) {
		rd2_wb(0x12ef2, 0);
		rd2_ww(0x12ef4, 0);
	}
	if (rd2_rw(0x12e2a) != 0) goto L13ace;
	if (rd2_rw(0x12e2c) != 0) goto L13a62;
	if (rd2_rw(TUNNEL) != 0) goto L13b84;
	if (rd2_rw(LADDER) != 0) goto L138c6;
	rd2_ww(0x12e1c, 0);
	rd2_ww(0x12e1e, 0);
	rd2_ww(0x12e20, 0);
	rd2_ww(0x12e22, 0);
	rd2_ww(0x12e24, 0);
	rd2_ww(0x12e28, 0);
	SETW(d1, rd2_rw(RVY) + 0x80);
	if (RWS(d1) > 0x800) SETW(d1, 0x800);
	rd2_ww(RVY, RW(d1));
	rd2_ww(RD2_PVY, RW(d1));
	EXTL(d1);
	d1 <<= 8;
	d1 += rd2_rl(RY);
	SWAP(d1);
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(RD2_PX, rd2_rw(RX));
	rd2_ww(RD2_PACT, 0xffff);
	probe();

L13194:
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20) goto L13a62;
	if (!(d7 & 0x40)) goto L1321e;
	d1 = 0;                                                  /* standing on an actor */
	SETW(d1, rd2_rw(RD2_PPLY) - 0x15);
	rd2_ww(RD2_PY, RW(d1));
	SWAP(d1);
	rd2_wl(RY, d1);
	SWAP(d1);
	rd2_ww(ONFLOOR, 1);
	rd2_ww(0x12e2e, rd2_rw(RD2_PPDX));
	rd2_ww(RD2_PACT, 0);
	probe();
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20) goto L13a62;
	d7 &= 0x06;
	if (d7 != 0) {
		d7 = rd2_rb(RD2_PRES);
		goto L13234;
	}
	d7 = rd2_rb(RD2_PRES);
	SETW(d6, 0x100);
	goto L13348;

L1321e:
	if (d7 & 0x04) goto L1327a;
	if (!(d7 & 0x02)) goto L13476;
	if (rd2_rws(RVY) >= 0) goto L1327a;
L13234:                                                      /* head */
	if (rd2_rw(LADDER) != 0) {
		SETW(d5, rd2_rw(RX));
		SETW(d5, RW(d5) & 0x1f);
		if (RWS(d5) < 0x0a) {
			SETW(d5, rd2_rw(RX));
			SETW(d5, RW(d5) & 0xf0);
			SETW(d5, RW(d5) | 4);
			rd2_ww(RX, RW(d5));
		}
	}
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) + 1);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
	rd2_ww(RVY, 0x100);
	goto L13476;

L1327a:                                                      /* floor */
	if ((d7 & 0x10) && rd2_rw(CROUCH) == 0) {
		if (rd2_rw(LADDER) != 0) goto L13476;
		if (!(d0 & 0x80) && (d0 & 0x02)) {
			SETW(d2, rd2_rw(RX));
			SETW(d2, RW(d2) & 0x1f);
			if (RWS(d2) < 0x0a) {
				SETW(d2, rd2_rw(RX));
				SETB(d2, RB(d2) & 0xf0);
				SETB(d2, RB(d2) | 4);
				rd2_ww(RX, RW(d2));
				rd2_ww(LADDER, 1);
				goto L13476;
			}
		}
	}
	rd2_ww(LADDER, 0);
	rd2_ww(ONFLOOR, 1);
	SETW(d1, RW(d1) + 0x0c);
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) - 0x14);
	SWAP(d1);
	SETW(d1, 0);
	rd2_wl(RY, d1);                                          /* d1 stays swapped (no swap back) */
	SETW(d6, 0x100);
	if ((d7 & 0x01) && MAP == 3) {
		SETW(d6, rd2_rw(RVY));
		if (RWS(d6) >= 0x200) {
			snd(0x19);
			SETW(d6, (U16)(-RWS(d6)));
			SETW(d6, RW(d6) + 0x60);
			rd2_ww(RVY, RW(d6));
			SETW(d6, 0xf781);                                /* -$87f */
			goto L13390;
		}
		rd2_ww(RVY, 0x100);
		SETW(d6, 0xfa00);                                    /* -$600 */
		goto L13390;
	}
L13348:
	rd2_ww(RVY, RW(d6));
	SETW(d6, 0xfa00);
	if (d7 & 0x01) {
		if (MAP == 1) {
			SETW(d6, 0xfe00);                                /* -$200 */
			goto L13390;
		}
		if (MAP == 5) {
			snd(0x1a);
			rd2_ww(RVY, 0xf781);
			goto L134ac;
		}
	}
L13390:
	if (rd2_rw(CROUCH) != 0) {
		if (rd2_rw(0x12e16) != 0) goto L134ac;
		rd2_ww(CROUCH, 0);
		goto L133da;
	}
	if (d0 & 0x80) goto L133ee;
	if (!(d0 & 0x01)) goto L133da;
	rd2_ww(RVY, RW(d6));                                     /* jump */
	if (d7 & 0x10) goto L134ac;
	if (!(d7 & 0x08)) goto L134ac;
	rd2_ww(LADDER, 1);
	goto L134ac;
L133da:
	if (!(d0 & 0x02)) goto L134ac;
	rd2_ww(CROUCH, 1);
	goto L134ac;

L133ee:                                                      /* fire held */
	if (d0 & 0x01) goto L13912;
	rd2_ww(0x12e26, 0);
	if (d0 & 0x02) goto L13976;
	if (RB(d0) == rd2_rb(0x12ef2)) goto L1394e;
	if (d0 & 0x04) {
		rd2_ww(RFACE, 1);
		rd2_ww(0x12ef4, 0x0a);
		rd2_wb(0x12ef2, RB(d0));
		snd(0x2d);
		goto L1394e;
	}
	if (!(d0 & 0x08)) goto L134ac;
	rd2_ww(RFACE, 0);
	rd2_ww(0x12ef4, 0x0a);
	rd2_wb(0x12ef2, RB(d0));
	snd(0x2d);
	goto L1394e;

L13476:
	SWAP(d1);
	rd2_wl(RY, d1);
	if (rd2_rw(CROUCH) != 0 && rd2_rw(0x12e16) == 0)
		rd2_ww(CROUCH, 0);
	SETB(d1, RB(d0) & 3);
	if (RB(d1) != 0 && (d7 & 0x08))
		rd2_ww(LADDER, 1);

L134ac:
	if (rd2_rw(LADDER) != 0 && !(d7 & 0x08)) {
		rd2_ww(LADDER, 0);
		if (rd2_rws(RVY) < 0)
			rd2_ww(RVY, 0xfe00);
	}
	if (d0 & 0x04) {
		rd2_ww(RFACE, 1);
		SETW(d6, 0xfffe);
		if (d7 & 0x01) {
			if (MAP == 4) {
				SETW(d6, 0xffff);
				rd2_ww(0x12e20, 1);
			} else if (MAP == 2)
				rd2_ww(0x12e22, 1);
		}
	} else if (d0 & 0x08) {
		rd2_ww(RFACE, 0);
		d6 = 2;
		if (d7 & 0x01) {
			if (MAP == 4) {
				d6 = 1;
				rd2_ww(0x12e20, 1);
			} else if (MAP == 2)
				rd2_ww(0x12e22, 1);
		}
	} else {
		d6 = 0;
		if (!(d7 & 0x01) || MAP != 2)
			rd2_ww(RWALK, 0xffff);
		else {
			rd2_ww(0x12e1e, 1);
			d6 = 2;
			if (rd2_rw(RFACE) != 0)
				SETW(d6, 0xfffe);
		}
	}
	SETW(d6, RW(d6) + rd2_rw(0x12e2e));
	SETW(d2, rd2_rw(RX));
	SETW(d2, RW(d2) + RW(d6));
	if (RWS(d6) != 0) {
		if (RWS(d6) < 0) {
			if (RWS(d2) < 0) {
				d2 = 0;
				rd2_ww(0x14360, 1);
			}
		} else if (RWS(d2) >= 0xe8) {
			SETW(d2, 0xe8);
			rd2_ww(0x14360, 2);
		}
	}
	rd2_ww(RD2_PY, rd2_rw(RY));
	rd2_ww(RD2_PVY, 0xffff);
	rd2_ww(RD2_PX, RW(d2));
	rd2_ww(RD2_PACT, 0xffff);
	probe();
L13606:
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20) goto L13a62;
	if (d7 & 0x02) {                                         /* wall */
		if (RWS(d6) < 0)
			SETW(d2, RW(d2) + 0x0c);
		else
			SETW(d2, RW(d2) + 0x03);
		SETB(d2, RB(d2) & 0xf8);
		SETW(d2, RW(d2) - 4);
		rd2_ww(RX, RW(d2));
	} else {
		rd2_ww(RX, RW(d2));
		if (rd2_rw(LADDER) != 0 && !(d7 & 0x08))
			rd2_ww(LADDER, 0);
	}

L13662:                                                      /* frame choice */
	if (rd2_rw(TUNNEL) != 0) {
		rd2_ww(RFRAME, 0x0c);
		rd2_13d0a(d0);
		goto L138b0;
	}
	if (rd2_rw(LADDER) != 0) {
		if (rd2_rw(RX) != rd2_rw(0x12e12) || rd2_rw(RY) != rd2_rw(0x12e10)) {
			rd2_ww(0x12e30, (U16)((rd2_rw(0x12e30) + 1) & 3));
			if (rd2_rw(0x12e30) == 0)
				snd(0x31);
		}
		rd2_ww(RFRAME, 0x1a);
		if ((rd2_rw(RX) ^ rd2_rw(RY)) & 4)
			rd2_ww(RFRAME, 0x1b);
		goto L13a4e;
	}
	if (rd2_rw(ONFLOOR) == 0) {
		rd2_ww(RFRAME, 8);
		if (rd2_rw(CROUCH) != 0)
			rd2_ww(RFRAME, 6);
		goto L138b0;
	}
	if (rd2_rw(0x12ef4) != 0) {                              /* melee */
		if (rd2_rw(RFACE) != 0) {
			rd2_ww(0x12ef6, rd2_rw(RX));
			rd2_ww(0x12ef8, (U16)(rd2_rw(RY) + 8));
			rd2_ww(RFRAME, 0x17);
		} else {
			rd2_ww(0x12ef6, (U16)(rd2_rw(RX) + 0x18));
			rd2_ww(0x12ef8, (U16)(rd2_rw(RY) + 8));
			rd2_ww(RFRAME, 0x0a);
		}
		goto L13a4e;
	}
	if (rd2_rw(0x12e24) != 0) {
		rd2_ww(RFRAME, 9);
		goto L138b0;
	}
	if (rd2_rw(0x12e28) != 0) {
		rd2_ww(RFRAME, 0);
		if (rd2_rw(0x16b36) != 0)
			rd2_ww(RFRAME, 0x0b);
		goto L138b0;
	}
	if (rd2_rw(0x12e1e) != 0) {
		rd2_ww(RFRAME, 6);
		if (rd2_rw(CROUCH) != 0) goto L138b0;
		a0 = 0x12e4cu;
		d1 = 3;
		d2 = 0x28;
		goto L13844;
	}
	if (rd2_rb(0x12ef2) != 0) {
		rd2_ww(RFRAME, 0);
		goto L138b0;
	}
	a0 = 0x12e56u;
	d2 = 0x20;
	rd2_ww(RFRAME, 6);
	if (rd2_rw(CROUCH) == 0) {
		a0 = 0x12e42u;
		d2 = 0x28;
		rd2_ww(RFRAME, 0);
	}
	if (rd2_rw(RWALK) == 0xffff) {
		rd2_ww(RWALK, 0);
		goto L138b0;
	}
	d1 = 1;
	if (rd2_rw(0x12e20) == 0) {
		d1 = 4;
		if (rd2_rw(0x12e22) == 0)
			d1 = 2;
	}
L13844:
	SETW(d1, RW(d1) + rd2_rw(RWALK));
	if (!(RWS(d1) < RWS(d2)))
		SETW(d1, RW(d1) - RW(d2));
	rd2_ww(RWALK, RW(d1));
	SETW(d1, RW(d1) >> 2);                                   /* lsr.w #2 */
	if (rd2_rw(CROUCH) != 0) {
		if (d1 & 1)
			snd(0x32);
	} else if (RW(d1) == 2 || RW(d1) == 5 || RW(d1) == 7 || RW(d1) == 0)
		snd(0x31);
	a0 += (U32)(S32)RWS(d1);
	rd2_ww(RFRAME, rd2_rb(a0));
L138b0:
	if (rd2_rw(RFACE) != 0)
		rd2_ww(RFRAME, (U16)(rd2_rw(RFRAME) + 0x0d));
L13a4e:
	rd2_ww(0x1662c, (U16)(rd2_rw(RY) - rd2_rw(0x12e10)));
	return;

L138c6:                                                      /* on a ladder */
	rd2_wb(0x1695d, rd2_rb(0x1695d) & 0xfe);
	d1 = rd2_rw(RY);
	d6 = 0;
	if (d0 & 0x01)
		SETW(d6, 0xfffe);
	else if (d0 & 0x02)
		d6 = 2;
	rd2_ww(RVY, RW(d6));
	SETW(d1, RW(d1) + RW(d6));
	rd2_ww(RD2_PVY, RW(d6));
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(RD2_PX, rd2_rw(RX));
	rd2_15fba();                                             /* [$15f16] inherited */
	goto L13194;

L13912:                                                      /* fire + up: laser */
	rd2_ww(0x12e24, 1);
	if (rd2_rw(0x12e26) != 0) goto L1394e;
	if (rd2_rw(0x176f4) == 0) {
		snd(0x30);
		rd2_ww(0x12e26, 1);
		goto L1394e;
	}
	if (rd2_rw(0x16902) == 0) {
		rd2_13d58();
		goto L13662;
	}
L1394e:
	if (!(d7 & 0x01)) goto L13662;
	if (MAP != 2) goto L13662;
	rd2_ww(0x12e1e, 1);
	goto L134ac;

L13976:                                                      /* fire + down: bomb */
	rd2_ww(0x12e28, 1);
	if (rd2_rw(0x17702) == 0) goto L1394e;
	if (rd2_rw(0x16b12) != 0) goto L1394e;
	rd2_ww(0x16b12, 1);
	rd2_ww(0x17702, (U16)(rd2_rw(0x17702) - 1));
	rd2_ww(0x17700, 0xffff);
	rd2_ww(0x16b14, rd2_rw(RX));
	rd2_ww(0x16b16, 0);
	rd2_ww(0x16b18, (U16)(rd2_rw(RY) + 5));
	rd2_ww(0x16b1a, 0);
	rd2_ww(0x16b1e, 0x100);
	rd2_ww(0x16b34, 0);
	rd2_wl(0x16b30, 0x12e5eu);
	rd2_ww(0x16b36, 0);
	if (d0 & 0x04) {
		rd2_ww(RFACE, 1);
		rd2_ww(0x16b36, 0xfe00);
		snd(0x32);
		goto L13662;
	}
	if (!(d0 & 0x08)) goto L13662;
	rd2_ww(RFACE, 0);
	rd2_ww(0x16b36, 0x200);
	snd(0x32);
	goto L13662;

L13a62:                                                      /* death */
	snd(0x0a);
	rd2_ww(0x12e2c, 0);
	rd2_ww(0x12e2a, 0xffff);
	rd2_ww(0x17710, (U16)(rd2_rw(0x17710) - 1));
	rd2_ww(0x1770e, 0xffff);
	rd2_ww(0x1696c, 0);
	rd2_ww(RWALK, 0);
	rd2_ww(0x16962, 0);
	rd2_ww(RVY, 0xfb00);
	rd2_ww(RFACE, 2);
	if (rd2_rws(RX) >= 0x74)
		rd2_ww(RFACE, 0xfffe);
L13ace:                                                      /* dead: fall */
	SETW(d1, rd2_rw(RVY) + 0x80);
	if (RWS(d1) > 0x800) SETW(d1, 0x800);
	rd2_ww(RVY, RW(d1));
	EXTL(d1);
	d1 <<= 8;
	d1 += rd2_rl(RY);
	rd2_wl(RY, d1);
	rd2_ww(RX, (U16)(rd2_rw(RX) + rd2_rw(RFACE)));
	SETW(d1, rd2_rw(RWALK));
	SETB(d1, (RB(d1) + 1) & 3);
	rd2_ww(RWALK, RW(d1));
	SETB(d1, RB(d1) >> 1);
	if (rd2_rws(RVY) < 0) {
		SETW(d1, RW(d1) + 0x1c);
		rd2_ww(RFRAME, RW(d1));
		rd2_ww(0x1696e, 0xffff);
		return;
	}
	{
		U32 a6;
		S16 n;
		SETB(d1, RB(d1) << 2);
		SETW(d1, RW(d1) + 0x95);
		a0 = 0x13e04u;
		for (a6 = 0x169b2u, n = 3; ; a6 += 0x58) {           /* debris */
			rd2_ww(a6 + 0x00, 1);
			rd2_ww(a6 + 0x14, 0xffff);
			rd2_ww(a6 + 0x0e, RW(d1));
			SETW(d1, RW(d1) + 1);
			rd2_ww(a6 + 0x02, (U16)(rd2_rw(RX) + rd2_rw(a0)));
			a0 += 2;
			rd2_ww(a6 + 0x06, (U16)(rd2_rw(RY) + rd2_rw(a0)));
			a0 += 2;
			if (--n == -1) break;
		}
		rd2_ww(0x1695a, 0);
	}
	return;

L13b84:                                                      /* tunnel */
	rd2_ww(0x12e1c, 0);
	rd2_ww(0x12e1e, 0);
	rd2_ww(0x12e20, 0);
	rd2_ww(0x12e22, 0);
	rd2_ww(0x12e24, 0);
	rd2_ww(0x12e28, 0);
	rd2_ww(LADDER, 0);
	SETW(d1, rd2_rw(RVY) + 0x40);
	if (RWS(d1) > 0x400) SETW(d1, 0x400);
	if (d0 & 0x01) {
		SETW(d1, RW(d1) - 0xc0);
		if (RWS(d1) < -0x400) SETW(d1, 0xfc00);
	} else if (d0 & 0x02) {
		SETW(d1, RW(d1) + 0x80);
		if (RWS(d1) > 0x400) SETW(d1, 0x400);
	}
	rd2_ww(RVY, RW(d1));
	rd2_ww(RD2_PVY, RW(d1));
	EXTL(d1);
	d1 <<= 8;
	d1 += rd2_rl(RY);
	SWAP(d1);
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(RD2_PX, rd2_rw(RX));
	rd2_ww(RD2_PACT, 0xffff);
	rd2_15fba();
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20) goto L13a62;
	if (d7 & 0x04) goto L13c6a;
	if (!(d7 & 0x02)) goto L13c86;
	if (rd2_rws(RVY) >= 0) goto L13c6a;
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) + 1);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
	rd2_ww(RVY, 0);
	goto L13c86;
L13c6a:
	SETW(d1, RW(d1) + 0x0c);
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) - 0x14);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
	rd2_ww(RVY, 0);
L13c86:
	SWAP(d1);
	rd2_wl(RY, d1);
	d6 = 0;
	if (d0 & 0x04) {
		d6 = 0xfffffffcu;
		rd2_ww(RFACE, 1);
	} else if (d0 & 0x08) {
		d6 = 4;
		rd2_ww(RFACE, 0);
	}
	SETW(d2, rd2_rw(RX));
	SETW(d2, RW(d2) + RW(d6));
	if (RWS(d6) != 0) {
		if (RWS(d6) < 0) {
			if (RWS(d2) < 0) {
				d2 = 0;
				rd2_ww(0x14360, 1);
			}
		} else if (RWS(d2) >= 0xe8) {
			SETW(d2, 0xe8);
			rd2_ww(0x14360, 2);
		}
	}
	rd2_ww(RD2_PY, rd2_rw(RY));
	rd2_ww(RD2_PVY, 0xffff);
	rd2_ww(RD2_PX, RW(d2));
	rd2_ww(RD2_PACT, 0xffff);
	rd2_15fba();                                             /* $13602 */
	goto L13606;
}

/* $13e14 laser shot */
void
rd2_13e14(void)
{
	U32 d0;

	if (rd2_rw(0x16902) == 0)
		return;
	d0 = 4;
	if (rd2_rws(0x16926) >= 0)
		d0 = 0x14;
	SETW(d0, RW(d0) + rd2_rw(0x16904));
	rd2_ww(RD2_PX, RW(d0));
	rd2_ww(RD2_PY, rd2_rw(0x16908));
	rd2_ww(RD2_PY, (U16)(rd2_rw(RD2_PY) + 4));
	rd2_161fe();
	if (rd2_rb(RD2_PRES) & 0x02)
		goto OFF;
	SETW(d0, RW(d0) + rd2_rw(0x16926));
	if (RWS(d0) < 0 || RWS(d0) > 0xff)
		goto OFF;
	rd2_ww(0x12efa, RW(d0));
	rd2_ww(0x12efc, rd2_rw(RD2_PY));
	rd2_ww(0x16904, (U16)(rd2_rw(0x16904) + rd2_rw(0x16926)));
	return;
OFF:
	rd2_ww(0x16902, 0);
}

/* $13e98 bomb */
void
rd2_13e98(void)
{
	U32 d0, d1, d2, d3, d5, d6 = 0;
	U8 d7;
	S16 st;

	rd2_ww(0x12efe, 0);
	rd2_ww(0x12f06, 0);
	st = rd2_rws(0x16b12);
	if (st < 1)
		return;
	if (st > 1)
		goto L14152;
	if (rd2_rw(0x16b5e) != 0)
		goto L1410e;
	if (rd2_171bc(0x16b12u))
		goto L1410e;
	d1 = (U16)(rd2_rw(0x16b1e) + 0x80);
	if (RWS(d1) > 0x800) SETW(d1, 0x800);
	rd2_ww(0x16b1e, RW(d1));
	rd2_ww(RD2_PVY, RW(d1));
	EXTL(d1);
	d1 <<= 8;
	d1 += rd2_rl(0x16b18);
	SWAP(d1);
	if (RWS(d1) > 0x130 || RWS(d1) < 0)
		goto L141c2;
	rd2_ww(RD2_PY, RW(d1));
	rd2_ww(RD2_PX, rd2_rw(0x16b14));
	rd2_ww(RD2_PACT, 0xffff);
	rd2_16278();
	d7 = rd2_rb(RD2_PRES);
	if (!(d7 & 0x40))
		goto L13f88;
	d1 = 0;                                                  /* on an actor */
	SETW(d1, rd2_rw(RD2_PPLY) - 0x10);
	rd2_ww(RD2_PY, RW(d1));
	SWAP(d1);
	rd2_wl(0x16b18, d1);
	SWAP(d1);
	rd2_ww(0x12f06, rd2_rw(RD2_PPDX));
	rd2_ww(RD2_PACT, 0);
	rd2_16278();
	d7 = rd2_rb(RD2_PRES);
	if (d7 & 0x20)
		goto L1410e;
	d7 &= 0x06;
	if (d7 != 0)
		goto L13fa8;
	rd2_ww(0x16b1e, 0x100);
	goto L14024;
L13f88:
	if (d7 & 0x04) goto L13fba;
	if (!(d7 & 0x02)) {
		SWAP(d1);
		rd2_wl(0x16b18, d1);
		goto L14024;
	}
	if (rd2_rws(0x16b1e) >= 0) goto L13fba;
L13fa8:
	SETB(d1, RB(d1) | 7);
	SETW(d1, RW(d1) + 1);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
	goto L13fca;
L13fba:
	SETW(d1, RW(d1) - 1);
	SETB(d1, RB(d1) & 0xf8);
	SWAP(d1);
	SETW(d1, 0);
	SWAP(d1);
L13fca:
	SWAP(d1);
	rd2_wl(0x16b18, d1);
	if (d7 & 0x20)
		goto L1410e;
	if (d7 & 0x01) {
		if (MAP == 3) {
			SETW(d6, rd2_rw(0x16b1e));
			if (RWS(d6) >= 0x200) {
				SETW(d6, (U16)(-RWS(d6)));
				SETW(d6, RW(d6) + 0x60);
				rd2_ww(0x16b1e, RW(d6));
				goto L14024;
			}
		} else if (MAP == 5) {
			SETW(d6, 0xf781);
			goto L1401e;
		}
	}
	SETW(d6, 0x100);
L1401e:
	rd2_ww(0x16b1e, RW(d6));
L14024:
	SETW(d6, rd2_rw(0x16b36));
	d5 = 8;
	if (d7 & 0x01) {
		if (MAP == 1) goto L14064;
		if (MAP == 2) goto L14076;
		if (MAP == 4) SETW(d5, 0x10);
	}
	if (RWS(d6) >= 0) {
		SETW(d6, RW(d6) - RW(d5));
		if (RWS(d6) >= 0) goto L14066;
	} else {
		SETW(d6, RW(d6) + RW(d5));
		if (RWS(d6) < 0) goto L14066;
	}
L14064:
	d6 = 0;
L14066:
	rd2_ww(0x16b36, RW(d6));
	SETW(d5, rd2_rw(0x12f06));
	SETW(d5, RW(d5) << 8);                                   /* asl.w #8 */
	SETW(d6, RW(d6) + RW(d5));
L14076:
	d2 = rd2_rl(0x16b14);
	rd2_ww(RD2_PY, rd2_rw(0x16b18));
	rd2_ww(RD2_PVY, 0xffff);
	d5 = d6;
	SETW(d5, RW(d6));
	EXTL(d5);
	d5 <<= 8;
	d2 += d5;
	SWAP(d2);
	if (RWS(d2) > 0xe8) {
		d2 = 0;
		SETW(d2, 0xe8);
		rd2_ww(0x16b36, 0);
	} else if (RWS(d2) < 0) {
		d2 = 0;
		rd2_ww(0x16b36, 0);
	}
	rd2_ww(RD2_PX, RW(d2));
	rd2_ww(RD2_PACT, 0xffff);
	rd2_16278();
	if (rd2_rb(RD2_PRES) & 0x02) {                           /* wall */
		SWAP(d2);
		SETW(d2, 0);
		SWAP(d2);
		if (RWS(d6) < 0) {
			SETW(d2, RW(d2) | 7);
			SETW(d2, RW(d2) + 1);
		} else {
			SETW(d2, RW(d2) - 1);
			SETB(d2, RB(d2) & 0xf8);
		}
	}
	SWAP(d2);
	rd2_wl(0x16b14, d2);
	if (!(rd2_rb(RD2_PRES) & 0x20))
		return;
L1410e:                                                      /* explode */
	snd(0x13);
	rd2_ww(0x16b5e, 0);
	rd2_ww(0x16b12, 2);
	rd2_ww(0x16b34, 0);
	rd2_wl(0x16b30, 0x12ed6u);
	rd2_ww(0x16b18, (U16)(rd2_rw(0x16b18) - 5));
	rd2_ww(0x12f00, 0);
L14152:
	rd2_ww(0x12f00, (U16)(rd2_rw(0x12f00) + 1));
	if (!(rd2_rws(0x12f00) > 7)) {
		rd2_ww(0x12efe, 1);
		rd2_ww(0x12f02, (U16)(rd2_rw(0x16b14) + 0x0c));
		rd2_ww(0x12f04, (U16)(rd2_rw(0x16b18) + 0x0a));
		d0 = (U16)(rd2_rw(0x12f02) - 0x10);
		d1 = (U16)(rd2_rw(0x12f04) - 0x0e);
		d2 = 0x20;
		d3 = 0x1c;
		if (rd2_14b7a(RWS(d0), RWS(d1), RWS(d2), RWS(d3)))
			rd2_ww(0x12e2c, 0xffff);
	}
	if (!rd2_171bc(0x16b12u))
		return;
L141c2:
	rd2_ww(0x16b12, 0);
}

/* eof */
