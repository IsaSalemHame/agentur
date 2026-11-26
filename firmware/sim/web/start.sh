#!/usr/bin/env bash
# Builds and starts the web simulator: sim/web/start.sh [port]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
port="${1:-8791}"
gfx="$here/../../.pio/libdeps/agentur/Adafruit GFX Library"
bin="${XDG_CACHE_HOME:-$HOME/.cache}/agentur-sim"
mkdir -p "$bin"
g++ -std=c++17 -O2 -Wno-unused-function -DARDUINO=100 -I"$here/../stub" -I"$gfx" "$here/simd.cpp" "$gfx/Adafruit_GFX.cpp" -o "$bin/simd"
exec python3 -I "$here/server.py" "$port" "$bin/simd"
