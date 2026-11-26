// Screen simulator: runs scene.h on the PC and writes PPM images.
//   ./sim <output-dir>
// Produces one still image per state, frames for the animations, and checks that a
// partial update (animated regions only) gives exactly the same image as a full redraw.
#include <stdio.h>
#include <vector>
#include "../src/scene.h"
using namespace scene;

static uint16_t fb[240 * 240];
static Strip strip;

static void push(uint16_t *b, int x, int y, int w, int h) {
  for (int j = 0; j < h; j++) memcpy(&fb[(y + j) * 240 + x], &b[j * w], w * 2);
}
static void full(const View &v) { render(strip, v, FULL, push); }
static void partial(const View &v) {
  Rect r[8];
  int n = animRects(v, r);
  for (int i = 0; i < n; i++) render(strip, v, r[i], push);
}
static void save(const char *path, const uint16_t *f) {
  FILE *o = fopen(path, "wb");
  fprintf(o, "P6 240 240 255\n");
  for (int i = 0; i < 240 * 240; i++) {
    uint32_t c = x888(f[i]);
    fputc(c >> 16, o); fputc((c >> 8) & 255, o); fputc(c & 255, o);
  }
  fclose(o);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  struct Case { const char *name; View v; } cases[] = {
      {"working", {WORK, 0, 0, 2, 1, 0, true}},
      {"resting", {REST, 0, 0, 0, 2, 0, true}},
      {"needs-you", {ASK, 0, 0, 1, 1, 1, true}},
      {"done", {DONE, 0, 0, 0, 1, 0, true}},
      {"sleeping", {SLEEP, 0, 0, 0, 0, 0, false}},
  };
  char path[512];
  int bad = 0;
  for (auto &c : cases) {
    View v = c.v;
    v.since = 100000;
    // Still image in the middle of a typical sequence
    v.t = v.since + (v.mood == REST ? 10500 : 1300);
    full(v);
    snprintf(path, sizeof path, "%s/%s.ppm", dir, c.name);
    save(path, fb);
    // Animation: 4 s with 50 ms between frames (resting: 15 s to include the coffee sip)
    int frames = v.mood == REST ? 150 : 80, step = v.mood == REST ? 100 : 50;
    v.t = v.since;
    full(v);
    for (int k = 0; k < frames; k++) {
      v.t = v.since + k * step;
      partial(v);
      snprintf(path, sizeof path, "%s/%s_%03d.ppm", dir, c.name, k);
      save(path, fb);
      // A partial update must give the same image as a full redraw
      static uint16_t ref[240 * 240];
      memcpy(ref, fb, sizeof ref);
      full(v);
      int diff = 0;
      for (int i = 0; i < 240 * 240; i++) diff += ref[i] != fb[i];
      if (diff) { bad++; if (bad < 6) printf("%s t=%u: %d pixels differ\n", c.name, v.t, diff); }
      memcpy(fb, ref, sizeof ref);
    }
  }
  printf(bad ? "ERROR: %d frames differ\n" : "OK: partial update = full redraw in all frames\n", bad);
  return bad ? 1 : 0;
}
