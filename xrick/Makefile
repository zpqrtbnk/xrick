# Makefile — added 2026-09-04 for `review-plan.md` R0.1.
#
# The tree shipped with an MSVC project only (xrick.vcxproj), which expects
# ..\..\lib\SDL2-2.0.9 and ..\..\lib\zlib1211 — neither present in the repo. This
# builds the same sources against the system SDL2 and zlib so the review can compile,
# warn and run on Linux/WSL.
#
# Targets:
#   make            release-ish build with the full warning set
#   make warn       syntax-only pass over the game logic (the R0.1 evidence)
#   make clean

CC      ?= gcc
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

# The warning set is deliberate: -Wconversion/-Wsign-conversion are how phase 2
# (variable widths) gets its candidate list. Do not silence them.
WARN    := -Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits
# -fcommon: this is pre-C99-style code with tentative definitions of the same symbol in
# several translation units (IMG_SPLASH, WAV_*). MSVC and pre-10 GCC allowed it; modern
# GCC defaults to -fno-common and rejects it. Build portability only.
# PLATFORM=ST (default) or PLATFORM=PC -- selects game behaviour and data.
PLATFORM ?= ST
CFLAGS  += -Iinclude $(SDL_CFLAGS) $(WARN) -fcommon -O2 -g -DPLATFORM_$(PLATFORM)
LDFLAGS +=
LIBS    := $(SDL_LIBS) -lz -lm

SRC := $(wildcard src/*.c)

# GFXST is selected in config.h, so the GFXPC data tables must not be compiled.
SRC := $(filter-out src/dat_picsPC.c src/dat_spritesPC.c src/dat_tilesPC.c,$(SRC))

OBJ := $(SRC:.c=.o)
BIN := xrick

# Game logic only — no generated tables, no SDL layer. This is the set that
# review-plan.md phases 2-3 actually read.
LOGIC := $(filter-out $(wildcard src/dat_*.c) src/unzip.c $(wildcard src/sys*.c),$(SRC))

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# R0.1 evidence: warnings over the logic surface, without linking.
warn:
	@: > warn.log
	@for f in $(LOGIC); do $(CC) -fsyntax-only -Iinclude $(SDL_CFLAGS) $(WARN) $$f 2>> warn.log; done
	@echo "=== warning census ==="
	@grep -oE '\[-W[a-z-]+\]' warn.log | sort | uniq -c | sort -rn
	@echo "total: $$(grep -c 'warning:' warn.log) warnings, $$(grep -c 'error:' warn.log) errors"

clean:
	rm -f $(OBJ) $(BIN) warn.log

.PHONY: all warn clean
