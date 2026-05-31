#include "sim.h"
#include "render.h"
#include "config.h"
#include "ephem.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

static const int CX = SCR_W / 2;
static const int CY = 58;
#define BLK rgb565(0, 0, 0)

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline void px(M5Canvas& g, int x, int y, float r, float gr, float b) {
  if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
  int R = (int)(r * 255.0f), G = (int)(gr * 255.0f), B = (int)(b * 255.0f);
  R = R < 0 ? 0 : (R > 255 ? 255 : R);
  G = G < 0 ? 0 : (G > 255 ? 255 : G);
  B = B < 0 ? 0 : (B > 255 ? 255 : B);
  g.drawPixel(x, y, rgb565((uint8_t)R, (uint8_t)G, (uint8_t)B));
}
static inline void add_px(M5Canvas& g, int x, int y, float r, float gr, float b) {
  if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
  uint16_t c = g.readPixel(x, y);
  int cr = ((c >> 11) & 0x1F) << 3, cg = ((c >> 5) & 0x3F) << 2, cb = (c & 0x1F) << 3;
  int nr = cr + (int)(r * 255), ng = cg + (int)(gr * 255), nb = cb + (int)(b * 255);
  if (nr > 255) nr = 255; if (ng > 255) ng = 255; if (nb > 255) nb = 255;
  g.drawPixel(x, y, rgb565(nr, ng, nb));
}
static inline void blend_px(M5Canvas& g, int x, int y, float r, float gr, float b, float a) {
  if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
  uint16_t c = g.readPixel(x, y);
  int cr = ((c >> 11) & 0x1F) << 3, cg = ((c >> 5) & 0x3F) << 2, cb = (c & 0x1F) << 3;
  int nr = (int)(cr * (1 - a) + r * 255 * a);
  int ng = (int)(cg * (1 - a) + gr * 255 * a);
  int nb = (int)(cb * (1 - a) + b * 255 * a);
  if (nr > 255) nr = 255; if (ng > 255) ng = 255; if (nb > 255) nb = 255;
  g.drawPixel(x, y, rgb565(nr < 0 ? 0 : nr, ng < 0 ? 0 : ng, nb < 0 ? 0 : nb));
}
static inline uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  h ^= h >> 13; h *= 0x85ebca6bu; h ^= h >> 16; return h;
}
static inline float frand(int x, int y) { return (hash2(x, y) & 0xffff) / 65535.0f; }

static inline float fade(float t) { return t * t * (3.0f - 2.0f * t); }
static float vnoise(float x, float y) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float xf = x - xi, yf = y - yi;
  float a = frand(xi, yi),     b = frand(xi + 1, yi);
  float c = frand(xi, yi + 1), d = frand(xi + 1, yi + 1);
  float u = fade(xf), v = fade(yf);
  return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}
static float fbm(float x, float y, int oct) {
  float s = 0, a = 0.5f, f = 1.0f, norm = 0;
  for (int i = 0; i < oct; i++) { s += a * vnoise(x * f, y * f); norm += a; a *= 0.5f; f *= 2.0f; }
  return s / norm;
}

static void spectral_rgb(const char* sp, float& r, float& g, float& b) {
  char cls = 0;
  if (sp) for (const char* p = sp; *p; ++p) if (strchr("OBAFGKMWCSDLT", *p)) { cls = *p; break; }
  switch (cls) {
    case 'O': case 'W': r = 0.62f; g = 0.71f; b = 1.00f; break;
    case 'B':           r = 0.72f; g = 0.80f; b = 1.00f; break;
    case 'A': case 'D': r = 0.85f; g = 0.89f; b = 1.00f; break;
    case 'F':           r = 0.99f; g = 0.97f; b = 0.94f; break;
    case 'G':           r = 1.00f; g = 0.88f; b = 0.62f; break;
    case 'K':           r = 1.00f; g = 0.74f; b = 0.46f; break;
    case 'M':           r = 1.00f; g = 0.52f; b = 0.34f; break;
    case 'C': case 'S': r = 1.00f; g = 0.40f; b = 0.28f; break;
    default:            r = 1.00f; g = 0.80f; b = 0.52f; break;
  }
}
static void ramp_jup(float v, float& r, float& g, float& b) {
  static const float S[6][4] = {
    { 0.00f, 0.30f, 0.16f, 0.10f }, { 0.25f, 0.55f, 0.30f, 0.18f },
    { 0.45f, 0.80f, 0.50f, 0.30f }, { 0.62f, 0.88f, 0.72f, 0.50f },
    { 0.80f, 0.93f, 0.86f, 0.72f }, { 1.01f, 0.99f, 0.95f, 0.88f } };
  v = clampf(v, 0, 1);
  for (int i = 0; i < 5; i++)
    if (v <= S[i + 1][0]) {
      float t = (v - S[i][0]) / (S[i + 1][0] - S[i][0]);
      r = S[i][1] + (S[i + 1][1] - S[i][1]) * t;
      g = S[i][2] + (S[i + 1][2] - S[i][2]) * t;
      b = S[i][3] + (S[i + 1][3] - S[i][3]) * t;
      return;
    }
  r = S[5][1]; g = S[5][2]; b = S[5][3];
}

static void caption(M5Canvas& g, const char* name, const char* info) {
  g.setTextDatum(bottom_center);
  g.setTextColor(rgb565(225, 225, 235));
  g.drawString(name, CX, SCR_H - 13);
  if (info && info[0]) {
    g.setTextColor(rgb565(140, 150, 165));
    g.drawString(info, CX, SCR_H - 2);
  }
}
static void hint(M5Canvas& g) {
  g.setTextDatum(top_left);
  g.setTextColor(rgb565(90, 100, 112));
  g.drawString("s/del: exit", 3, 3);
}

static M5Canvas g_cache(&M5Cardputer.Display);
static bool g_cacheReady = false, g_cacheTried = false;
static int  g_ck = -2, g_ci = -2;
static bool ensure_cache() {
  if (!g_cacheTried) {
    g_cacheTried = true;
    g_cache.setColorDepth(16);
    g_cacheReady = g_cache.createSprite(SCR_W, SCR_H);
  }
  return g_cacheReady;
}

static void bake_photosphere(M5Canvas& d, bool isSun, const char* spect) {
  const int R = 46;
  float br, bg, bb; spectral_rgb(spect, br, bg, bb);
  if (isSun) { br = 1.00f; bg = 0.74f; bb = 0.34f; }
  char cls = 0;
  if (spect) for (const char* p = spect; *p; ++p) if (strchr("OBAFGKM", *p)) { cls = *p; break; }
  int ns = (cls == 'M') ? 5 : (cls == 'K') ? 4 : (cls == 'G' || isSun) ? 4 : 1;
  float spx[6], spy[6], spr2[6];
  for (int k = 0; k < ns; k++) {
    float a = frand(k + 11, 3) * 6.2832f, rad = frand(k + 5, 7) * R * 0.6f;
    float rr = R * (0.07f + 0.06f * frand(k + 2, 9));
    spx[k] = CX + cosf(a) * rad; spy[k] = CY + sinf(a) * rad * 0.92f; spr2[k] = rr * rr;
  }
  for (int y = CY - R; y <= CY + R; y++)
    for (int x = CX - R; x <= CX + R; x++) {
      float dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
      if (d2 > (float)(R * R)) continue;
      float mu = sqrtf(1.0f - d2 / (float)(R * R));
      float lon = dx / R, lat = dy / R;
      float gran = fbm(lon * 7.0f + 2.0f, lat * 7.0f + 5.0f, 4);
      float net  = fbm(lon * 15.0f, lat * 15.0f, 3);
      float mott = 0.78f + 0.36f * gran + 0.10f * (net - 0.5f);
      float bright = mott * (0.45f + 0.55f * mu);
      float warm = clampf(gran, 0, 1);
      float r = br * (0.88f + 0.22f * warm);
      float g = bg * (0.82f + 0.30f * warm);
      float b = bb * (0.74f + 0.40f * warm);
      r *= bright; g *= bright; b *= bright;
      for (int k = 0; k < ns; k++) {
        float sd = (x - spx[k]) * (x - spx[k]) + (y - spy[k]) * (y - spy[k]);
        if (sd < spr2[k]) { float f = (sd < spr2[k] * 0.45f) ? 0.30f : 0.62f; r *= f; g *= f; b *= f; break; }
      }
      px(d, x, y, r, g, b);
    }
}
static void overlay_corona(M5Canvas& g, bool isSun, const char* spect, float t) {
  const int R = 46, M = 30;
  float br, bg, bb; spectral_rgb(spect, br, bg, bb);
  if (isSun) { br = 1.0f; bg = 0.74f; bb = 0.34f; }
  float cr = br * 0.4f + 1.00f * 0.6f, cg = bg * 0.4f + 0.50f * 0.6f, cb = bb * 0.4f + 0.20f * 0.6f;
  float breathe = 0.85f + 0.15f * sinf(t * 1.3f);
  for (int y = CY - R - M; y <= CY + R + M; y++)
    for (int x = CX - R - M; x <= CX + R + M; x++) {
      float dx = x - CX, dy = y - CY, r2 = dx * dx + dy * dy, rr = sqrtf(r2);
      if (rr < R - 1 || rr > R + M) continue;
      float ang = atan2f(dy, dx);
      float flick = 0.55f + 0.45f * vnoise(ang * 3.0f + t * 0.9f, rr * 0.12f);
      float fall = expf(-(rr - R) / 12.0f);
      float inten = fall * flick * breathe * 0.95f;
      add_px(g, x, y, cr * inten, cg * inten, cb * inten);
    }
  int flares = isSun ? 4 : 2;
  for (int k = 0; k < flares; k++) {
    float a = k * 1.7f + 0.4f + 0.15f * sinf(t * 0.7f + k);
    float len = 12.0f + 8.0f * vnoise(k * 3.1f, t * 1.6f);
    for (int s = 0; s < (int)len; s++) {
      float rr = R + s;
      float wob = sinf(s * 0.35f + t * 3.0f + k) * 2.5f;
      int x = CX + (int)(cosf(a) * rr - sinf(a) * wob);
      int y = CY + (int)(sinf(a) * rr + cosf(a) * wob);
      float f = (1.0f - s / len) * 0.9f;
      add_px(g, x, y, 1.0f * f, 0.35f * f, 0.12f * f);
    }
  }
}
static void render_starlike(M5Canvas& g, int kind, int idx, bool isSun,
                            const char* spect, const char* name, float mag, float distLY, float t) {
  bool cached = ensure_cache();
  bool bakeNow = !cached || g_ck != kind || g_ci != idx;
  if (bakeNow) {
    if (cached) g_cache.fillScreen(BLK);
    bake_photosphere(cached ? g_cache : g, isSun, spect);
    if (cached) { g_ck = kind; g_ci = idx; }
  }
  if (cached) g_cache.pushSprite(&g, 0, 0);
  overlay_corona(g, isSun, spect, t);
  char info[44];
  if (isSun) snprintf(info, sizeof(info), "G2V star   mag %.1f", mag);
  else if (distLY >= 0) snprintf(info, sizeof(info), "%s   m%.1f   %.0f LY", spect && spect[0] ? spect : "star", mag, distLY);
  else snprintf(info, sizeof(info), "%s   m%.1f", spect && spect[0] ? spect : "star", mag);
  caption(g, name, info);
}

static const float JPA = 54, JPB = 49;
static void bake_jupiter(M5Canvas& d) {
  for (int y = CY - 52; y <= CY + 52; y++)
    for (int x = CX - 58; x <= CX + 58; x++) {
      float dx = x - CX, dy = y - CY;
      float ne = (dx * dx) / (JPA * JPA) + (dy * dy) / (JPB * JPB);
      if (ne > 1.0f) continue;
      float mu = sqrtf(1.0f - ne);
      float lon = dx / JPA, lat = dy / JPB;
      float wx = fbm(lon * 3.0f + 5.0f, lat * 6.0f + 1.0f, 4);
      float wy = fbm(lon * 3.0f + 9.0f, lat * 6.0f + 7.0f, 4);
      float wlat = lat + (wy - 0.5f) * 0.22f;
      float bands = 0.5f + 0.5f * sinf(wlat * (float)M_PI * 5.5f);
      float turb = fbm(lon * 5.0f + (wx - 0.5f) * 2.0f, lat * 9.0f + 3.0f, 4);
      float v = clampf(bands * 0.55f + (turb - 0.5f) * 0.8f + 0.25f, 0, 1);
      float r, g, b; ramp_jup(v, r, g, b);
      float s = 0.45f + 0.55f * mu;
      px(d, x, y, r * s, g * s, b * s);
    }

  float gx = CX - JPA * 0.20f, gy = CY + JPB * 0.30f, rx = 16, ry = 10;
  for (int y = (int)(gy - ry); y <= (int)(gy + ry); y++)
    for (int x = (int)(gx - rx); x <= (int)(gx + rx); x++) {
      float ex = (x - gx) / rx, ey = (y - gy) / ry, e = ex * ex + ey * ey;
      if (e > 1.0f) continue;
      float sw = fbm(ex * 3.0f + 20.0f, ey * 3.0f + 30.0f, 3);
      float r = 0.80f + 0.10f * (sw - 0.5f), gg = 0.34f + 0.12f * sw, b = 0.24f;
      float rim = (e > 0.7f) ? 0.7f : 1.0f;
      px(d, x, y, r * rim, gg * rim, b * rim);
    }
}
static void overlay_jupiter(M5Canvas& g, double d, float t) {
  float breathe = 0.80f + 0.20f * sinf(t * 1.1f);
  for (int y = CY - 56; y <= CY + 56; y++)
    for (int x = CX - 62; x <= CX + 62; x++) {
      float dx = x - CX, dy = y - CY;
      float ne = (dx * dx) / (JPA * JPA) + (dy * dy) / (JPB * JPB);
      if (ne <= 1.0f) {
        float lon = dx / JPA, lat = dy / JPB;
        float a = 0.0f;
        for (int k = 0; k < 3; k++) {
          float bl = -0.45f + 0.45f * k;
          float w = expf(-((lat - bl) * (lat - bl)) / 0.020f);
          float streak = 0.5f + 0.5f * sinf(lon * 6.0f + t * (7.0f + 3.0f * k) + lat * 4.0f);
          a += w * streak;
        }
        a = clampf(a * 0.14f, 0, 0.30f);
        if (a > 0.01f) blend_px(g, x, y, 0.18f, 0.10f, 0.06f, a);
      } else {
        float rr = sqrtf(dx * dx + dy * dy);
        float refr = JPA;
        if (rr > refr && rr < refr + 10) {
          float fall = expf(-(rr - refr) / 5.0f) * breathe * 0.5f;
          add_px(g, x, y, 0.55f * fall, 0.32f * fall, 0.16f * fall);
        }
      }
    }
  const float P[4] = { 1.769f, 3.551f, 7.155f, 16.689f };
  const float A[4] = { 66, 86, 110, 140 };
  for (int i = 0; i < 4; i++) {
    float ph = 6.2832f * (float)(fmod(d, (double)P[i] * 1000.0) / P[i]);
    int mx = CX + (int)(A[i] * sinf(ph));
    bool front = cosf(ph) > 0, over = fabsf((float)(mx - CX)) < JPA;
    if (over && !front) continue;
    g.fillCircle(mx, CY, 1, rgb565(235, 235, 220));
  }
}
static void render_jupiter(M5Canvas& g, double d, const char* name, float mag, float t) {
  bool cached = ensure_cache();
  bool bakeNow = !cached || g_ck != SEL_BODY || g_ci != 5;
  if (bakeNow) { if (cached) g_cache.fillScreen(BLK); bake_jupiter(cached ? g_cache : g); if (cached) { g_ck = SEL_BODY; g_ci = 5; } }
  if (cached) g_cache.pushSprite(&g, 0, 0);
  overlay_jupiter(g, d, t);
  char info[32]; snprintf(info, sizeof(info), "+ moons  mag %.1f", mag);
  caption(g, name, info);
}

static const float SPA = 44, SPB = 39;
static void bake_saturn(M5Canvas& d) {
  const float Rin = 58, gap0 = 84, gap1 = 90, Rout = 104, ry = 0.32f;
  for (int y = CY - 40; y <= CY + 40; y++)
    for (int x = CX - 110; x <= CX + 110; x++) {
      float dx = x - CX, dy = y - CY;
      float q = sqrtf(dx * dx + (dy / ry) * (dy / ry));
      bool ring = (q >= Rin && q <= Rout && !(q > gap0 && q < gap1));
      bool front = dy > 0;
      bool planet = (dx * dx) / (SPA * SPA) + (dy * dy) / (SPB * SPB) <= 1.0f;
      if (ring && front) {
        float band = (q < gap0) ? 0.92f : 0.80f, tt = 0.85f + 0.10f * sinf(q * 0.4f);
        px(d, x, y, 0.86f * band * tt, 0.79f * band * tt, 0.62f * band * tt);
      } else if (planet) {
        float mu = sqrtf(clampf(1.0f - ((dx * dx) / (SPA * SPA) + (dy * dy) / (SPB * SPB)), 0, 1));
        float s = 0.45f + 0.55f * mu, lat = dy / SPB;
        float turb = fbm((dx / SPA) * 4.0f + 11.0f, lat * 10.0f, 3);
        float band = 1.0f + 0.08f * sinf(lat * 9.0f) + 0.05f * (turb - 0.5f);
        px(d, x, y, 0.92f * s * band, 0.84f * s * band, 0.60f * s * band);
      } else if (ring && !front) {
        float sh = (dx > -SPA && dx < SPA * 0.3f) ? 0.40f : 1.0f;
        float band = (q < gap0) ? 0.88f : 0.76f;
        px(d, x, y, 0.84f * band * sh, 0.77f * band * sh, 0.60f * band * sh);
      }
    }
}
static void overlay_saturn(M5Canvas& g, float t) {
  float breathe = 0.80f + 0.20f * sinf(t * 1.0f);
  for (int y = CY - 44; y <= CY + 44; y++)
    for (int x = CX - 50; x <= CX + 50; x++) {
      float dx = x - CX, dy = y - CY;
      float ne = (dx * dx) / (SPA * SPA) + (dy * dy) / (SPB * SPB);
      if (ne <= 1.0f) continue;
      float rr = sqrtf(dx * dx + dy * dy);
      if (rr > SPA && rr < SPA + 8) {
        float fall = expf(-(rr - SPA) / 4.0f) * breathe * 0.4f;
        add_px(g, x, y, 0.50f * fall, 0.45f * fall, 0.30f * fall);
      }
    }
}
static void render_saturn(M5Canvas& g, const char* name, float mag, float t) {
  bool cached = ensure_cache();
  bool bakeNow = !cached || g_ck != SEL_BODY || g_ci != 6;
  if (bakeNow) { if (cached) g_cache.fillScreen(BLK); bake_saturn(cached ? g_cache : g); if (cached) { g_ck = SEL_BODY; g_ci = 6; } }
  if (cached) g_cache.pushSprite(&g, 0, 0);
  overlay_saturn(g, t);
  char info[32]; snprintf(info, sizeof(info), "rings  mag %.1f", mag);
  caption(g, name, info);
}

static void bake_venus(M5Canvas& d) {
  const int R = 42;
  for (int y = CY - R; y <= CY + R; y++)
    for (int x = CX - R; x <= CX + R; x++) {
      float dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
      if (d2 > (float)(R * R)) continue;
      float mu = sqrtf(1.0f - d2 / (float)(R * R));
      float lon = dx / R, lat = dy / R;
      float cl = fbm(lon * 3.5f + 4.0f, lat * 4.5f + 2.0f, 4);
      float v = 0.90f + 0.10f * (cl - 0.5f);
      float s = 0.5f + 0.5f * mu;
      px(d, x, y, 0.97f * v * s, 0.93f * v * s, 0.78f * v * s);
    }
}
static void overlay_venus(M5Canvas& g, float t) {
  const int R = 42;
  for (int y = CY - R; y <= CY + R; y++)
    for (int x = CX - R; x <= CX + R; x++) {
      float dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
      if (d2 > (float)(R * R)) continue;
      float lat = dy / (float)R;
      float drift = 0.5f + 0.5f * sinf(lat * 6.0f + (x - CX) * 0.05f + t * 1.2f);
      float a = drift * 0.10f;
      blend_px(g, x, y, 0.85f, 0.80f, 0.62f, a);
    }
  float breathe = 0.80f + 0.20f * sinf(t * 0.9f);
  for (int y = CY - R - 8; y <= CY + R + 8; y++)
    for (int x = CX - R - 8; x <= CX + R + 8; x++) {
      float dx = x - CX, dy = y - CY, rr = sqrtf(dx * dx + dy * dy);
      if (rr > R && rr < R + 8) {
        float fall = expf(-(rr - R) / 4.0f) * breathe * 0.45f;
        add_px(g, x, y, 0.60f * fall, 0.56f * fall, 0.42f * fall);
      }
    }
}
static void render_venus(M5Canvas& g, const char* name, float mag, float t) {
  bool cached = ensure_cache();
  bool bakeNow = !cached || g_ck != SEL_BODY || g_ci != 3;
  if (bakeNow) { if (cached) g_cache.fillScreen(BLK); bake_venus(cached ? g_cache : g); if (cached) { g_ck = SEL_BODY; g_ci = 3; } }
  if (cached) g_cache.pushSprite(&g, 0, 0);
  overlay_venus(g, t);
  char info[24]; snprintf(info, sizeof(info), "mag %.1f", mag);
  caption(g, name, info);
}

static void sim_moon(M5Canvas& g, float phase, bool waxing, const char* name, float mag) {
  const int R = 55;
  const float mar[7][3] = {
    { -0.28f, -0.30f, 0.27f }, { 0.06f, -0.26f, 0.18f }, { 0.32f, -0.08f, 0.20f },
    { 0.55f, -0.16f, 0.12f }, { -0.20f, 0.30f, 0.20f }, { -0.55f, 0.04f, 0.22f },
    { 0.40f, 0.22f, 0.15f } };
  const int NC = 26;
  for (int y = CY - R; y <= CY + R; y++)
    for (int x = CX - R; x <= CX + R; x++) {
      float dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
      if (d2 > (float)(R * R)) continue;
      float mu = sqrtf(1.0f - d2 / (float)(R * R));
      float col = (0.55f + 0.45f * mu) * 0.82f;
      for (int m = 0; m < 7; m++) {
        float mx = CX + mar[m][0] * R, my = CY + mar[m][1] * R, mr = mar[m][2] * R;
        float md = (x - mx) * (x - mx) + (y - my) * (y - my);
        if (md < mr * mr) { col *= 0.58f + 0.10f * (md / (mr * mr)); break; }
      }
      for (int c = 0; c < NC; c++) {
        float ca = frand(c + 3, 17) * 6.2832f, cd = sqrtf(frand(c + 9, 5)) * R * 0.9f;
        float ccx = CX + cosf(ca) * cd, ccy = CY + sinf(ca) * cd * 0.98f;
        float cr = 2.0f + frand(c, 23) * 5.0f;
        float dd = (x - ccx) * (x - ccx) + (y - ccy) * (y - ccy);
        if (dd < cr * cr) { col *= (dd > cr * cr * 0.55f) ? 1.22f : 0.78f; break; }
      }
      float w = sqrtf((float)(R * R) - dy * dy);
      float xt = (1.0f - 2.0f * phase) * w;
      float rel = waxing ? (dx - xt) : (xt - dx);
      float lit = clampf(0.5f + rel / 5.0f, 0.0f, 1.0f);
      col *= 0.06f + 0.94f * lit;
      px(g, x, y, col, col, col * 1.02f);
    }
  char info[40];
  snprintf(info, sizeof(info), "Phase %d%%  %s", (int)lroundf(phase * 100), waxing ? "waxing" : "waning");
  caption(g, name, info);
}
static void sim_planet_rocky(M5Canvas& g, int idx, const char* name, float mag) {
  const int R = 42;
  float br, bg, bb;
  if (idx == 2) { br = 0.62f; bg = 0.60f; bb = 0.57f; }
  else          { br = 0.82f; bg = 0.42f; bb = 0.28f; }
  for (int y = CY - R; y <= CY + R; y++)
    for (int x = CX - R; x <= CX + R; x++) {
      float dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
      if (d2 > (float)(R * R)) continue;
      float mu = sqrtf(1.0f - d2 / (float)(R * R));
      float s = 0.42f + 0.58f * mu, r = br, gg = bg, b = bb;
      if (idx == 2) {
        for (int c = 0; c < 18; c++) {
          float ca = frand(c + 4, 13) * 6.2832f, cd = sqrtf(frand(c + 7, 9)) * R * 0.88f;
          float ccx = CX + cosf(ca) * cd, ccy = CY + sinf(ca) * cd;
          float cr = 2.0f + frand(c, 19) * 4.0f, dd = (x - ccx) * (x - ccx) + (y - ccy) * (y - ccy);
          if (dd < cr * cr) { s *= (dd > cr * cr * 0.5f) ? 1.2f : 0.8f; break; }
        }
      } else {
        float cap = (x - CX) * (x - CX) + (y - (CY - R * 0.72f)) * (y - (CY - R * 0.72f));
        if (cap < (R * 0.30f) * (R * 0.30f)) { r = 0.95f; gg = 0.96f; b = 0.98f; }
        else {
          float aa = fbm((dx / R) * 4.0f + 6.0f, (dy / R) * 4.0f + 9.0f, 3);
          if (aa < 0.40f) { r *= 0.62f; gg *= 0.62f; b *= 0.66f; }
        }
      }
      px(g, x, y, r * s, gg * s, b * s);
    }
  char info[24]; snprintf(info, sizeof(info), "mag %.1f", mag);
  caption(g, name, info);
}
static int dstype(const char* t) {
  if (!t) return 5;
  if (strstr(t, "Globular"))  return 2;
  if (strstr(t, "Open"))      return 1;
  if (strstr(t, "Planetary")) return 3;
  if (strstr(t, "Supernova")) return 4;
  if (strstr(t, "Galaxy"))    return 0;
  if (strstr(t, "Star"))      return 6;
  return 5;
}
static void sim_deepsky(M5Canvas& g, const char* type, const char* id, const char* name, float mag) {
  int k = dstype(type);
  if (k == 0) {
    const float rx = 74, ry = 26;
    for (int y = CY - 30; y <= CY + 30; y++)
      for (int x = CX - 80; x <= CX + 80; x++) {
        float nx = (x - CX) / rx, ny = (y - CY) / ry, rr = sqrtf(nx * nx + ny * ny);
        if (rr > 1.5f) continue;
        float I = expf(-rr * 2.3f) * 1.25f, t = clampf(rr, 0, 1);
        px(g, x, y, (0.95f * (1 - t) + 0.55f * t) * I, (0.90f * (1 - t) + 0.66f * t) * I, (0.78f * (1 - t) + 1.00f * t) * I);
      }
    for (int a = 0; a < 130; a++) {
      float th = a * 0.16f, rad = 6 + a * 0.52f;
      for (int s = -1; s <= 1; s += 2) {
        int x = CX + (int)(cosf(th) * rad * (s > 0 ? 1 : -1));
        int y = CY + (int)(sinf(th) * rad * 0.34f * (s > 0 ? 1 : -1));
        px(g, x, y, 0.55f, 0.65f, 0.95f);
      }
    }
    g.fillCircle(CX, CY, 3, rgb565(255, 248, 230));
  } else if (k == 1) {
    for (int i = 0; i < 70; i++) {
      float a = frand(i + 2, 7) * 6.2832f, rad = sqrtf(frand(i + 5, 3)) * 56.0f;
      int x = CX + (int)(cosf(a) * rad), y = CY + (int)(sinf(a) * rad);
      float br = 0.6f + 0.4f * frand(i, 11);
      if (frand(i, 31) > 0.85f) g.fillCircle(x, y, 1, rgb565((int)(210 * br), (int)(220 * br), 255));
      else px(g, x, y, 0.85f * br, 0.90f * br, 1.0f * br);
    }
  } else if (k == 2) {
    g.fillCircle(CX, CY, 4, rgb565(255, 250, 235));
    for (int i = 0; i < 260; i++) {
      float a = frand(i + 1, 9) * 6.2832f, rad = (1.0f - powf(frand(i + 6, 4), 1.8f)) * 52.0f;
      int x = CX + (int)(cosf(a) * rad), y = CY + (int)(sinf(a) * rad);
      float br = clampf(1.0f - rad / 60.0f, 0.25f, 1.0f);
      px(g, x, y, br, br * 0.97f, br * 0.85f);
    }
  } else if (k == 3) {
    const float rx = 40, ry = 34;
    for (int y = CY - 36; y <= CY + 36; y++)
      for (int x = CX - 42; x <= CX + 42; x++) {
        float nx = (x - CX) / rx, ny = (y - CY) / ry, rr = sqrtf(nx * nx + ny * ny);
        if (rr > 1.05f) continue;
        float shell = expf(-(rr - 0.75f) * (rr - 0.75f) / 0.06f);
        px(g, x, y, 0.30f * shell, 0.90f * shell, 0.78f * shell);
      }
    g.fillCircle(CX, CY, 1, rgb565(220, 235, 255));
  } else if (k == 4) {
    for (int a = 0; a < 200; a++) {
      float th = frand(a + 1, 5) * 6.2832f, rad = 30 + frand(a + 4, 9) * 28.0f;
      int x = CX + (int)(cosf(th) * rad), y = CY + (int)(sinf(th) * rad * 0.9f);
      float br = 0.4f + 0.5f * frand(a, 13);
      px(g, x, y, 0.85f * br, 0.35f * br, 0.40f * br);
    }
  } else if (k == 6) {
    g.fillCircle(CX - 10, CY, 2, rgb565(255, 255, 245));
    g.fillCircle(CX + 12, CY - 4, 2, rgb565(230, 235, 255));
  } else {
    for (int y = CY - 40; y <= CY + 40; y++)
      for (int x = CX - 70; x <= CX + 70; x++) {
        float dx = (x - CX) / 70.0f, dy = (y - CY) / 40.0f, rr = sqrtf(dx * dx + dy * dy);
        if (rr > 1.05f) continue;
        float I = expf(-rr * 1.8f) * (0.55f + 0.7f * frand(x / 3, y / 3));
        px(g, x, y, 1.0f * I, 0.50f * I, 0.62f * I);
      }
    for (int i = 0; i < 18; i++) {
      int x = CX + (int)((frand(i + 2, 21) - 0.5f) * 130), y = CY + (int)((frand(i + 7, 14) - 0.5f) * 70);
      px(g, x, y, 1.0f, 1.0f, 0.95f);
    }
  }
  char info[40];
  if (mag < 90.0f) snprintf(info, sizeof(info), "%s  mag %.1f", type ? type : "", mag);
  else snprintf(info, sizeof(info), "%s", type ? type : "");
  caption(g, name && name[0] ? name : id, info);
}

void sim_draw(M5Canvas& g, int kind, int idx, double d) {
  float t = millis() * 0.001f;
  g.fillScreen(BLK);
  if (kind == SEL_STAR) {
    render_starlike(g, kind, idx, false, star_spect_at(idx), star_label_at(idx), star_mag_at(idx), star_dist_at(idx), t);
  } else if (kind == SEL_MESSIER) {
    sim_deepsky(g, messier_type_at(idx), messier_id_at(idx), messier_name_at(idx), messier_mag_at(idx));
  } else if (kind == SEL_BODY) {
    SkyBody b = ephem_body(idx, d);
    const char* nm = ephem_name(idx);
    if (idx == 0)      render_starlike(g, kind, 0, true, "G2V", nm, b.mag, -1, t);
    else if (idx == 1) sim_moon(g, b.phase, b.waxing, nm, b.mag);
    else if (idx == 3) render_venus(g, nm, b.mag, t);
    else if (idx == 5) render_jupiter(g, d, nm, b.mag, t);
    else if (idx == 6) render_saturn(g, nm, b.mag, t);
    else               sim_planet_rocky(g, idx, nm, b.mag);
  }
  hint(g);
}
