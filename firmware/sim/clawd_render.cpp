// Renders the Clawd figure from the STL file to images (PPM with color, PGM with coverage).
// Runs on the PC; the result is packed into sprites for the clock by make_sprites.py.
//
//   ./clawd_render <stl> <out-prefix> <width> <yaw> <pitch> [part] [leg-limit] [axes] [eye-limit]
//
// part: all | body | legs | eyes   (which triangles are drawn)
// axes: which model axes are up, right and toward the viewer, e.g. "+x+y+z" (default,
//   the Bambu model lying on its back) or "+z+x-y" (upright model).
// leg-limit: everything entirely below this height (along the up axis) counts as legs.
// eye-limit: triangles sticking out further toward the viewer than this are eyes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <map>
#include <tuple>

struct V { float x, y, z; };
struct T { V a, b, c; int part; };   // part: 0 body, 2 eye, 10/11 arm left/right, 20..23 legs


int main(int argc, char **argv) {
  if (argc < 6) { fprintf(stderr, "usage: %s stl prefix width yaw pitch [part] [legx]\n", argv[0]); return 1; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror("stl"); return 1; }
  std::vector<unsigned char> d;
  { unsigned char buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n); }
  fclose(f);
  uint32_t n; memcpy(&n, &d[80], 4);
  std::vector<T> tris(n);
  for (uint32_t i = 0; i < n; i++) {
    float v[12]; memcpy(v, &d[84 + 50 * i], 48);
    tris[i].a = {v[3], v[4], v[5]}; tris[i].b = {v[6], v[7], v[8]}; tris[i].c = {v[9], v[10], v[11]};
    tris[i].part = 0;
  }
  // Convert to (up, right, toward the viewer)
  std::string ax = argc > 8 ? argv[8] : "+x+y+z";
  auto comp = [&](V p, int k) {
    float sg = ax[2 * k] == '-' ? -1 : 1; char c = ax[2 * k + 1];
    return sg * (c == 'x' ? p.x : c == 'y' ? p.y : p.z);
  };
  for (auto &t : tris)
    for (V *p : {&t.a, &t.b, &t.c}) *p = V{comp(*p, 0), comp(*p, 1), comp(*p, 2)};
  // The parts are separated per pixel by where in the figure the point lies:
  // below legTop = legs (four, by side position), outside armLo/armHi = arm, otherwise body.
  float legTop = argc > 7 ? atof(argv[7]) : -1e9;
  float eyeT = argc > 9 ? atof(argv[9]) : 1e9;
  float armLo = argc > 10 ? atof(argv[10]) : -1e9, armHi = argc > 11 ? atof(argv[11]) : 1e9;
  float legZ = argc > 12 ? atof(argv[12]) : -1e9;   // legs behind this depth are left out
  float rLo = 1e9, rHi = -1e9;
  for (auto &t : tris) for (V p : {t.a, t.b, t.c}) { rLo = std::min(rLo, p.y); rHi = std::max(rHi, p.y); }
  // The eye pads are marked (to find where the eyes are) and pressed flat into the face
  for (auto &t : tris)
    if (std::max({t.a.z, t.b.z, t.c.z}) > eyeT) {
      t.part = 2;
      for (V *p : {&t.a, &t.b, &t.c}) if (p->z > eyeT - 0.5f) p->z = eyeT - 0.5f;
    }
  auto fragPart = [&](V p) {
    if (p.y < armLo) return 10;
    if (p.y > armHi) return 11;
    if (p.x < legTop) {
      if (p.z < legZ) return -1;                     // the model has legs at the back too; only the four at the front are used
      return p.y < 136 ? 20 : p.y < 160 ? 21 : p.y < 184 ? 22 : 23;   // the legs stand at y 123-131, 141-149, 171-179, 188-196
    }
    return 0;
  };
  int W = atoi(argv[3]);
  float yaw = atof(argv[4]) * M_PI / 180, pitch = atof(argv[5]) * M_PI / 180;
  std::string part = argc > 6 ? argv[6] : "all";

  // Center from the extent. After the conversion x is up, y is right, z is toward the viewer.
  float lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
  for (auto &t : tris) for (V p : {t.a, t.b, t.c}) {
    float c3[3] = {p.x, p.y, p.z};
    for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], c3[k]); hi[k] = std::max(hi[k], c3[k]); }
  }
  const float cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2, cz = (lo[2] + hi[2]) / 2;
  auto view = [&](V p) {
    float X = p.y - cy, Y = -(p.x - cx), Z = p.z - cz;
    float X2 = X * cosf(yaw) + Z * sinf(yaw), Z2 = -X * sinf(yaw) + Z * cosf(yaw);   // around the vertical axis
    float Y3 = Y * cosf(pitch) - Z2 * sinf(pitch), Z3 = Y * sinf(pitch) + Z2 * cosf(pitch);
    return V{X2, Y3, Z3};
  };
  // Find the extent with all parts, so all layers get the same placement
  float mnx = 1e9, mxx = -1e9, mny = 1e9, mxy = -1e9;
  for (auto &t : tris) for (V p : {t.a, t.b, t.c}) { V q = view(p); mnx = std::min(mnx, q.x); mxx = std::max(mxx, q.x); mny = std::min(mny, q.y); mxy = std::max(mxy, q.y); }
  const int SS = 4;                                      // supersampling
  float sc = (W - 2) * SS / (mxx - mnx);
  int H = (int)ceilf((mxy - mny) * sc / SS) + 2;
  int w = W * SS, h = H * SS;
  std::vector<float> zb(w * h, -1e9f);
  std::vector<float> shade(w * h, 0);
  std::vector<int> pid(w * h, -1);
  V L = {-0.35f, -0.45f, 0.82f};                         // light from the upper left, mostly from the front
  float ll = sqrtf(L.x * L.x + L.y * L.y + L.z * L.z); L = {L.x / ll, L.y / ll, L.z / ll};

  // Rounded edges: the narrow bevel faces get normals that blend from neighbor to neighbor,
  // so the shading goes smoothly from the front to the side instead of one bright stripe.
  auto norm = [](V n) { float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z); return l > 0 ? V{n.x / l, n.y / l, n.z / l} : n; };
  auto dot = [](V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  std::vector<V> fn(tris.size());
  std::vector<char> narrow(tris.size(), 0);
  std::map<std::tuple<int, int, int>, std::vector<int>> at;
  auto key = [](V p) { return std::make_tuple((int)lroundf(p.x * 100), (int)lroundf(p.y * 100), (int)lroundf(p.z * 100)); };
  for (size_t i = 0; i < tris.size(); i++) {
    T &t = tris[i];
    V a = view(t.a), b = view(t.b), c = view(t.c);
    V u = {b.x - a.x, b.y - a.y, b.z - a.z}, v = {c.x - a.x, c.y - a.y, c.z - a.z};
    V cr = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    float area2 = sqrtf(dot(cr, cr));
    fn[i] = norm(cr);
    float e1 = sqrtf(dot(u, u)), e2 = sqrtf(dot(v, v));
    V wv = {c.x - b.x, c.y - b.y, c.z - b.z}; float e3 = sqrtf(dot(wv, wv));
    float longest = std::max({e1, e2, e3});
    narrow[i] = longest > 0 && area2 / longest < 2.6f;      // height under 2.6 mm: bevel
    for (V p : {t.a, t.b, t.c}) at[key(p)].push_back((int)i);
  }
  auto cornerN = [&](size_t i, V p) {
    std::vector<V> seen;
    V sum = {0, 0, 0};
    for (int f : at[key(p)]) {
      if (dot(fn[f], fn[i]) < 0.2f) continue;
      bool dup = false;
      for (V q : seen) if (dot(q, fn[f]) > 0.995f) { dup = true; break; }
      if (dup) continue;
      seen.push_back(fn[f]);
      sum = {sum.x + fn[f].x, sum.y + fn[f].y, sum.z + fn[f].z};
    }
    return norm(sum);
  };
  auto light = [&](V n) {
    if (n.z < 0) n = {-n.x, -n.y, -n.z};                 // two-sided
    float s = 0.30f + 0.70f * std::max(0.f, dot(n, L));
    return s > 0.87f ? 0.87f + (s - 0.87f) * 0.35f : s;  // tone down anything brighter than the face (no white edges)
  };
  for (size_t ti = 0; ti < tris.size(); ti++) {
    T &t = tris[ti];
    V na = fn[ti], nb = fn[ti], nc = fn[ti];
    if (narrow[ti]) { na = cornerN(ti, t.a); nb = cornerN(ti, t.b); nc = cornerN(ti, t.c); }
    int wantId = part == "body" ? 0 : part == "armL" ? 10 : part == "armR" ? 11
               : (part.size() == 4 && part.compare(0, 3, "leg") == 0) ? 20 + (part[3] - '0') : -1;
    V a = view(t.a), b = view(t.b), c = view(t.c);
    V u = {b.x - a.x, b.y - a.y, b.z - a.z}, v = {c.x - a.x, c.y - a.y, c.z - a.z};
    V nn = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    float nl = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z); if (nl == 0) continue;
    nn = {nn.x / nl, nn.y / nl, nn.z / nl};
    if (nn.z < 0) nn = {-nn.x, -nn.y, -nn.z};             // two-sided
    (void)nn;
    auto P = [&](V p) { return V{(p.x - mnx) * sc + SS, (p.y - mny) * sc + SS, p.z}; };
    V A = P(a), B = P(b), C = P(c);
    int x0 = std::max(0, (int)floorf(std::min({A.x, B.x, C.x}))), x1 = std::min(w - 1, (int)ceilf(std::max({A.x, B.x, C.x})));
    int y0 = std::max(0, (int)floorf(std::min({A.y, B.y, C.y}))), y1 = std::min(h - 1, (int)ceilf(std::max({A.y, B.y, C.y})));
    float den = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
    if (fabsf(den) < 1e-9f) continue;
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float l1 = ((B.y - C.y) * (px - C.x) + (C.x - B.x) * (py - C.y)) / den;
        float l2 = ((C.y - A.y) * (px - C.x) + (A.x - C.x) * (py - C.y)) / den;
        float l3 = 1 - l1 - l2;
        if (l1 < 0 || l2 < 0 || l3 < 0) continue;
        float z = l1 * A.z + l2 * B.z + l3 * C.z;
        if (wantId >= 0) {
          V m = {l1 * t.a.x + l2 * t.b.x + l3 * t.c.x, l1 * t.a.y + l2 * t.b.y + l3 * t.c.y, l1 * t.a.z + l2 * t.b.z + l3 * t.c.z};
          if (fragPart(m) != wantId) continue;
        }
        if (z > zb[y * w + x]) {
          V n = narrow[ti] ? norm(V{l1 * na.x + l2 * nb.x + l3 * nc.x, l1 * na.y + l2 * nb.y + l3 * nc.y, l1 * na.z + l2 * nb.z + l3 * nc.z}) : fn[ti];
          zb[y * w + x] = z; shade[y * w + x] = light(n); pid[y * w + x] = t.part;
        }
      }
  }
  // Downscale: coverage, average light and how much is eye
  std::string pre = argv[2];
  FILE *oc = fopen((pre + ".ppm").c_str(), "wb"), *oa = fopen((pre + ".pgm").c_str(), "wb"), *oi = fopen((pre + ".txt").c_str(), "w");
  FILE *os = fopen((pre + ".shd").c_str(), "wb");
  fprintf(oc, "P6 %d %d 255\n", W, H); fprintf(oa, "P5 %d %d 255\n", W, H); fprintf(os, "P5 %d %d 255\n", W, H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int cov = 0, eye = 0; float sh = 0;
      for (int j = 0; j < SS; j++) for (int i = 0; i < SS; i++) {
        int k = (y * SS + j) * w + x * SS + i;
        if (pid[k] >= 0) { cov++; sh += shade[k]; if (pid[k] == 2) eye++; }
      }
      float s = cov ? sh / cov : 0;
      int r, g, b;
      if (eye * 2 > cov && cov) { r = g = b = (int)(25 + 40 * s); }
      else { r = (int)(232 * s + 30 * s * s); g = (int)(128 * s + 20 * s * s); b = (int)(82 * s); }
      fputc(std::min(255, r), oc); fputc(std::min(255, g), oc); fputc(std::min(255, b), oc);
      fputc(cov * 255 / (SS * SS), oa);
      fputc(std::min(255, (int)(s * 255)), os);
    }
  fprintf(oi, "%d %d %.4f %.4f %.4f %.4f %.4f\n", W, H, sc / SS, mnx, mny, cx, cy);
  // Center and size of the eyes in the image (left and right eye)
  for (int side = 0; side < 2; side++) {
    float sx = 0, sy = 0, x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; int k = 0;
    for (auto &t : tris) if (t.part == 2) for (V p : {t.a, t.b, t.c}) {
      if ((p.y < (rLo + rHi) / 2) != (side == 0)) continue;
      V q = view(p); float px = (q.x - mnx) * sc / SS + 1, py = (q.y - mny) * sc / SS + 1;
      sx += px; sy += py; k++; x0 = std::min(x0, px); x1 = std::max(x1, px); y0 = std::min(y0, py); y1 = std::max(y1, py);
    }
    if (k) fprintf(oi, "eye %d %.1f %.1f %.1f %.1f\n", side, sx / k, sy / k, x1 - x0, y1 - y0);
  }
  fclose(oc); fclose(oa); fclose(oi); fclose(os);
  return 0;
}
