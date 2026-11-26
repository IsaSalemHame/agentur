// The screen of the Claude clock: a mascot next to a laptop, three status rows and a wave at the bottom.
//
// Depends only on Adafruit_GFX, so the exact same code runs in the simulator on the PC
// (sim/) and on the clock. The clock does not have memory for a full frame (240x240x2 = 115 kB),
// so everything is drawn in strips of up to 240x40 pixels (19 kB) and sent strip by strip.
// Each strip draws the whole scene with clipping; drawAll skips whatever does not intersect it.
#pragma once
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "logo.h"
#include "clawd_sprites.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace scene {

enum Mood : uint8_t { SLEEP, WORK, REST, ASK, DONE };   // REST: nothing running, some jobs finished recently

struct View {
  Mood mood;
  uint32_t t;      // ms now
  uint32_t since;  // ms when the state began
  int work, done, ask;   // running / finished in the last 15 min / need an answer
  bool online;
};

// ---------- Colors (RGB888) ----------
static const uint32_t BG_TOP = 0x182540, BG_BOT = 0x0A1120, PANEL = 0x1D2A46, EDGE = 0x2A3A5C;
static const uint32_t TEXT = 0xF4F1EA, SOFT = 0x7F8AA3, ORANGE = 0xE07B53, ORANGE_DK = 0x9C4B2C;
static const uint32_t GREEN = 0x2BD18F, YELLOW = 0xF6BC33, BLUE = 0x4FA6F7, WHITE = 0xFFFFFF;
static const uint32_t FLOOR = 0x22304F, LID = 0x4A5672, LID_HI = 0x6A7898, LID_DK = 0x2E3850;
static const uint32_t CUP = 0xF3E9D6, CUP_DK = 0xCDBB9C, COFFEE = 0x6B4127, EYE = 0x141B2B;
static const uint32_t STAR = 0xFFF4C8, STEAM = 0xC9D4EA;

static inline uint16_t c565(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}
static inline uint32_t x888(uint16_t c) {
  uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
}
// a -> b, t 0..255
static inline uint32_t mix(uint32_t a, uint32_t b, int t) {
  if (t <= 0) return a;
  if (t >= 255) return b;
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return ((uint32_t)(ar + (br - ar) * t / 255) << 16) | ((uint32_t)(ag + (bg - ag) * t / 255) << 8) |
         (uint32_t)(ab + (bb - ab) * t / 255);
}
static inline float ease(float x) { return x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x); }
// p -> c with strength a (0..256), directly in 565. The ESP8266 has no hardware division.
static inline uint16_t blend565(uint16_t p, uint16_t c, int a) {
  int pr = p >> 11, pg = (p >> 5) & 63, pb = p & 31;
  int cr = c >> 11, cg = (c >> 5) & 63, cb = c & 31;
  pr += ((cr - pr) * a) >> 8; pg += ((cg - pg) * a) >> 8; pb += ((cb - pb) * a) >> 8;
  return (uint16_t)(pr << 11 | pg << 5 | pb);
}

// Sine from a table (256 steps per revolution). The chip has no floating-point unit, and sinf
// costs tens of microseconds. Time is computed in integers so the animation does not stutter
// after a long uptime (floats lose precision when millis() gets large).
static float SIN[256];
static bool sinReady = false;
static inline void sinInit() {
  if (sinReady) return;
  for (int i = 0; i < 256; i++) SIN[i] = sinf(i * 6.2831853f / 256);
  sinReady = true;
}
static inline float S(int idx) { return SIN[idx & 255]; }
static inline float C(int idx) { return SIN[(idx + 64) & 255]; }
static inline int ri(float rad) { return (int)(rad * 40.743665f); }          // radians -> index
static inline int tp(uint32_t t, float radPerMs) {                           // time -> index
  return (int)(((uint64_t)t * (uint32_t)(radPerMs * 40.743665f * 65536.0f)) >> 16);
}

// ---------- Strip canvas ----------
struct Rect { int16_t x, y, w, h; };

class Strip : public Adafruit_GFX {
 public:
  static const int CAP = 240 * 40;
  uint16_t buf[CAP];
  int16_t rx = 0, ry = 0, rw = 240, rh = 40;
  int16_t kx0 = 0, ky0 = 0, kx1 = 240, ky1 = 40;  // clip window, local coordinates
  Strip() : Adafruit_GFX(240, 240) {}
  void begin(int x, int y, int w, int h) { rx = x; ry = y; rw = w; rh = h; clip(); }
  // Clips drawing to r (in addition to the strip). Without argument: the whole strip.
  void clip() { kx0 = 0; ky0 = 0; kx1 = rw; ky1 = rh; }
  void clip(const Rect &r) {
    clip();
    if (r.x - rx > kx0) kx0 = r.x - rx;
    if (r.y - ry > ky0) ky0 = r.y - ry;
    if (r.x + r.w - rx < kx1) kx1 = r.x + r.w - rx;
    if (r.y + r.h - ry < ky1) ky1 = r.y + r.h - ry;
  }
  bool hit(int x, int y, int w, int h) const {
    return x < rx + kx1 && x + w > rx + kx0 && y < ry + ky1 && y + h > ry + ky0;
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    x -= rx; y -= ry;
    if (x >= kx0 && x < kx1 && y >= ky0 && y < ky1) buf[y * rw + x] = c;
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    y -= ry;
    if (y < ky0 || y >= ky1) return;
    x -= rx;
    if (x < kx0) { w -= kx0 - x; x = kx0; }
    if (x + w > kx1) w = kx1 - x;
    uint16_t *p = buf + y * rw + x;
    while (w-- > 0) *p++ = c;
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
    x -= rx;
    if (x < kx0 || x >= kx1) return;
    y -= ry;
    if (y < ky0) { h -= ky0 - y; y = ky0; }
    if (y + h > ky1) h = ky1 - y;
    uint16_t *p = buf + y * rw + x;
    while (h-- > 0) { *p = c; p += rw; }
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    for (int i = 0; i < h; i++) drawFastHLine(x, y + i, w, c);
  }
  void fillScreen(uint16_t c) override {
    for (int i = 0; i < rw * rh; i++) buf[i] = c;
  }
  // Translucent pixel: blends col in with strength a (0..255)
  void blend(int x, int y, uint32_t col, int a) { blend16(x, y, c565(col), a); }
  void blend16(int x, int y, uint16_t c, int a) {
    x -= rx; y -= ry;
    if (a <= 0 || x < kx0 || x >= kx1 || y < ky0 || y >= ky1) return;
    uint16_t &p = buf[y * rw + x];
    p = blend565(p, c, a >= 255 ? 256 : a);
  }
};

// ---------- Layout ----------
static const int HEAD_H = 28;
static const Rect STAGE = {0, 28, 240, 114};
static const Rect WAVE = {0, 221, 240, 19};
static inline int rowY(int i) { return 144 + i * 26; }
static const int ROW_H = 24;

static uint16_t bgAt(int y) { return c565(mix(BG_TOP, BG_BOT, y * 255 / 239)); }

// Soft glow: a disc with falling strength towards the edge
static void glow(Strip &g, int cx, int cy, int r, uint32_t col, int a) {
  if (a <= 0 || !g.hit(cx - r, cy - r, 2 * r + 1, 2 * r + 1)) return;
  int top = g.ry + g.ky0, bot = g.ry + g.ky1 - 1;
  int y0 = cy - r < top ? top - cy : -r, y1 = cy + r > bot ? bot - cy : r;
  int r2 = r * r;
  uint32_t inv = (255u << 16) / r2;
  uint16_t c = c565(col);
  for (int y = y0; y <= y1; y++)
    for (int x = -r; x <= r; x++) {
      int d2 = x * x + y * y;
      if (d2 >= r2) continue;
      int f = (int)(((uint32_t)(r2 - d2) * inv) >> 16);     // 255 in the middle, 0 at the edge
      g.blend16(cx + x, cy + y, c, ((a * f) >> 8) * f >> 8);
    }
}

static void thickLine(Strip &g, int x0, int y0, int x1, int y1, uint16_t c) {
  g.drawLine(x0, y0, x1, y1, c);
  g.drawLine(x0 + 1, y0, x1 + 1, y1, c);
  g.drawLine(x0, y0 + 1, x1, y1 + 1, c);
}

// ---------- Text with Nordic letters ----------
// The fonts are ASCII-only. The Norwegian letters (AE, O-slash, A-ring) are drawn as the base letter plus a ring or stroke.
static const GFXfont *const F_SMALL = &FreeSansBold9pt7b;
static const GFXfont *const F_BIG = &FreeSansBold12pt7b;

static int adv(const GFXfont *f, char c) {
  uint16_t first = pgm_read_word(&f->first), last = pgm_read_word(&f->last);
  if ((uint8_t)c < first || (uint8_t)c > last) return 0;
  const GFXglyph *gl = f->glyph;   // aligned 32-bit read, safe from flash on the ESP8266 too
  return pgm_read_byte(&gl[(uint8_t)c - first].xAdvance);
}

// mark: 0 none, 1 ring (a-ring), 2 stroke (o-slash), 3 ae ligature
static const char *nextCh(const char *s, char &c, uint8_t &mark) {
  mark = 0;
  uint8_t b = (uint8_t)*s;
  if (b == 0xC3 && s[1]) {
    uint8_t n = (uint8_t)s[1];
    switch (n) {
      case 0xA5: c = 'a'; mark = 1; break;
      case 0x85: c = 'A'; mark = 1; break;
      case 0xB8: c = 'o'; mark = 2; break;
      case 0x98: c = 'O'; mark = 2; break;
      case 0xA6: c = 'a'; mark = 3; break;
      case 0x86: c = 'A'; mark = 3; break;
      default: c = '?';
    }
    return s + 2;
  }
  c = b < 0x80 ? (char)b : '?';
  return s + 1;
}

static int textW(const GFXfont *f, const char *s) {
  int w = 0;
  while (*s) {
    char c; uint8_t m;
    s = nextCh(s, c, m);
    if (m == 3) w += adv(f, c) - 3 + adv(f, c == 'A' ? 'E' : 'e');
    else w += adv(f, c);
  }
  return w;
}

static void text(Strip &g, const GFXfont *f, int x, int y, const char *s, uint32_t col) {
  int big = f == F_BIG;
  if (!g.hit(x - 2, y - 22, textW(f, s) + 4, 28)) return;
  g.setFont(f);
  uint16_t c16 = c565(col);
  while (*s) {
    char c; uint8_t m;
    s = nextCh(s, c, m);
    if (m == 3) {
      g.drawChar(x, y, c, c16, c16, 1);
      x += adv(f, c) - 3;
      c = c == 'A' ? 'E' : 'e';
      m = 0;
    }
    g.drawChar(x, y, c, c16, c16, 1);
    int a = adv(f, c);
    bool up = c >= 'A' && c <= 'Z';
    if (m == 1) {
      int cx = x + a / 2 - 1, cy = y - (up ? 17 + big * 4 : 13 + big * 3);
      g.drawCircle(cx, cy, 2, c16);
      g.drawCircle(cx + 1, cy, 2, c16);
    } else if (m == 2) {
      int top = up ? 14 + big * 3 : 11 + big * 2;
      thickLine(g, x + 1, y + 1, x + a - 3, y - top, c16);
    }
    x += a;
  }
}

// ---------- Small shapes ----------
// The Claude logo, drawn from the alpha masks in logo.h (r is about half the width)
static void claudeMark(Strip &g, int cx, int cy, int r, uint32_t col) {
  const uint8_t *m = r >= 10 ? LOGO22 : r >= 7 ? LOGO16 : LOGO10;
  int n = r >= 10 ? 22 : r >= 7 ? 16 : 10;
  int x0 = cx - n / 2, y0 = cy - n / 2;
  if (!g.hit(x0, y0, n, n)) return;
  uint16_t c = c565(col);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) g.blend16(x0 + x, y0 + y, c, pgm_read_byte(&m[y * n + x]));
}

// The logo rotated by angle an (256 = one full turn), 22 px, sampled 2x2 from the 44 px mask
static void claudeMarkRotated(Strip &g, int cx, int cy, int an, uint32_t col) {
  const int n = 22;
  int x0 = cx - n / 2, y0 = cy - n / 2;
  if (!g.hit(x0, y0, n, n)) return;
  uint16_t c = c565(col);
  int ca = (int)(C(an) * 256), sa = (int)(S(an) * 256);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      int sum = 0;
      for (int k = 0; k < 4; k++) {
        int dx = x * 4 + (k & 1 ? 3 : 1) - 44, dy = y * 4 + (k & 2 ? 3 : 1) - 44;   // quarter pixels
        int u = 22 + ((ca * dx + sa * dy) >> 9), v = 22 + ((-sa * dx + ca * dy) >> 9);
        if ((unsigned)u < 44 && (unsigned)v < 44) sum += pgm_read_byte(&LOGO44[v * 44 + u]);
      }
      g.blend16(x0 + x, y0 + y, c, sum >> 2);
    }
}

static void sparkle(Strip &g, int x, int y, int len, uint32_t col, uint32_t under) {
  g.drawPixel(x, y, c565(col));
  for (int k = 1; k <= len; k++) {
    uint16_t c = c565(mix(under, col, 255 - k * 170 / (len + 1)));
    g.drawPixel(x + k, y, c); g.drawPixel(x - k, y, c);
    g.drawPixel(x, y + k, c); g.drawPixel(x, y - k, c);
  }
}

static void star5(Strip &g, int cx, int cy, float r, int rot, uint32_t col) {
  uint16_t c = c565(col);
  int px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float rr = i % 2 ? r * 0.45f : r;
    int an = rot + i * 256 / 10 - 64;
    px[i] = cx + (int)roundf(C(an) * rr);
    py[i] = cy + (int)roundf(S(an) * rr);
  }
  for (int i = 0; i < 10; i++) g.fillTriangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
}

static void quad(Strip &g, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
  g.fillTriangle(x0, y0, x1, y1, x2, y2, c);
  g.fillTriangle(x0, y0, x2, y2, x3, y3, c);
}

// ---------- Top bar ----------
static Rect dotRect(const View &v) {
  int w = textW(F_SMALL, v.online ? "Online" : "Offline");
  return {(int16_t)(230 - w - 22), 4, 22, 22};
}

static void header(Strip &g, const View &v) {
  // The logo spins slowly while Claude is working
  if (v.mood == WORK) claudeMarkRotated(g, 17, 14, tp(v.t, 0.0012f), ORANGE);
  else claudeMark(g, 17, 14, 10, ORANGE);
  text(g, F_BIG, 33, 22, "Claude", TEXT);
  const char *lbl = v.online ? "Online" : "Offline";
  int x = 230 - textW(F_SMALL, lbl);
  text(g, F_SMALL, x, 21, lbl, v.online ? GREEN : SOFT);
  int cx = x - 11, cy = 15;
  if (v.online) {
    float p = 0.5f + 0.5f * S(tp(v.t, 0.002f));
    glow(g, cx, cy, 10, GREEN, (int)(60 + 120 * p));
    g.fillCircle(cx, cy, 4, c565(GREEN));
  } else {
    g.fillCircle(cx, cy, 4, c565(SOFT));
  }
}

// ---------- The scene ----------
// The mascot is Clawd, rendered from the 3D model (sim/clawd_render.cpp) into sprites for
// the body, two arms and four legs, so each part can be moved separately. The eyes are drawn here.
static const int FX = 50, FY = 46;                  // top-left corner of the figure
static const int CUP_X = 30, CUP_BOT = 132;

struct Pose {
  int bob = 0, armL = 0, armR = 0, ex = 0, ey = 0;
  int leg[4] = {0, 0, 0, 0};
  uint8_t eyes = 0;   // 0 open, 1 closed line, 2 happy ^, 3 asleep
  float lift = 0;     // the cup: 0 on the table, 1 lifted
  int dim = 0;        // 0..255 towards the background (asleep)
};

static Pose pose(const View &v) {
  Pose p;
  uint32_t t = v.t, since = v.t - v.since;
  bool blink = (t % 4200) < 150;
  switch (v.mood) {
    case WORK: {                                   // types with the right arm, shuffles the legs
      bool look = (t % 8000) > 6800;
      p.armR = look ? 0 : ((t / 340) % 2 ? 3 : 0);
      p.ex = look ? 0 : 3; p.ey = look ? 0 : 2;
      static const uint8_t ORD[4] = {0, 2, 1, 3};
      uint32_t c = t % 2400;
      if (c < 1600) p.leg[ORD[c / 400]] = -2;       // one leg at a time is lifted slightly
      p.eyes = blink ? 1 : 0;
      break;
    }
    case REST: {                                   // resting, takes a sip of coffee now and then
      p.ex = (int)roundf(2.4f * S(tp(t, 0.0007f)));
      uint32_t c = t % 15000;
      if (c > 9000 && c < 13000)
        p.lift = c < 10000 ? ease((c - 9000) / 1000.0f) : c > 12000 ? 1 - ease((c - 12000) / 1000.0f) : 1;
      p.armL = -(int)(12 * p.lift);
      p.eyes = p.lift > 0.85f ? 2 : blink ? 1 : 0;
      if (p.lift > 0.05f) p.ex = -3;
      p.bob = S(tp(t, 0.0011f)) > 0.7f ? -1 : 0;
      break;
    }
    case ASK:                                      // waves with the right arm
      p.bob = -(int)(fabsf(S(tp(t, 0.0017f))) * 3);       // one hop every 1.8 s
      p.armR = -12 + (int)(5 * S(tp(t, 0.0052f)));        // one wave every 1.2 s
      p.ey = -2;
      p.eyes = blink ? 1 : 0;
      break;
    case DONE: {                                   // jumps for joy, legs dangling
      float amp = since < 2500 ? 8 : 2;
      float h = fabsf(S(tp(t, 0.0045f)));
      p.bob = -(int)(h * amp);
      p.armL = p.armR = -10 + (int)(3 * S(tp(t, 0.008f)));
      for (int i = 0; i < 4; i++) p.leg[i] = -(int)(h * amp * 0.3f) * (i & 1);   // every other leg kicks up (never down, that would leave a gap)
      p.eyes = 2;
      p.ey = -1;
      break;
    }
    default:                                       // asleep
      p.bob = S(tp(t, 0.0012f)) > 0 ? 0 : 1;
      p.eyes = 3;
      p.dim = 110;
  }
  return p;
}

// The colors for the 16 values in the sprites: 1-12 brightness levels, 13-15 edge pixels
static void clawdPalette(uint16_t *pal, int dim) {
  for (int i = 1; i <= 12; i++) {
    float s = 0.30f + (i - 1) / 11.0f * 0.70f;
    // The face sits around s = 0.87 and gets pure Claude orange; the edges above that only get slightly lighter
    uint32_t c = s < 0.86f ? mix(0x6A2C14, ORANGE, (int)((s - 0.30f) / 0.56f * 255))
                           : mix(ORANGE, 0xF0A47E, (int)((s - 0.86f) / 0.14f * 255));
    pal[i] = c565(mix(c, BG_BOT, dim));
  }
  for (int i = 13; i <= 15; i++) pal[i] = c565(mix(ORANGE_DK, BG_BOT, dim));
}

static void blit(Strip &g, const ClawdSprite &sp, int ox, int oy, const uint16_t *pal) {
  int x0 = ox + sp.x, y0 = oy + sp.y;
  if (!g.hit(x0, y0, sp.w, sp.h)) return;
  int ya = g.ry + g.ky0 - y0, yb = g.ry + g.ky1 - y0;
  if (ya < 0) ya = 0;
  if (yb > sp.h) yb = sp.h;
  for (int y = ya; y < yb; y++) {
    const uint8_t *row = sp.px + y * sp.w / 2;
    for (int x = 0; x < sp.w; x += 2) {
      uint8_t b = pgm_read_byte(&row[x / 2]);
      for (int k = 0; k < 2; k++) {
        uint8_t v = k ? b & 15 : b >> 4;
        if (!v) continue;
        if (v < 13) g.drawPixel(x0 + x + k, y0 + y, pal[v]);
        else g.blend16(x0 + x + k, y0 + y, pal[v], v == 13 ? 80 : v == 14 ? 140 : 205);
      }
    }
  }
}

static void mascot(Strip &g, const Pose &p) {
  uint16_t pal[16];
  clawdPalette(pal, p.dim);
  int ox = FX, oy = FY + p.bob;
  // Shadow on the floor
  for (int j = -3; j <= 3; j++)
    for (int x = -46; x <= 46; x++) g.blend16(FX + 47 + x, 131 + j, 0x0000, 70 - x * x * 70 / 2116 - j * j * 6);
  // Back to front: right arm (far side), legs, body, left arm (nearest)
  blit(g, CLAWD_ARMR, ox, oy + p.armR, pal);
  blit(g, CLAWD_LEG3, ox, oy + p.leg[3], pal);
  blit(g, CLAWD_LEG2, ox, oy + p.leg[2], pal);
  blit(g, CLAWD_LEG1, ox, oy + p.leg[1], pal);
  blit(g, CLAWD_LEG0, ox, oy + p.leg[0], pal);
  blit(g, CLAWD_BODY, ox, oy, pal);
  blit(g, CLAWD_ARML, ox, oy + p.armL, pal);
  // Eyes
  uint16_t e = c565(EYE), wt = c565(mix(WHITE, BG_BOT, p.dim));
  for (int k = 0; k < 2; k++) {
    int ex = ox + (k ? CLAWD_EYE1_X : CLAWD_EYE0_X) - 4 + p.ex, ey = oy + (k ? CLAWD_EYE1_Y : CLAWD_EYE0_Y) - 7 + p.ey;
    switch (p.eyes) {
      case 0:
        g.fillRoundRect(ex, ey, 8, 15, 2, e);
        g.fillRect(ex + 2, ey + 2, 2, 3, wt);                      // highlight
        break;
      case 1: g.fillRect(ex, ey + 7, 8, 3, e); break;
      case 2:
        thickLine(g, ex, ey + 10, ex + 4, ey + 5, e);
        thickLine(g, ex + 4, ey + 5, ex + 8, ey + 10, e);
        break;
      default: g.fillRect(ex - 1, ey + 9, 10, 2, e);
    }
  }
}

// Laptop in front of Clawd on the right, screen facing Clawd: we see the lid from behind.
// The lid is tall enough that Clawd stands behind it, not hovering above it.
static void laptop(Strip &g, const View &v, const Pose &) {
  if (v.mood == WORK) glow(g, 172, 72, 34, BLUE, 55);           // light from the screen
  // Keyboard part: surface and front edge with thickness
  quad(g, 118, 128, 218, 124, 224, 133, 112, 137, c565(mix(LID, LID_DK, 90)));
  quad(g, 112, 137, 224, 133, 224, 137, 112, 141, c565(LID_DK));
  g.drawLine(118, 128, 218, 124, c565(LID_HI));
  // The lid from behind: edge (thickness), face, light top and dark hinge
  quad(g, 133, 77, 209, 69, 207, 127, 128, 130, c565(LID_HI));
  quad(g, 136, 78, 212, 70, 210, 127, 131, 130, c565(LID));
  g.drawLine(136, 78, 212, 70, c565(mix(LID, LID_HI, 170)));
  g.drawLine(212, 70, 210, 127, c565(mix(LID, LID_DK, 110)));
  g.drawLine(131, 129, 210, 126, c565(LID_DK));
  g.drawLine(131, 130, 210, 127, c565(LID_DK));
  uint32_t logo = mix(ORANGE_DK, ORANGE, 150);
  if (v.mood == WORK) logo = mix(ORANGE_DK, ORANGE, (int)(150 + 105 * S(tp(v.t, 0.0025f))));
  if (v.mood == SLEEP) logo = mix(LID, ORANGE_DK, 120);
  claudeMark(g, 172, 100, 8, logo);
}

static void cup(Strip &g, const View &v, float lift) {
  int cx = CUP_X + (int)roundf(24 * lift), bot = CUP_BOT - (int)roundf(30 * lift);
  bool sl = v.mood == SLEEP;
  uint16_t cc = c565(sl ? mix(CUP, BG_BOT, 90) : CUP), cd = c565(sl ? mix(CUP_DK, BG_BOT, 90) : CUP_DK);
  g.drawCircle(cx + 13, bot - 13, 6, cc);                          // handle
  g.drawCircle(cx + 13, bot - 13, 5, cc);
  g.drawCircle(cx + 13, bot - 13, 4, cd);
  g.fillRoundRect(cx - 13, bot - 25, 26, 25, 6, cc);
  for (int k = 0; k < 5; k++) g.drawFastVLine(cx + 12 - k, bot - 22, 19, c565(mix(sl ? mix(CUP, BG_BOT, 90) : CUP, CUP_DK, 60 + k * 30)));
  g.fillRoundRect(cx - 11, bot - 25, 22, 5, 2, c565(COFFEE));
  claudeMark(g, cx - 1, bot - 11, 5, sl ? ORANGE_DK : ORANGE);
  if (sl) return;
  int ph = tp(v.t, 0.0025f);                                         // steam
  uint16_t sc = c565(STEAM);
  for (int i = 0; i < 2; i++)
    for (int k = 0; k < 14; k++) {
      int y = bot - 29 - k * 2;
      int x = cx - 4 + i * 8 + (int)roundf(2.5f * S(k * 22 - ph + i * 86));
      int a = 190 - k * 13;
      g.blend16(x, y, sc, a); g.blend16(x + 1, y, sc, a / 2);
      g.blend16(x, y + 1, sc, a / 2);
    }
}

static void stars(Strip &g, const View &v) {
  static const uint8_t POS[][2] = {{18, 52}, {62, 38}, {150, 36}, {216, 44}, {226, 100}, {204, 122}, {14, 104}};
  for (unsigned i = 0; i < sizeof(POS) / sizeof(POS[0]); i++) {
    float b = 0.5f + 0.5f * S(tp(v.t, 0.0011f * (1 + i % 3 * 0.4f)) + i * 69);
    if (v.mood == SLEEP) b *= 0.5f;
    uint32_t under = mix(BG_TOP, BG_BOT, POS[i][1] * 255 / 239);
    sparkle(g, POS[i][0], POS[i][1], 2 + (int)roundf(2 * b), mix(under, STAR, (int)(70 + 185 * b)), under);
  }
}

static void fillEllipse(Strip &g, int cx, int cy, int rx, int ry, uint16_t c) {
  for (int y = -ry; y <= ry; y++) {
    int w = (int)roundf(rx * sqrtf(1.0f - (float)(y * y) / (ry * ry)));
    g.drawFastHLine(cx - w, cy + y, 2 * w + 1, c);
  }
}

static void floorShape(Strip &g) {
  fillEllipse(g, 120, 135, 110, 7, c565(FLOOR));
  g.drawFastHLine(30, 128, 180, c565(mix(FLOOR, EDGE, 120)));
}

static void extras(Strip &g, const View &v, const Pose &p) {
  uint32_t t = v.t, since = v.t - v.since;
  switch (v.mood) {
    case WORK: {                                       // thick speed lines by the head
      int ph = (t / 520) % 3;
      for (int i = 0; i < 3; i++) {
        int an = -54 + i * 22;
        int r0 = 12 + (i == ph ? 2 : 0), r1 = r0 + (i == ph ? 12 : 9);
        int ox = FX + 92, oy = FY + 2 + p.bob;
        uint16_t c = c565(i == ph ? ORANGE : mix(ORANGE, BG_TOP, 70));
        for (int r = r0; r <= r1; r++) g.fillCircle(ox + (int)(C(an) * r), oy + (int)(S(an) * r), 2, c);
      }
      break;
    }
    case REST: break;                                  // just coffee and a calm gaze
    case ASK: {                                        // speech bubble with a question mark
      int cx = 196, cy = 52 + p.bob / 2;
      glow(g, cx, cy, 26, BLUE, 90);
      uint16_t b = c565(BLUE);
      g.fillTriangle(cx - 12, cy + 8, cx - 4, cy + 13, cx - 24, cy + 22, b);
      g.fillCircle(cx, cy, 16, b);
      text(g, F_BIG, cx - adv(F_BIG, '?') / 2, cy + 8, "?", WHITE);
      break;
    }
    case DONE: {                                       // star and confetti
      float in = ease(since / 450.0f);
      int sy = 36 - (int)((1 - in) * 30);
      float r = 11 + 2 * S(tp(t, 0.004f));
      glow(g, 105, sy, 22, YELLOW, 120);
      star5(g, 105, sy, r, tp(t, 0.0008f), YELLOW);
      star5(g, 105, sy, r * 0.45f, tp(t, 0.0008f), STAR);
      static const uint32_t CC[4] = {ORANGE, YELLOW, GREEN, BLUE};
      for (int i = 0; i < 12; i++) {
        int x = 10 + (i * 53 + 17) % 220;
        int y = 30 + (int)fmodf(since * 0.025f * (1 + i % 3 * 0.3f) + i * 29, 108);
        if (y > 30 && y < 138) g.fillRect(x + (int)(3 * S(tp(since, 0.003f) + i * 41)), y, 3, 3, c565(CC[i % 4]));
      }
      sparkle(g, 80, 30 + (t / 650) % 2 * 6, 2, STAR, BG_TOP);
      sparkle(g, 128, 34 + (t / 650 + 1) % 2 * 6, 2, STAR, BG_TOP);
      break;
    }
    default: {                                         // zzz
      g.setFont(NULL);
      for (int k = 0; k < 3; k++) {
        float ph = ((t + k * 1500) % 4500) / 4500.0f;
        int s = 1 + k % 2;
        uint16_t c = c565(mix(BG_TOP, SOFT, (int)(255 * S((int)(ph * 128)))));
        g.drawChar(158 + (int)(ph * 14), 56 - (int)(ph * 24), 'z', c, c, s);
      }
    }
  }
}

static void stage(Strip &g, const View &v) {
  Pose p = pose(v);
  floorShape(g);
  stars(g, v);
  mascot(g, p);
  laptop(g, v, p);
  cup(g, v, p.lift);
  extras(g, v, p);
}

// ---------- The status rows ----------
static Rect iconRect(int i) { return {14, (int16_t)rowY(i), 27, ROW_H}; }

static void row(Strip &g, const View &v, int i) {
  int y = rowY(i), n = i == 0 ? v.work : i == 1 ? v.done : v.ask;
  uint32_t col = i == 0 ? ORANGE : i == 1 ? GREEN : BLUE;
  bool on = n > 0;
  if (g.hit(10, y, 220, ROW_H)) {
    g.fillRoundRect(10, y, 220, ROW_H, 12, c565(PANEL));
    g.drawRoundRect(10, y, 220, ROW_H, 12, c565(on ? mix(PANEL, col, 110) : EDGE));
  }
  int cx = 27, cy = y + 12;
  float p = 0.5f + 0.5f * S(tp(v.t, 0.002f) + i * 81);
  if (on) glow(g, cx, cy, 12, col, (int)(50 + 110 * p));
  uint32_t ic = on ? col : mix(PANEL, col, 80);
  g.fillCircle(cx, cy, 9, c565(ic));
  uint16_t w = c565(on ? WHITE : mix(PANEL, WHITE, 110)), d = c565(EYE);
  (void)d;
  if (i == 0) {                                                            // spinner: work in progress
    int base = on ? tp(v.t, 0.004f) : 40;
    for (int k = 0; k < 10; k++) {
      int an = base - k * 19;
      int px = cx + (int)roundf(C(an) * 5), py = cy + (int)roundf(S(an) * 5);
      int a = on ? 255 - k * 24 : 170 - k * 15;
      g.blend16(px, py, w, a); g.blend16(px + 1, py, w, a);
      g.blend16(px, py + 1, w, a); g.blend16(px + 1, py + 1, w, a);
    }
  } else if (i == 1) {                                                     // checkmark: finished
    thickLine(g, cx - 5, cy, cx - 2, cy + 3, w);
    thickLine(g, cx - 2, cy + 3, cx + 4, cy - 4, w);
  } else {                                                                 // speech bubble
    g.fillRoundRect(cx - 6, cy - 5, 13, 9, 3, w);
    g.fillTriangle(cx - 3, cy + 3, cx - 5, cy + 7, cx + 1, cy + 3, w);
    int lit = on ? (v.t / 550) % 3 : -1;
    for (int k = 0; k < 3; k++) g.fillRect(cx - 4 + k * 4, cy - 1 - (k == lit ? 1 : 0), 2, 2, c565(ic));
  }
  if (!g.hit(42, y, 188, ROW_H)) return;
  char s[32];
  if (i == 0) on ? snprintf(s, sizeof s, "%d working", n) : snprintf(s, sizeof s, "Nothing running");
  else if (i == 1) on ? snprintf(s, sizeof s, "%d just finished", n) : snprintf(s, sizeof s, "None finished");
  else on ? snprintf(s, sizeof s, n == 1 ? "%d needs you" : "%d need you", n) : snprintf(s, sizeof s, "No questions");
  text(g, F_SMALL, 44, y + 17, s, on ? TEXT : SOFT);
  if (i == 2 && on) {
    uint16_t c = c565(TEXT);
    thickLine(g, 211, y + 7, 216, y + 12, c);
    thickLine(g, 216, y + 12, 211, y + 17, c);
  }
}

// ---------- The wave ----------
// Two layers: a weaker yellow wave behind and a glowing orange one in front, with a glow around it.
static void wave(Strip &g, const View &v) {
  float sp = v.mood == SLEEP ? 0.0005f : v.mood == WORK || v.mood == DONE ? 0.0016f : 0.0011f;
  int str = v.mood == SLEEP ? 120 : 255;
  int p1 = tp(v.t, sp), p2 = tp(v.t, sp * 1.7f), p3 = tp(v.t, sp * 1.3f);
  uint16_t cy = c565(YELLOW), co = c565(ORANGE), ck = c565(0xFFE2B8);
  static int16_t wy[15], oy[11];                                  // strength per distance from the line
  for (int d = 0; d < 15; d++) wy[d] = d <= 1 ? 255 : d - 2 < 6 ? 180 - (d - 2) * 30 : 0;
  for (int d = 0; d < 11; d++) oy[d] = d == 0 ? 210 : 110 - d * 20 > 0 ? 110 - d * 20 : 0;
  for (int x = 0; x < 240; x++) {
    float y1 = 230.5f + 3.2f * S(x * 183 / 100 + p1) + 0.9f * S(x * 530 / 100 - p2);
    float y2 = 231.5f + 2.6f * S(x * 244 / 100 - p3 + 61);
    int a = str * (90 + (int)(165 * S(x * 128 / 239))) >> 8;     // weaker towards the edges
    int i2 = (int)(y2 + 0.5f), i1 = (int)(y1 + 0.5f);
    for (int dy = -5; dy <= 5; dy++) g.blend16(x, i2 + dy, cy, a * oy[dy < 0 ? -dy : dy] >> 8);
    for (int dy = -7; dy <= 7; dy++) g.blend16(x, i1 + dy, co, a * wy[dy < 0 ? -dy : dy] >> 8);
    g.blend16(x, i1, ck, a * 190 >> 8);                            // bright core
  }
}

// ---------- Putting it together ----------
static void drawAll(Strip &g, const View &v) {
  sinInit();
  for (int y = g.ry; y < g.ry + g.rh; y++) g.drawFastHLine(g.rx, y, g.rw, bgAt(y));
  g.clip({0, 0, 240, HEAD_H});
  if (g.hit(0, 0, 240, HEAD_H)) header(g, v);
  g.clip(STAGE);
  if (g.hit(STAGE.x, STAGE.y, STAGE.w, STAGE.h)) stage(g, v);
  for (int i = 0; i < 3; i++) {
    g.clip({10, (int16_t)rowY(i), 220, ROW_H});
    if (g.hit(10, rowY(i), 220, ROW_H)) row(g, v, i);
  }
  g.clip(WAVE);
  if (g.hit(WAVE.x, WAVE.y, WAVE.w, WAVE.h)) wave(g, v);
  g.clip();
}

// Draws the rectangle r in strips and hands each strip to push(buf, x, y, w, h).
template <class Push>
static void render(Strip &g, const View &v, Rect r, Push push) {
  int rows = Strip::CAP / r.w;
  for (int y = r.y; y < r.y + r.h; y += rows) {
    int h = r.y + r.h - y < rows ? r.y + r.h - y : rows;
    g.begin(r.x, y, r.w, h);
    drawAll(g, v);
    push(g.buf, r.x, y, r.w, h);
  }
}

// The areas that change from frame to frame (the rest is only drawn when the state changes)
static int animRects(const View &v, Rect *out) {
  int n = 0;
  out[n++] = STAGE;
  out[n++] = {5, 2, 24, 24};             // the logo in the top bar
  out[n++] = dotRect(v);
  for (int i = 0; i < 3; i++) out[n++] = iconRect(i);
  out[n++] = WAVE;
  return n;
}

static const Rect FULL = {0, 0, 240, 240};

}  // namespace scene
