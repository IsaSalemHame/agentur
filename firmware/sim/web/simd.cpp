// Draws screen images on demand for the web simulator (server.py).
// Reads lines "mood t since working done questions online" from stdin and writes
// a BMP image (240x240) prefixed with a 4-byte length to stdout.
#include <stdio.h>
#include <vector>
#include "../../src/scene.h"
using namespace scene;

static uint16_t fb[240 * 240];
static Strip strip;
static void push(uint16_t *b, int x, int y, int w, int h) {
  for (int j = 0; j < h; j++) memcpy(&fb[(y + j) * 240 + x], &b[j * w], w * 2);
}

int main() {
  int mood; unsigned t, since; int w, d, a, on;
  std::vector<unsigned char> bmp(54 + 240 * 240 * 3);
  unsigned char *h = bmp.data();
  memset(h, 0, 54);
  uint32_t fs = bmp.size(), off = 54, hs = 40, wd = 240, ht = 240, img = 240 * 240 * 3;
  h[0] = 'B'; h[1] = 'M'; memcpy(h + 2, &fs, 4); memcpy(h + 10, &off, 4); memcpy(h + 14, &hs, 4);
  memcpy(h + 18, &wd, 4); memcpy(h + 22, &ht, 4); h[26] = 1; h[28] = 24; memcpy(h + 34, &img, 4);
  while (scanf("%d %u %u %d %d %d %d", &mood, &t, &since, &w, &d, &a, &on) == 7) {
    View v{(Mood)mood, t, since, w, d, a, on != 0};
    render(strip, v, FULL, push);
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 240; x++) {
        uint32_t c = x888(fb[(239 - y) * 240 + x]);
        unsigned char *p = h + 54 + (y * 240 + x) * 3;
        p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = c >> 16;
      }
    fwrite(&fs, 4, 1, stdout);
    fwrite(bmp.data(), 1, bmp.size(), stdout);
    fflush(stdout);
  }
  return 0;
}
