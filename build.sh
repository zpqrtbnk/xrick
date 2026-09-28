#!/bin/bash
# xrick build: the Windows desktop version AND the web (WebAssembly) version, every time.
#
#   desktop  MSBuild xrick/xrick.vcxproj, Release x64 -> build/win/ (xrick.exe, SDL3.dll;
#            objects in build/win/obj/) (kb/build.md §2; SDL3 from vcpkg, manifest mode)
#   web      emscripten, RD1 only for now -> build/web/ (index.html, player.js,
#            xrick.js, xrick.wasm; kb/build.md §4, wasm.md). Mirrors xrick/Makefile:
#            same sources, include paths and PLATFORM switch, SDL3 from emscripten's
#            own port (-sUSE_SDL=3).
#
# Run from a plain Git Bash at the top of the repo; nothing has to be sourced first:
#   ./build.sh            incremental builds
#   ./build.sh clean      rebuild both from scratch (needed for the web build after a
#                         header change: its incremental check only compares .c/.cpp dates)
#   ./build.sh gz         also write gzip-compressed copies of the web files to build/web/gz/
#
# Before building anything, the script checks that it can find what it needs and stops
# with a message otherwise:
#   MSBuild   MSBUILD (path to MSBuild.exe) or, by default, found with vswhere
#   SDL3      xrick/vcpkg_installed/x64-windows (vcpkg install --triplet x64-windows,
#             one-time setup, kb/build.md §2)
#   emsdk     EMSDK_DIR, default /d/d/EmSdk: emsdk_env.sh and upstream/emscripten/emcc
#   python    EMSDK_PYTHON, default the one bundled in EMSDK_DIR (plain `python` can be
#             the Windows Store alias, which breaks emsdk_env.sh)
#   page      build/emsdk/index.html and player.js (the web page sources)
# PLATFORM (ST default, or PC) selects the web build's game behaviour, as in the
# Makefile; the desktop project always builds PLATFORM_ST (kb/build.md §3).
#
# Serve build/web/ over http to play the web version (e.g. `emrun build/web/index.html`);
# .wasm must be served as application/wasm. build/emsdk/ holds the page sources (tracked);
# build/web/ and build/win/ are outputs (git-ignored).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRCDIR="$ROOT/xrick"
WEB="$ROOT/build/web"
WIN="$ROOT/build/win"
PAGE="$ROOT/build/emsdk"
PLATFORM="${PLATFORM:-ST}"

fail() { echo "error: $*" >&2; exit 1; }

CLEAN=; GZ=
for arg in "$@"; do
  case "$arg" in
    clean) CLEAN=1 ;;
    gz) GZ=1 ;;
    *) echo "unknown argument: $arg (use: clean, gz)" >&2; exit 2 ;;
  esac
done

# --- 1. find what the builds need, before building anything --------------------------------

# MSBuild (Visual Studio "Desktop development with C++")
if [ -z "$MSBUILD" ]; then
  VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  [ -x "$VSWHERE" ] || fail "vswhere.exe not found at $VSWHERE -- install Visual Studio with the C++ workload, or set MSBUILD to MSBuild.exe"
  MSBUILD="$("$VSWHERE" -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild \
             -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>/dev/null | head -1 | tr -d '\r')"
  [ -n "$MSBUILD" ] || fail "vswhere found no MSBuild.exe -- install the Visual Studio C++ workload, or set MSBUILD"
  MSBUILD="$(cygpath -u "$MSBUILD")"
fi
[ -f "$MSBUILD" ] || fail "MSBuild not found: $MSBUILD"
echo "msbuild: $MSBUILD"

# the web page sources (tracked in build/emsdk/)
for p in index.html player.js; do
  [ -f "$PAGE/$p" ] || fail "web page source missing: $PAGE/$p"
done

# SDL3 for the desktop build (vcpkg manifest mode, x64-windows)
[ -f "$SRCDIR/vcpkg_installed/x64-windows/include/SDL3/SDL.h" ] || \
  fail "SDL3 not found in $SRCDIR/vcpkg_installed/x64-windows -- run the one-time vcpkg setup (kb/build.md §2): vcpkg install --triplet x64-windows in xrick/"

# emsdk, registered in this shell. before `set -e`: emsdk_env.sh is sourced here, and a
# non-zero status from it must not end the script silently
EMSDK_DIR="${EMSDK_DIR:-/d/d/EmSdk}"
[ -d "$EMSDK_DIR" ] || fail "emsdk not found: EMSDK_DIR=$EMSDK_DIR does not exist (set EMSDK_DIR to the emsdk folder)"
[ -f "$EMSDK_DIR/emsdk_env.sh" ] || fail "no emsdk_env.sh in EMSDK_DIR=$EMSDK_DIR -- not an emsdk folder?"
[ -f "$EMSDK_DIR/upstream/emscripten/emcc" ] || \
  fail "no upstream/emscripten/emcc in $EMSDK_DIR -- install and activate emscripten there (emsdk install latest; emsdk activate latest)"
if [ -z "$EMSDK_PYTHON" ]; then
  # the python bundled with emsdk: Windows layout first, then Linux/macOS
  EMSDK_PYTHON="$(ls -d "$EMSDK_DIR"/python/3*/python.exe 2>/dev/null | sort | tail -1)"
  [ -z "$EMSDK_PYTHON" ] && EMSDK_PYTHON="$(ls -d "$EMSDK_DIR"/python/3*/bin/python3 2>/dev/null | sort | tail -1)"
fi
[ -n "$EMSDK_PYTHON" ] && export EMSDK_PYTHON
echo "emsdk:   registering $EMSDK_DIR (python: ${EMSDK_PYTHON:-from PATH})"
EMSDK_LOG="$(mktemp)"
pushd "$EMSDK_DIR" >/dev/null
# in this shell, not through a pipe: a pipe would run it in a subshell and lose the variables
source ./emsdk_env.sh >"$EMSDK_LOG" 2>&1
popd >/dev/null
if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc not found after sourcing $EMSDK_DIR/emsdk_env.sh; its output:" >&2
  cat "$EMSDK_LOG" >&2
  rm -f "$EMSDK_LOG"
  exit 1
fi
rm -f "$EMSDK_LOG"
echo "emcc:    $(command -v emcc) -- $(emcc --version 2>/dev/null | head -1)"

set -e

# --- 2. desktop build (MSBuild, Release x64) --------------------------------------------------
echo
echo "=== desktop (MSBuild) ==="
# -t:/-p: rather than /t: /p: -- Git Bash would take a leading slash for a path.
# OutDir/IntDir override the project's bin\<Config>\ and obj\...\ so that the exe (plus the
# SDL3.dll the post-build step copies to $(OutDir)) and its objects all land in build/win/.
TARGET=Build; [ -n "$CLEAN" ] && TARGET=Rebuild
[ -n "$CLEAN" ] && rm -rf "$WIN"
mkdir -p "$WIN"
WINW="$(cygpath -w "$WIN")"
MSLOG="$(mktemp)"
if ! "$MSBUILD" "$(cygpath -w "$SRCDIR/xrick.vcxproj")" -t:$TARGET -p:Configuration=Release -p:Platform=x64 \
     "-p:OutDir=$WINW\\" "-p:IntDir=$WINW\\obj\\" -nologo -v:minimal >"$MSLOG" 2>&1; then
  grep -E "error|Error" "$MSLOG" | tail -20 >&2
  rm -f "$MSLOG"
  fail "desktop build failed"
fi
grep -E "^ *[0-9]+ Warning\(s\)|^ *[0-9]+ Error\(s\)|warning [A-Z]+[0-9]+" "$MSLOG" | sed 's/^/  /' | tail -8
rm -f "$MSLOG"
[ -f "$WIN/SDL3.dll" ] || fail "desktop build: SDL3.dll was not copied next to the exe in $WIN"
ls -la "$WIN/xrick.exe" "$WIN/SDL3.dll"

# --- 3. web build (emscripten) -----------------------------------------------------------------
echo
echo "=== web (emscripten) ==="
[ -n "$CLEAN" ] && rm -rf "$WEB"

INC="-Iinclude -Iinclude/rd1 -Iinclude/rd2 -Isrc -Isrc/rd1 -Isrc/rd2"
WARN="-Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits"
CFLAGS="$INC $WARN -fcommon -O2 -DPLATFORM_$PLATFORM -sUSE_SDL=3"
CXXFLAGS="$INC -O2 -std=c++17 -sUSE_SDL=3"
# INVOKE_RUN=0 + callMain: the page starts the game on a click (audio unlock, wasm.md W1.5)
# EXIT_RUNTIME: exit() and emscripten_force_exit() really end the runtime, flush stdio and
# call Module.onExit (the page shows why the game stopped). _fflush: the page flushes the
# -trace file before reading it from MEMFS while the game runs (wasm.md W1.7).
LDFLAGS="-O2 -sUSE_SDL=3 -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sEXPORTED_RUNTIME_METHODS=callMain,FS -sEXPORTED_FUNCTIONS=_main,_fflush -sALLOW_MEMORY_GROWTH=1"

cd "$SRCDIR"
CSRC=$(ls src/*.c src/rd1/*.c src/rd2/*.c | grep -v -E 'src/rd1/dat_(pics|sprites|tiles)PC\.c')
CXXSRC=$(ls src/audio_engine/*.cpp src/audio_engine/external/Musashi/*.cpp)

mkdir -p "$WEB/obj"
: > "$WEB/warn.log"
OBJS=""
n=0
for f in $CSRC $CXXSRC; do
  o="$WEB/obj/$(echo "$f" | tr '/' '_').o"
  OBJS="$OBJS $o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    case "$f" in
      *.c)   cc=emcc; flags="$CFLAGS" ;;
      *.cpp) cc=em++; flags="$CXXFLAGS" ;;
    esac
    # diagnostics go to warn.log; on an error, show them rather than stop silently
    if ! $cc -c "$f" -o "$o" $flags 2>>"$WEB/warn.log"; then
      echo "error: $cc failed on $f:" >&2
      grep -A3 "error:" "$WEB/warn.log" | tail -20 >&2
      exit 1
    fi
    n=$((n+1))
  fi
done
echo "compiled: $n file(s), warnings this run: $(grep -c 'warning:' "$WEB/warn.log")"

echo "linking..."
em++ $OBJS -o "$WEB/xrick.js" $LDFLAGS

cp "$PAGE/index.html" "$PAGE/player.js" "$WEB/"

if [ -n "$GZ" ]; then
  rm -rf "$WEB/gz" && mkdir -p "$WEB/gz"
  for f in index.html player.js xrick.js xrick.wasm; do gzip -c -9 "$WEB/$f" > "$WEB/gz/$f"; done
  echo "gz/: serve with Content-Encoding: gzip (and application/wasm for xrick.wasm)"
fi
ls -la "$WEB/xrick.js" "$WEB/xrick.wasm"

echo
echo "done: desktop $WIN/xrick.exe, web $WEB"
