#include "boot_splash.h"
#include "config.h"
#include "data/orbitron15.h"
#include <cmath>
#include <cstring>

static const float DEGr = (float)(M_PI / 180.0);
static const float TILT = -18.0f * DEGr;
static const float CT = cosf(TILT), ST = sinf(TILT);

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ra = (a >> 11) & 0x1F, ga = (a >> 5) & 0x3F, ba = a & 0x1F;
  int rb = (b >> 11) & 0x1F, gb = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ra + (int)((rb - ra) * t), g = ga + (int)((gb - ga) * t), bl = ba + (int)((bb - ba) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void drawSpark(M5Canvas& g, float cx, float cy, float r, uint16_t col) {
  if (r < 0.6f) return;
  float i = r * 0.34f;
  float vx[8] = { cx, cx + i, cx + r, cx + i, cx, cx - i, cx - r, cx - i };
  float vy[8] = { cy - r, cy - i, cy, cy + i, cy + r, cy + i, cy, cy - i };
  for (int k = 0; k < 8; k++) {
    int n = (k + 1) & 7;
    g.fillTriangle((int)cx, (int)cy, (int)vx[k], (int)vy[k], (int)vx[n], (int)vy[n], col);
  }
}

static void drawOrbit(M5Canvas& g, int cx, int cy, float scale, float rx, float ry, float frac, uint16_t col) {
  if (frac <= 0) return;
  const int steps = 44;
  float maxA = 2.0f * (float)M_PI * frac;
  int px = 0, py = 0; bool have = false;
  for (int k = 0; k <= steps; k++) {
    float a = maxA * k / steps;
    float ex = rx * cosf(a), ey = ry * sinf(a);
    float xr = ex * CT - ey * ST, yr = ex * ST + ey * CT;
    int X = cx + (int)lroundf(xr * scale), Y = cy + (int)lroundf(yr * scale);
    if (have) g.drawLine(px, py, X, Y, col);
    px = X; py = Y; have = true;
  }
}

static void drawPlanet(M5Canvas& g, int cx, int cy, float scale, float rx, float ry, float adeg, float appear, uint16_t col) {
  if (appear <= 0) return;
  float a = adeg * DEGr;
  float ex = rx * cosf(a), ey = ry * sinf(a);
  float xr = ex * CT - ey * ST, yr = ex * ST + ey * CT;
  int X = cx + (int)lroundf(xr * scale), Y = cy + (int)lroundf(yr * scale);
  int r = (int)lroundf(2.4f * scale * appear); if (r < 1) r = 1;
  g.fillCircle(X, Y, r, col);
}

struct OStar { float a, rad, r, t; };
static const OStar OUTER[] = {
  {14,46,3.4f,0.55f},{50,43,2.1f,0.62f},{84,47,3.0f,0.70f},{116,42,1.7f,0.78f},
  {150,46,2.6f,0.85f},{196,45,3.2f,0.58f},{232,43,1.9f,0.74f},{270,47,2.7f,0.82f},
  {305,42,1.7f,0.85f},{338,46,3.0f,0.66f},
};

static void drawLogo(M5Canvas& g, int cx, int cy, float scale, float p, uint16_t col) {
  auto sub = [&](float s, float l) { return p >= 1.0f ? 1.0f : clampf((p - s) / l, 0, 1); };
  drawOrbit(g, cx, cy, scale, 38, 25, sub(0.00f, 0.45f), col);
  drawOrbit(g, cx, cy, scale, 27, 18, sub(0.10f, 0.45f), col);
  drawOrbit(g, cx, cy, scale, 16, 11, sub(0.20f, 0.45f), col);
  drawPlanet(g, cx, cy, scale, 38, 25, 158, sub(0.70f, 0.18f), col);
  drawPlanet(g, cx, cy, scale, 27, 18,  30, sub(0.55f, 0.18f), col);
  drawPlanet(g, cx, cy, scale, 16, 11, 210, sub(0.60f, 0.18f), col);
  drawSpark(g, cx, cy, 13.0f * scale * sub(0.35f, 0.35f), col);
  for (auto& o : OUTER) {
    float ap = sub(o.t, 0.12f);
    if (ap <= 0) continue;
    float X = cx + cosf(o.a * DEGr) * o.rad * scale;
    float Y = cy + sinf(o.a * DEGr) * o.rad * scale;
    drawSpark(g, X, Y, o.r * scale * ap, col);
  }
}

static void drawTextReveal(M5Canvas& g, const char* text,
                           int x, int ymid, float u, float baseDelay, float spread,
                           float fadeDur, float drift, uint16_t target) {
  g.setTextDatum(middle_left);
  int n = (int)strlen(text);
  int acc = 0;
  for (int i = 0; i < n; i++) {
    char cb[2] = { text[i], 0 };
    int w = g.textWidth(cb);
    float d = baseDelay + (n - 1 - i) * (spread / n);
    float local = clampf((u - d) / fadeDur, 0, 1);
    if (local > 0.001f && text[i] != ' ') {
      uint16_t c = lerp565(COL_BG, target, local);
      int dx = (int)lroundf((1.0f - local) * drift);
      g.setTextColor(c);
      g.drawString(cb, x + acc - dx, ymid);
    }
    acc += w;
  }
}

void boot_splash(M5Canvas& g) {
  const uint16_t INK = COL_ACCENT;
  const int LSZ = 48;
  const float scale = LSZ / 100.0f;
  const int cxC = SCR_W / 2;
  const int cy  = SCR_H / 2;

  g.setFont(&Orbitron15);   int titleW = g.textWidth("STELLAR MAP");
  g.setFont(&fonts::TomThumb); int subW = g.textWidth("for Cardputer ADV");
  int colW = titleW > subW ? titleW : subW;
  const int GAP = 4;
  int groupLeft = (SCR_W - (LSZ + GAP + colW)) / 2; if (groupLeft < 20) groupLeft = 20;
  int logoXf = groupLeft + LSZ / 2;
  int colX   = groupLeft + LSZ + GAP;
  int titleYmid = cy - 4, subYmid = cy + 7;

  const uint32_t REVEAL = 1300;
  uint32_t t0 = millis();
  for (;;) {
    float p = (millis() - t0) / (float)REVEAL; if (p > 1) p = 1;
    g.fillScreen(COL_BG);
    drawLogo(g, cxC, cy, scale, p, INK);
    g.pushSprite(0, 0);
    if (p >= 1) break;
    delay(16);
  }
  delay(160);

  const uint32_t SLIDE = 1000;
  uint32_t s0 = millis();
  for (;;) {
    float u = (millis() - s0) / (float)SLIDE; if (u > 1) u = 1;
    float es = u * u * (3.0f - 2.0f * u);
    int cx = (int)lroundf(cxC + (logoXf - cxC) * es);
    g.fillScreen(COL_BG);
    drawLogo(g, cx, cy, scale, 1.0f, INK);
    g.setFont(&Orbitron15);
    drawTextReveal(g, "STELLAR MAP", colX, titleYmid, u,
                   0.00f, 0.55f, 0.30f, 5.0f, INK);
    g.setFont(&fonts::TomThumb);
    drawTextReveal(g, "for Cardputer ADV", colX, subYmid, u,
                   0.15f, 0.50f, 0.30f, 4.0f, lerp565(COL_BG, INK, 0.55f));
    g.pushSprite(0, 0);
    if (u >= 1) break;
    delay(16);
  }
  delay(550);
  g.setFont(&fonts::Font0);
}
