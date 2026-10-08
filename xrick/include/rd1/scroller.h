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

#ifndef _SCROLLER_H
#define _SCROLLER_H

#define SCROLL_RUNNING 1
#define SCROLL_DONE 0

/* 12 ms per scroll step: xrick's 24, halved when game.c's timer was fixed so the
   scroll keeps the pace it had (the old timer ran about half period). Not measured
   on the ST. */
#define SCROLL_PERIOD 12

extern U8 scroll_up(void);
extern U8 scroll_down(void);

#endif

/* eof */


