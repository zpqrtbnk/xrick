# Makefile — added 2026-09-04 for `review-plan.md` R0.1.
#
# The tree shipped with an MSVC project only (xrick.vcxproj), which expects
# ..\..\lib\SDL2-2.0.9 and ..\..\lib\zlib1211 — neither present in the repo. This
# builds the same sources against the system SDL3 (T20) so the review can compile,
# warn and run on Linux/WSL.
#
# Targets (output under ../build/wsl, see OUTDIR):
#   make            release-ish build with the full warning set -> ../build/wsl/xrick
#   make warn       syntax-only pass over the game logic (the R0.1 evidence)
#   make clean      removes ../build/wsl

CC      ?= gcc
CXX     ?= g++
# T20: upgraded from SDL2 to SDL3 -- see build.md. SDL3 dropped sdl2-config-style
# shell scripts in favor of pkg-config exclusively.
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

# The warning set is deliberate: -Wconversion/-Wsign-conversion are how phase 2
# (variable widths) gets its candidate list. Do not silence them.
WARN    := -Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits
# -fcommon: this is pre-C99-style code with tentative definitions of the same symbol in
# several translation units (WAV_*). MSVC and pre-10 GCC allowed it; modern
# GCC defaults to -fno-common and rejects it. Build portability only.
# PLATFORM=ST (default) or PLATFORM=PC -- selects game behaviour and data.
PLATFORM ?= ST
# RD1/RD2 split (2026-09-22): include/ and src/ hold platform-common code; each game's
# own engine and data live in include/rd1|rd2 and src/rd1|rd2. -Isrc(/rd1|/rd2) is needed
# because a couple of common files quote-include a game's own subtree (e.g. sysvid.c's
# "img_icon.e", syssnd.c's "audio_engine/AtariMachineC.h") and GCC's quote-include only
# checks the including file's own directory before falling back to -I.
INC := -Iinclude -Iinclude/rd1 -Iinclude/rd2 -Isrc -Isrc/rd1 -Isrc/rd2
CFLAGS  += $(INC) $(SDL_CFLAGS) $(WARN) -fcommon -O2 -g -DPLATFORM_$(PLATFORM)
# T19 / audio-sndh.md S9: src/audio_engine/ is AtariAudio, vendored verbatim from
# arnaud-carre/AtariAudio (MIT) -- C++, not C. Built with its own, more lenient flags;
# this tree's -Wconversion/-Wsign-conversion discipline is not held against vendored
# third-party code. Quote-includes resolve same-directory (ym2149c.h, external/Musashi/
# m68k.h, etc.), so no extra -I is needed beyond what CFLAGS already has.
CXXFLAGS += $(INC) $(SDL_CFLAGS) -O2 -g -std=c++17
# T22: zlib/unzip dropped -- see build.md. Nothing loads assets from disk anymore;
# every WAV_*/dat_* table is compiled in, and data_file_{open,read,close,...} (the
# only things that ever called into zlib) were dead code with zero callers.
LDFLAGS +=
LIBS    := $(SDL_LIBS) -lm

CSRC := $(wildcard src/*.c) $(wildcard src/rd1/*.c) $(wildcard src/rd2/*.c)

# GFXST is selected in config.h, so the GFXPC data tables must not be compiled.
CSRC := $(filter-out src/rd1/dat_picsPC.c src/rd1/dat_spritesPC.c src/rd1/dat_tilesPC.c,$(CSRC))

CXXSRC := $(wildcard src/audio_engine/*.cpp) $(wildcard src/audio_engine/external/Musashi/*.cpp)

# All build output goes under the repo's build/ (git-ignored, never sources; build.sh
# uses build/win and build/web): the binary is build/wsl/xrick, objects in
# build/wsl/obj/, the `make warn` log build/wsl/warn.log. PLATFORM=ST and PLATFORM=PC
# share this tree, so `make clean` when switching.
OUTDIR := ../build/wsl
OBJ := $(CSRC:%.c=$(OUTDIR)/obj/%.o) $(CXXSRC:%.cpp=$(OUTDIR)/obj/%.o)
BIN := $(OUTDIR)/xrick
WARNLOG := $(OUTDIR)/warn.log

# Game logic only — no generated tables, no SDL layer, no vendored library. This is the
# set that review-plan.md phases 2-3 actually read.
LOGIC := $(filter-out $(wildcard src/rd1/dat_*.c) $(wildcard src/rd2/dat_*.c) $(wildcard src/sys*.c) src/rd1/syssnd.c src/rd2/syssnd.c,$(CSRC))

all: $(BIN)

# Linked with CXX (not CC) because audio_engine/ pulls in libstdc++ -- harmless for the
# C objects, required for the C++ ones.
$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OUTDIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# R0.1 evidence: warnings over the logic surface, without linking.
warn:
	@mkdir -p $(OUTDIR)
	@: > $(WARNLOG)
	@for f in $(LOGIC); do $(CC) -fsyntax-only $(INC) $(SDL_CFLAGS) $(WARN) $$f 2>> $(WARNLOG); done
	@echo "=== warning census ==="
	@grep -oE '\[-W[a-z-]+\]' $(WARNLOG) | sort | uniq -c | sort -rn
	@echo "total: $$(grep -c 'warning:' $(WARNLOG)) warnings, $$(grep -c 'error:' $(WARNLOG)) errors"

# T43 phase 3: xrick-core, the RD1 game logic headless -- no SDL, no sound engine.
# Same sources as the game minus the host layer (sys*.c, xrick.c, rd1/syssnd.c) and
# RD2, plus src/headless/. Built with -DHEADLESS into its own object tree, since
# game.c differs. Output goes under the repo's build/ (git-ignored, all build output
# lives there): build/core/xrick-core, objects in build/core/obj/.
CORE_DIR := ../build/core
CORE_SRC := $(filter-out $(wildcard src/sys*.c) src/xrick.c src/rd1/syssnd.c \
	src/rd1/dat_picsPC.c src/rd1/dat_spritesPC.c src/rd1/dat_tilesPC.c, \
	$(wildcard src/*.c) $(wildcard src/rd1/*.c)) $(wildcard src/headless/*.c)
CORE_OBJ := $(CORE_SRC:%.c=$(CORE_DIR)/obj/%.o)
CORE_BIN := $(CORE_DIR)/xrick-core

core: $(CORE_BIN)

$(CORE_BIN): $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(CORE_OBJ) -lm

$(CORE_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(WARN) -fcommon -O2 -g -DPLATFORM_$(PLATFORM) -DHEADLESS -c -o $@ $<

clean:
	rm -rf $(OUTDIR) $(CORE_DIR)

.PHONY: all core warn clean
