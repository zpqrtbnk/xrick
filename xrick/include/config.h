/*
 * xrick/include/config.h
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

#ifndef _CONFIG_H
#define _CONFIG_H

/* version */
#define VERSION "050500"

/* graphics (choose one) */
#define GFXST
#undef GFXPC

/*
 * Target platform (choose exactly one) -- see ../../review-plan.md
 *
 * This selects GAME BEHAVIOUR and GAME DATA, and is INDEPENDENT of GFXST/GFXPC above,
 * which select artwork only. The stock xrick build was ST artwork driving PC logic;
 * PLATFORM_ST makes it an Atari ST clone throughout.
 *
 * Where the two versions genuinely differ, both values are kept and switched here --
 * neither is deleted. Every switched site cites its xrick/re/xref.md row or its
 * review-log.md id.
 */
/* The default is ST. Either may be forced from the build with -DPLATFORM_ST or
   -DPLATFORM_PC, so both configurations can be built and compared (review-plan.md
   R4.2) without editing this file: `make PLATFORM=PC`. */
#if !defined(PLATFORM_ST) && !defined(PLATFORM_PC)
#define PLATFORM_ST
#endif

#if defined(PLATFORM_ST) && defined(PLATFORM_PC)
#error "define exactly one of PLATFORM_ST / PLATFORM_PC, not both"
#endif
#if !defined(PLATFORM_ST) && !defined(PLATFORM_PC)
#error "define exactly one of PLATFORM_ST / PLATFORM_PC"
#endif

/* logging (write to console) */
#define ENABLE_LOG
#ifdef EMSCRIPTEN
#undef ENABLE_LOG
#endif

/* joystick support */
#undef ENABLE_JOYSTICK

/* sound support */
#define ENABLE_SOUND

/* cheats support */
#define ENABLE_CHEATS

/* auto-defocus support */
/* does seem to cause all sorts of problems on BeOS, Windows... */
#undef ENABLE_FOCUS

/* demo (attract) mode: -demo plays a script, -record writes one. see ../../demo.md */
#define ENABLE_DEMO

/* development tools */
#undef ENABLE_DEVTOOLS
#define DEBUG /* see include/debug.h */

/* zlib */
#ifndef NOZLIB
#define WITH_ZLIB
#endif

#endif

/* eof */


