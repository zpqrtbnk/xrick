/*
 * xrick/include/sysarg.h
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#ifndef _SYSARG_H
#define _SYSARG_H

#include "system.h"

extern int sysarg_args_period;
extern int sysarg_args_map;
extern int sysarg_args_submap;
extern int sysarg_args_fullscreen;
extern int sysarg_args_zoom;
#ifdef ENABLE_SOUND
extern int sysarg_args_nosound;
extern int sysarg_args_vol;
#endif
extern char* sysarg_args_data;

extern void sysarg_init(int, char**);

#endif /* _SYSARG_H */

/* eof */
