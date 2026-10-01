/*
 * xrick/src/sysarg.c
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

/*
 * 20021010 added test to prevent buffer overrun in -keys parsing.
 */

#include <stdlib.h>  /* atoi */
#include <string.h>  /* strcasecmp */

#include <SDL3/SDL.h>

#include "system.h"
#include "syskbd.h"
#include "syssnd.h"

#include "game.h"
#include "maps.h"

/* handle Microsoft Visual C (must come after system.h!) */
#ifdef __MSVC__
/* review-plan.md R0.1: _stricmp is MSVC-only; POSIX already has strcasecmp. */
#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif
#endif

typedef struct {
  char name[16];
  int code;
} sdlcodes_t;

// these codes are exported from SDL
static sdlcodes_t sdlcodes[] = {
#include "sdlcodes.e"
};

int sysarg_args_period = 0;
int sysarg_args_map = 0;
int sysarg_args_submap = 0;
int sysarg_args_mapset = 0;  /* -map was given (RD2 then skips its title and picker) */
static int submapset = 0;
int sysarg_args_fullscreen = 0;
int sysarg_args_zoom = 0;
int sysarg_args_nosound = 0;
int sysarg_args_vol = -1;  /* -vol: 0 (silence) .. SYSSND_MAXVOL; -1 = not given */
int sysarg_args_game = 1;   /* -game: 1 = Rick Dangerous, 2 = Rick Dangerous 2 */
#ifdef ENABLE_DEMO
int sysarg_args_demo = 0;
char *sysarg_args_record = NULL;
char *sysarg_args_trace = NULL;
#endif

/*
 * Fail
 */
void
sysarg_fail(char *msg)
{
    sys_printf(
        "xrick [version #%s]: %s\n"
        "usage: xrick [OPTION]\n"
        "\n"
        "  -h, -help            display this information\n"
        "  -fullscreen          run in fullscreen mode, default is to run in a window\n"
        "  -speed <speed>       run at speed <speed>, <speed> must be an integer between 1\n"
        "                         (fast) and 100 (slow), default is %d\n"
        "  -zoom <zoom>         display with zoom factor <zoom>, <zoom> must be an integer\n"
        "                         between 1 (320x200) and x (x times bigger), default is 2\n"
        "  -game <game>         play Rick Dangerous <game>, <game> is 1 or 2, default is 1\n"
        "  -map <map>           start at map number <map>, <map> must be an integer between\n"
        "                         1 and %d, default is to start at map number 1. With -game 2:\n"
        "                         a game starts at that level, as if picked on SELECT LEVEL\n"
        "  -submap <submap>     start at submap <submap>, <submap> must be an integer\n"
        "                         between 1 and %d, default is to start at submap number 1 or,\n"
        "                         if a map was specified, at the first submap of that map.\n"
        "                         Not with -game 2.\n"
        "  -keys <bindings>     override the default key bindings, <bindings> uses format\n"
        "                         <left>-<right>-<up>-<down>-<fire> (cf. KeyCodes)\n",
        // FIXME what's KeyCodes? also nb of maps/submaps depend on game!
        VERSION, msg, GAME_PERIOD, MAP_NBR_MAPS-1, MAP_NBR_SUBMAPS);
#ifdef ENABLE_SOUND
    sys_printf(
        "  -nosound             disable sounds, default is to play with sounds enabled\n"
        "  -vol <vol>           play sounds at volume <vol>, <vol> must be an integer\n"
        "                         between 0 (silence) and %d (max). The default is to play sounds\n"
        "                         at maximal volume (%d).\n",
        SYSSND_MAXVOL, SYSSND_MAXVOL);
#endif
#ifdef ENABLE_DEMO
    sys_printf(
    	"  -demo                play the built-in demo script (attract mode)\n"
        // FIXME meh?
    	"                         RD1 scripts are counted per submap visit from the start of the\n"
    	"                         game, so -map / -submap only match a script recorded from that same\n"
    	"                         start. At the end of the game the demo loops back to the title screens.\n"
    	"                         With -game 2: one script per map, played from the level start of a game\n"
    	"                         started by hand; the run's end hands control back.\n"
    	"  -record <file>       record controls played into <file>\n"
    	"  -trace <file>        RD1: write one line per logic step to <file> -- random\n"
    	"                         generator, counters, entities -- to diff two runs tick by tick.\n");
#endif
	exit(1);
}

/*
 * Get SDL key code
 */
static int
sysarg_sdlcode(char *k)
{
  int i, result;

  i = 0;
  result = 0;

  while (sdlcodes[i].code) {
    if (!strcasecmp(sdlcodes[i].name, k)) {
      result = sdlcodes[i].code;
      break;
    }
    i++;
  }

  return result;
}

/*
 * Scan key codes sequence
 */
int
sysarg_scankeys(char *keys)
{
  char k[16];
  int i, j;

  i = 0;

  j = 0;
  while (keys[i] != '\0' && keys[i] != '-' && j + 1 < sizeof k) k[j++] = keys[i++];
  if (keys[i++] == '\0') return -1;
  k[j] = '\0';
  syskbd_left = sysarg_sdlcode(k);
  if (!syskbd_left) return -1;

  j = 0;
  while (keys[i] != '\0' && keys[i] != '-' && j + 1 < sizeof k) k[j++] = keys[i++];
  if (keys[i++] == '\0') return -1;
  k[j] = '\0';
  syskbd_right = sysarg_sdlcode(k);
  if (!syskbd_right) return -1;

  j = 0;
  while (keys[i] != '\0' && keys[i] != '-' && j + 1 < sizeof k) k[j++] = keys[i++];
  if (keys[i++] == '\0') return -1;
  k[j] = '\0';
  syskbd_up = sysarg_sdlcode(k);
  if (!syskbd_up) return -1;

  j = 0;
  while (keys[i] != '\0' && keys[i] != '-' && j + 1 < sizeof k) k[j++] = keys[i++];
  if (keys[i++] == '\0') return -1;
  k[j] = '\0';
  syskbd_down = sysarg_sdlcode(k);
  if (!syskbd_down) return -1;

  j = 0;
  while (keys[i] != '\0' && keys[i] != '-' && j + 1 < sizeof k) k[j++] = keys[i++];
  if (keys[i] != '\0') return -1;
  k[j] = '\0';
  syskbd_fire = sysarg_sdlcode(k);
  if (!syskbd_fire) return -1;

  return 0;
}

/*
 * Read and process arguments
 */
void
sysarg_init(int argc, char **argv)
{
  int i;

  for (i = 1; i < argc; i++) {

    if (!strcmp(argv[i], "-fullscreen")) {
      sysarg_args_fullscreen = 1;
    }

    else if (!strcmp(argv[i], "-help") ||
	     !strcmp(argv[i], "-h")) {
      sysarg_fail("help");
    }

    else if (!strcmp(argv[i], "-speed")) {
      if (++i == argc) sysarg_fail("missing speed value");
      sysarg_args_period = atoi(argv[i]) - 1;
      if (sysarg_args_period < 0 || sysarg_args_period > 99)
	sysarg_fail("invalid speed value");
    }

    else if (!strcmp(argv[i], "-keys")) {
      if (++i == argc) sysarg_fail("missing key codes");
      if (sysarg_scankeys(argv[i]) == -1)
	sysarg_fail("invalid key codes");
    }

    else if (!strcmp(argv[i], "-zoom")) {
      if (++i == argc) sysarg_fail("missing zoom value");
      sysarg_args_zoom = atoi(argv[i]);
      if (sysarg_args_zoom < 1)
	sysarg_fail("invalid zoom value");
    }

    else if (!strcmp(argv[i], "-map")) {
      if (++i == argc) sysarg_fail("missing map number");
      sysarg_args_map = atoi(argv[i]) - 1;
      if (sysarg_args_map < 0 || sysarg_args_map >= MAP_NBR_MAPS-1)
	sysarg_fail("invalid map number");
      sysarg_args_mapset = 1;
    }

    else if (!strcmp(argv[i], "-submap")) {
      if (++i == argc) sysarg_fail("missing submap number");
      sysarg_args_submap = atoi(argv[i]) - 1;
      if (sysarg_args_submap < 0 || sysarg_args_submap >= MAP_NBR_SUBMAPS)
	sysarg_fail("invalid submap number");
      submapset = 1;
    }
#ifdef ENABLE_SOUND
    else if (!strcmp(argv[i], "-vol")) {
      if (++i == argc) sysarg_fail("missing volume");
      sysarg_args_vol = atoi(argv[i]);
      if (sysarg_args_vol < 0 || sysarg_args_vol > SYSSND_MAXVOL)
	sysarg_fail("invalid volume");
    }

    else if (!strcmp(argv[i], "-nosound")) {
      sysarg_args_nosound = 1;
    }
#endif
    else if (!strcmp(argv[i], "-game")) {
      if (++i == argc) sysarg_fail("missing game number");
      sysarg_args_game = atoi(argv[i]);
      if (sysarg_args_game != 1 && sysarg_args_game != 2)
	sysarg_fail("invalid game number");
    }
#ifdef ENABLE_DEMO
    else if (!strcmp(argv[i], "-demo")) {
      sysarg_args_demo = 1;
    }

    else if (!strcmp(argv[i], "-record")) {
      if (++i == argc) sysarg_fail("missing record file name");
      sysarg_args_record = argv[i];
    }

    else if (!strcmp(argv[i], "-trace")) {
      if (++i == argc) sysarg_fail("missing trace file name");
      sysarg_args_trace = argv[i];
    }
#endif

    else {
      sysarg_fail("invalid argument(s)");
    }

  }

  /* RD2 enters a submap only through an exit trigger or a respawn, so there is no start
     point to give (wasm.md §10); -map works for both games, which both have 4 maps */
  if (submapset && sysarg_args_game == 2)
    sysarg_fail("-submap is not available with -game 2");

  /* FIXME this is dirty (sort of) */
  if (sysarg_args_submap > 0 && sysarg_args_submap < 9)
    sysarg_args_map = 0;
  if (sysarg_args_submap >= 9 && sysarg_args_submap < 20)
    sysarg_args_map = 1;
  if (sysarg_args_submap >= 20 && sysarg_args_submap < 38)
    sysarg_args_map = 2;
  if (sysarg_args_submap >= 38)
    sysarg_args_map = 3;
  if (sysarg_args_submap == 9 ||
      sysarg_args_submap == 20 ||
      sysarg_args_submap == 38)
    sysarg_args_submap = 0;

}

/* eof */
