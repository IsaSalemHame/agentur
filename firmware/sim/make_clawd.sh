#!/usr/bin/env bash
# Builds src/clawd_sprites.h from the Clawd 3D model (STL). Usage: sim/make_clawd.sh <path-to-stl>
# View: 14 degrees around the vertical axis and 9 degrees down, 94 px wide figure.
# Cut-offs in model units (mm): legs below 152.5 in height, arms outside 120.8/199.2 in width,
# eye pads above 35.3 are flattened (the eyes are drawn in scene.h). Only the four front legs
# (depth over 17.5) are kept; the model has four back legs as well.
set -euo pipefail
stl="${1:?path to obj_1_Clawd.stl}"
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "${tmp:?}"' EXIT
g++ -O2 -std=c++17 "$here/clawd_render.cpp" -o "$tmp/render"
for part in all body armL armR leg0 leg1 leg2 leg3; do
  "$tmp/render" "$stl" "$tmp/p_$part" 94 14 9 "$part" 152.5 +x+y+z 35.3 120.8 199.2 17.5
done
python3 -I "$here/make_sprites.py" "$tmp" "$here/../src/clawd_sprites.h" "$tmp/p_all.txt"
