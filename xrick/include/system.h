/*
 * xrick/include/system.h
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

#ifndef _SYSTEM_H
#define _SYSTEM_H

#include "config.h"

#include <stddef.h> /* NULL */

/*
 * If compiling w/gcc, then we can use attributes. UNUSED(x) flags a
 * parameter or a variable as potentially being unused, so that gcc doesn't
 * complain about it.
 *
 * Note: from OpenAL code: Darwin OS cc is based on gcc and has __GNUC__
 * defined, yet does not support attributes. So in theory we should exclude
 * Darwin target here.
 */
#ifdef __GNUC__
#define UNUSED(x) x __attribute((unused))
#else
#define UNUSED(x) x
#endif

/*
 * Detect Microsoft Visual C
 */
#ifdef _MSC_VER
#define __MSVC__
/*
 * FIXME disable "integral size mismatch in argument; conversion supplied" warning
 * as long as the code has not been cleared -- there are so many of them...
 */

#pragma warning( disable : 4761 )
#endif

/*
 * Detect Microsoft Windows
 */
#ifdef WIN32
#define __WIN32__
#endif

/* there are true at least on x86 platforms */
typedef unsigned char U8;         /*  8 bits unsigned */
typedef unsigned short int U16;   /* 16 bits unsigned */
typedef unsigned int U32;         /* 32 bits unsigned */
typedef signed char S8;           /*  8 bits signed   */
typedef signed short int S16;     /* 16 bits signed   */
typedef signed int S32;           /* 32 bits signed   */

#define TRUE 1
#define FALSE 0

extern void sys_init(int, char **);
extern void sys_shutdown(void);
extern void sys_panic(char *, ...);
extern void sys_printf(char *, ...);
extern U32 sys_gettime(void);
extern void sys_sleep(int);

#endif

/* eof */


