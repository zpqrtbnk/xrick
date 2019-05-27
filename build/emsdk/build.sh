# xrick emscripten build script
# runs in build.tmp

# configure pathsg
PATH_PYTHON=/c/python27-x64
PATH_EMSDK="/d/d/EmSdk"
PATH_XRICK="/d/d/Rick Dangerous/wip/xrick-vs19"

# ensure Python
echo Adding Python directory to PATH:
echo PATH += $PATH_PYTHON
PATH=$PATH_PYTHON:$PATH
echo

# configure emsdk
echo Configuring EmSdk...
source "$PATH_EMSDK/emsdk_env.sh"

SRC="$PATH_XRICK/xrick/src"
INC="$PATH_XRICK/xrick/include"
DATA="$PATH_XRICK/data"

echo Compiling...
for f in "$SRC"/*.c; \
  do emcc "$f" -o $(basename -- "${f%.c}").bc \
    -I "$INC" \
    -s USE_SDL=2 \
    -D NOZLIB \
    -O2 \
    ; \
done
echo

echo Linking...
# -o .wasm .html .js
emcc *.bc -o xrick.js \
  -s USE_SDL=2 \
  -s FORCE_FILESYSTEM=1 \
  -O2 \
  --preload-file "$DATA@/data"
echo

echo Copy files...
cp "$PATH_XRICK/build/emsdk/player.js" player.js

echo Done.