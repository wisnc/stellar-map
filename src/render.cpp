#include "render.h"
#include "config.h"
#include "gnss.h"
#include "data/star_catalog.h"
#include "data/star_names.h"
#include "data/constellations.h"
#include "data/messier.h"
#include <cmath>
#include <cstdio>

static const float DEG = (float)(M_PI / 180.0);
static const float RAD = (float)(180.0 / M_PI);
static const float DEPTH_MIN = 0.08716f;
static const int   MOON_R = 7;

static CenterObj g_center  = { -1, 0, 0, false };
static CenterObj g_centerM = { -1, 0, 0, false };
const CenterObj& center_star()    { return g_center; }
const CenterObj& center_messier() { return g_centerM; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Vec3 star_vec_at(int i)    { return { star_xyz[i][0], star_xyz[i][1], star_xyz[i][2] }; }
Vec3 messier_vec_at(int i) { return { messier_xyz[i][0], messier_xyz[i][1], messier_xyz[i][2] }; }
Vec3 body_vec_at(int idx, double d) { SkyBody b = ephem_body(idx, d); return vec_from_radec(b.ra, b.dec); }
const char* star_spect_at(int i)   { return star_spect[i]; }
const char* star_label_at(int i)   { return (star_label[i] && star_label[i][0]) ? star_label[i] : "Star"; }
float       star_mag_at(int i)     { return star_mag[i]; }
float       star_dist_at(int i)    { return star_dist_ly[i]; }
const char* messier_id_at(int i)   { return messier_id[i]; }
const char* messier_name_at(int i) { return messier_name[i]; }
const char* messier_type_at(int i) { return messier_type[i]; }
float       messier_mag_at(int i)  { return messier_mag[i]; }

void camera_init(Camera& c) {
  c.az = c.azT = DEF_AZ * DEG;
  c.alt = c.altT = DEF_ALT * DEG;
  c.fov = c.fovT = DEF_FOV * DEG;
  c.ppx = c.ppxT = 0.0f;
  c.panelX = c.panelXT = -(float)PANEL_W;
}

float compute_fov_max(float ppx) {
  float maxX = fmaxf(CENTER_X + ppx, SCR_W - (CENTER_X + ppx));
  float corner = sqrtf(maxX * maxX + (SCR_H * 0.5f) * (SCR_H * 0.5f));
  float fmax = 2.0f * atanf((SCR_H * 0.5f) * tanf(85.0f * DEG) / corner);
  return fminf(fmax, FOV_MAX * DEG);
}

float pan_fraction(const Camera& c) {
  float z = (c.fovT - FOV_MIN * DEG) / ((DEF_FOV - FOV_MIN) * DEG);
  z = clampf(z, 0.0f, 1.0f);
  return PAN_FRAC_MIN + (PAN_FRACTION - PAN_FRAC_MIN) * z;
}

void camera_pan(Camera& c, float daz_frac, float dalt_frac) {
  c.azT += daz_frac * c.fov;
  while (c.azT < 0) c.azT += 2 * (float)M_PI;
  while (c.azT >= 2 * (float)M_PI) c.azT -= 2 * (float)M_PI;
  c.altT = clampf(c.altT + dalt_frac * c.fov, -88.0f * DEG, 88.0f * DEG);
}

void camera_zoom(Camera& c, float factor) {
  c.fovT = clampf(c.fovT * factor, FOV_MIN * DEG, compute_fov_max(c.ppxT));
}

void camera_ease(Camera& c, float dt_ms) {
  float f  = 1.0f - expf(-dt_ms / EASE_TAU_MS);
  float fp = 1.0f - expf(-dt_ms / PANEL_TAU_MS);
  float daz = c.azT - c.az;
  while (daz >  (float)M_PI) daz -= 2 * (float)M_PI;
  while (daz < -(float)M_PI) daz += 2 * (float)M_PI;
  c.az += daz * f;
  while (c.az < 0) c.az += 2 * (float)M_PI;
  while (c.az >= 2 * (float)M_PI) c.az -= 2 * (float)M_PI;
  c.alt += (c.altT - c.alt) * f;
  c.fov += (c.fovT - c.fov) * f;
  c.ppx += (c.ppxT - c.ppx) * fp;
  c.panelX += (c.panelXT - c.panelX) * fp;
}

void projector_build(Projector& p, const Camera& c, const Triad& t) {
  float ca = cosf(c.alt), sa = sinf(c.alt), cA = cosf(c.az), sA = sinf(c.az);
  Vec3 look = v_add(v_add(v_scale(t.north, ca * cA), v_scale(t.east, ca * sA)),
                    v_scale(t.zenith, sa));
  p.fwd = v_norm(look);
  Vec3 right = v_cross(p.fwd, t.zenith);
  if (v_dot(right, right) < 1e-6f) right = v_cross(p.fwd, t.north);
  p.right = v_norm(right);
  p.up = v_norm(v_cross(p.right, p.fwd));
  p.focal = (SCR_H * 0.5f) / tanf(c.fov * 0.5f);
  p.ppx_x = CENTER_X + c.ppx;
}

bool project(const Projector& p, const Vec3& s, float& sx, float& sy) {
  float depth = v_dot(s, p.fwd);
  if (depth <= DEPTH_MIN) { sx = sy = 0; return false; }
  float xc = v_dot(s, p.right) / depth;
  float yc = v_dot(s, p.up) / depth;
  sx = p.ppx_x + p.focal * xc;
  sy = CENTER_Y - p.focal * yc;
  return true;
}

bool on_screen(float sx, float sy, float m) {
  return sx >= -m && sx <= SCR_W + m && sy >= -m && sy <= SCR_H + m;
}
static inline float brightness(float mag) {
  float b = (MAG_LIMIT - mag) / (MAG_LIMIT + 1.5f);
  b = clampf(b, 0.0f, 1.0f);
  b = sqrtf(b);
  return 0.30f + 0.70f * b;
}

static uint16_t bv_color(int16_t ci, float b) {
  float bv = (ci == 0x7FFF) ? 0.6f : (ci / 1000.0f);
  bv = clampf(bv, -0.4f, 2.0f);
  float rr = 1.0f, gg = 1.0f - 0.10f * bv, bl = 1.0f - 0.32f * bv;
  if (bv < 0) { rr = 1.0f + 0.30f * bv; bl = 1.0f - 0.45f * bv; }
  rr = clampf(rr, 0, 1); gg = clampf(gg, 0, 1); bl = clampf(bl, 0, 1);
  return rgb565((uint8_t)(255 * b * rr), (uint8_t)(255 * b * gg), (uint8_t)(255 * b * bl));
}
static uint16_t class_color(const char* spect, int16_t ci, float b) {
  char cls = 0;
  if (spect)
    for (const char* p = spect; *p; ++p) {
      char c = *p;
      if (c=='O'||c=='B'||c=='A'||c=='F'||c=='G'||c=='K'||c=='M'||
          c=='W'||c=='C'||c=='S'||c=='D'||c=='L'||c=='T') { cls = c; break; }
    }
  float r, g, bl;
  switch (cls) {
    case 'O': case 'W': r = 0.61f; g = 0.70f; bl = 1.00f; break;
    case 'B':           r = 0.70f; g = 0.78f; bl = 1.00f; break;
    case 'A': case 'D': r = 0.83f; g = 0.87f; bl = 1.00f; break;
    case 'F':           r = 0.97f; g = 0.97f; bl = 1.00f; break;
    case 'G':           r = 1.00f; g = 0.95f; bl = 0.90f; break;
    case 'K':           r = 1.00f; g = 0.81f; bl = 0.62f; break;
    case 'M':           r = 1.00f; g = 0.63f; bl = 0.44f; break;
    case 'C': case 'S': r = 1.00f; g = 0.45f; bl = 0.36f; break;
    case 'L': case 'T': r = 1.00f; g = 0.50f; bl = 0.42f; break;
    default: return bv_color(ci, b);
  }
  return rgb565((uint8_t)(255 * b * r), (uint8_t)(255 * b * g), (uint8_t)(255 * b * bl));
}

void draw_stars(M5Canvas& g, const Projector& p, const Triad& t, bool labels) {
  g_center = { -1, 0, 0, false };
  float bestD2 = 1e9f;
  const float horizon = sinf(-1.0f * DEG);
  g.setTextDatum(middle_left);
  for (int i = 0; i < STAR_COUNT; i++) {
    Vec3 e = star_vec_at(i);
    if (v_dot(e, t.zenith) < horizon) continue;
    float sx, sy;
    if (!project(p, e, sx, sy) || !on_screen(sx, sy, 2)) continue;
    float mag = star_mag[i];
    float b = brightness(mag);
    uint16_t col = class_color(star_spect[i], star_ci[i], b);
    int rad = mag <= 1.0f ? 2 : (mag <= 2.5f ? 1 : 0);
    if (rad > 0) g.fillCircle((int)sx, (int)sy, rad, col);
    else g.drawPixel((int)sx, (int)sy, col);
    if (labels && mag <= BRIGHT_LABEL_MAG) {
      const char* lb = star_label[i];
      if (lb && lb[0] && !(lb[0] == 'H' && lb[1] == 'I' && lb[2] == 'P')) {
        g.setTextColor(COL_LABEL);
        g.drawString(lb, (int)sx + rad + 3, (int)sy);
      }
    }
    float dx = sx - p.ppx_x, dy = sy - CENTER_Y, d2 = dx * dx + dy * dy;
    if (d2 < bestD2) { bestD2 = d2; g_center = { i, sx, sy, true }; }
  }
  if (bestD2 > (float)(SELECT_RADIUS_PX * SELECT_RADIUS_PX)) g_center.valid = false;
}

void draw_messier(M5Canvas& g, const Projector& p, const Triad& t, bool show) {
  g_centerM = { -1, 0, 0, false };
  if (!show) return;
  float bestD2 = 1e9f;
  const float horizon = sinf(-1.0f * DEG);
  g.setTextDatum(middle_left);
  g.setTextColor(COL_MESSIER);
  for (int i = 0; i < MESSIER_COUNT; i++) {
    Vec3 e = messier_vec_at(i);
    if (v_dot(e, t.zenith) < horizon) continue;
    float sx, sy;
    if (!project(p, e, sx, sy) || !on_screen(sx, sy, 6)) continue;
    int ix = (int)sx, iy = (int)sy;
    g.drawCircle(ix, iy, 2, COL_MESSIER);
    g.drawPixel(ix, iy, COL_MESSIER);
    g.drawString(messier_id[i], ix + 5, iy);
    float dx = sx - p.ppx_x, dy = sy - CENTER_Y, d2 = dx * dx + dy * dy;
    if (d2 < bestD2) { bestD2 = d2; g_centerM = { i, sx, sy, true }; }
  }
  if (bestD2 > (float)(SELECT_RADIUS_PX * SELECT_RADIUS_PX)) g_centerM.valid = false;
}

void draw_constellations(M5Canvas& g, const Projector& p, const Triad& t) {
  if (CONST_SEG_COUNT <= 0) return;
  for (int s = 0; s < CONST_SEG_COUNT; s++) {
    Vec3 ea = star_vec_at(const_seg[s][0]);
    Vec3 eb = star_vec_at(const_seg[s][1]);
    float ax, ay, bx, by;
    if (!project(p, ea, ax, ay)) continue;
    if (!project(p, eb, bx, by)) continue;
    ax = clampf(ax, -2000, 2000); ay = clampf(ay, -2000, 2000);
    bx = clampf(bx, -2000, 2000); by = clampf(by, -2000, 2000);
    g.drawLine((int)ax, (int)ay, (int)bx, (int)by, COL_CONST);
  }
}

void draw_horizon(M5Canvas& g, const Projector& p, const Camera& c, const Triad& t) {
  bool have = false; float px = 0, py = 0;
  for (float a = c.az - (float)M_PI * 0.5f; a <= c.az + (float)M_PI * 0.5f; a += 2.0f * DEG) {
    Vec3 e = v_add(v_scale(t.north, cosf(a)), v_scale(t.east, sinf(a)));
    float sx, sy;
    bool ok = project(p, e, sx, sy);
    if (ok && have)
      g.drawLine((int)clampf(px, -2000, 2000), (int)clampf(py, -2000, 2000),
                 (int)clampf(sx, -2000, 2000), (int)clampf(sy, -2000, 2000), COL_HORIZON);
    have = ok; px = sx; py = sy;
  }
}

void draw_cardinals(M5Canvas& g, const Projector& p, const Triad& t) {
  const char* lbl[4] = { "N", "E", "S", "W" };
  g.setTextDatum(middle_center);
  g.setTextColor(COL_CARDINAL);
  for (int k = 0; k < 4; k++) {
    float a = k * (float)M_PI * 0.5f;
    Vec3 e = v_add(v_scale(t.north, cosf(a)), v_scale(t.east, sinf(a)));
    float sx, sy;
    if (project(p, e, sx, sy) && on_screen(sx, sy, 0)) {
      g.fillCircle((int)sx, (int)sy, 1, COL_CARDINAL);
      g.drawString(lbl[k], (int)sx, (int)sy - 7);
    }
  }
}

static void draw_moon_disk(M5Canvas& g, int cx, int cy, int r, float k, bool waxing) {
  g.fillCircle(cx, cy, r, rgb565(45, 45, 52));
  for (int dy = -r; dy <= r; dy++) {
    float w = sqrtf((float)(r * r - dy * dy));
    float xt = (1.0f - 2.0f * k) * w;
    int y = cy + dy;
    if (waxing) {
      int x0 = (int)lroundf(cx + xt), x1 = (int)lroundf(cx + w);
      if (x1 >= x0) g.drawFastHLine(x0, y, x1 - x0 + 1, COL_MOON);
    } else {
      int x0 = (int)lroundf(cx - w), x1 = (int)lroundf(cx - xt);
      if (x1 >= x0) g.drawFastHLine(x0, y, x1 - x0 + 1, COL_MOON);
    }
  }
  g.drawCircle(cx, cy, r, rgb565(120, 120, 130));
}

void draw_body(M5Canvas& g, const Projector& p, const Triad& t,
               const SkyBody& b, const char* name, uint16_t color, bool label) {
  Vec3 e = vec_from_radec(b.ra, b.dec);
  if (v_dot(e, t.zenith) < sinf(-1.0f * DEG)) return;
  float sx, sy;
  if (!project(p, e, sx, sy) || !on_screen(sx, sy, 8)) return;
  int ix = (int)sx, iy = (int)sy, rad;
  if (b.isMoon) {
    draw_moon_disk(g, ix, iy, MOON_R, b.phase, b.waxing);
    rad = MOON_R;
  } else {
    rad = (b.mag < -2.0f) ? 3 : (b.mag < 1.0f ? 2 : 1);
    g.fillCircle(ix, iy, rad, color);
  }
  if (label) {
    g.setTextDatum(middle_left);
    g.setTextColor(color);
    g.drawString(name, ix + rad + 3, iy);
  }
}

void draw_highlight(M5Canvas& g, const Projector& p, const Triad& t, int kind, int idx, double d) {
  if (kind == SEL_NONE || idx < 0) return;
  Vec3 e = (kind == SEL_MESSIER) ? messier_vec_at(idx)
         : (kind == SEL_BODY)    ? body_vec_at(idx, d)
                                 : star_vec_at(idx);
  float sx, sy;
  if (project(p, e, sx, sy) && on_screen(sx, sy, 4)) {
    uint16_t c = (kind == SEL_MESSIER) ? COL_MESSIER : COL_RETICLE;
    g.drawCircle((int)sx, (int)sy, 6, c);
    g.drawCircle((int)sx, (int)sy, 7, c);
  }
}

static void fmt_ra(double ra, char* out) {
  double h = ra * 12.0 / M_PI; if (h < 0) h += 24.0;
  int hh = (int)h, mm = (int)lround((h - hh) * 60.0);
  if (mm == 60) { mm = 0; hh = (hh + 1) % 24; }
  snprintf(out, 12, "%02d:%02d", hh, mm);
}
static void fmt_dec(double dec, char* out) {
  double dd = dec * 180.0 / M_PI;
  char sgn = dd < 0 ? '-' : '+'; dd = fabs(dd);
  int d = (int)dd, m = (int)lround((dd - d) * 60.0);
  if (m == 60) { m = 0; d += 1; }
  snprintf(out, 12, "%c%02d:%02d", sgn, d, m);
}
static void fmt_dist(float ly, char* out) {
  if (ly < 0) snprintf(out, 16, "Dist --");
  else if (ly < 10.0f) snprintf(out, 16, "Dist %.1f LY", ly);
  else snprintf(out, 16, "Dist %.0f LY", ly);
}
static int az_int(float azrad) { int a = (int)lroundf(azrad * RAD); return ((a % 360) + 360) % 360; }

static void panel_star(M5Canvas& g, const Triad& t, int idx, int tx) {
  Vec3 e = star_vec_at(idx);
  double ra = atan2((double)e.y, (double)e.x); if (ra < 0) ra += 2 * M_PI;
  double dec = asin(clampf(e.z, -1.0f, 1.0f));
  char rabuf[12], decbuf[12], line[40];
  fmt_ra(ra, rabuf); fmt_dec(dec, decbuf);
  g.setTextColor(COL_ACCENT);
  g.drawString(star_label_at(idx), tx, 4);
  snprintf(line, sizeof(line), "m %.1f  %s", star_mag[idx], star_spect[idx] ? star_spect[idx] : "");
  g.drawString(line, tx, 18);
  if (star_con[idx] && star_con[idx][0]) g.drawString(star_con[idx], tx, 30);
  snprintf(line, sizeof(line), "RA  %s", rabuf);  g.drawString(line, tx, 46);
  snprintf(line, sizeof(line), "Dec %s", decbuf); g.drawString(line, tx, 58);
  snprintf(line, sizeof(line), "Alt %+.1f", alt_of(e, t) * RAD); g.drawString(line, tx, 74);
  snprintf(line, sizeof(line), "Az  %03d", az_int(az_of(e, t))); g.drawString(line, tx, 86);
  fmt_dist(star_dist_ly[idx], line); g.drawString(line, tx, 102);
}

static void panel_messier(M5Canvas& g, const Triad& t, int idx, int tx) {
  Vec3 e = messier_vec_at(idx);
  double ra = atan2((double)e.y, (double)e.x); if (ra < 0) ra += 2 * M_PI;
  double dec = asin(clampf(e.z, -1.0f, 1.0f));
  char rabuf[12], decbuf[12], line[40];
  fmt_ra(ra, rabuf); fmt_dec(dec, decbuf);
  g.setTextColor(COL_ACCENT);
  g.drawString(messier_id[idx], tx, 4);
  g.setTextColor(COL_ACCENT);
  if (messier_name[idx] && messier_name[idx][0]) g.drawString(messier_name[idx], tx, 18);
  if (messier_type[idx] && messier_type[idx][0]) g.drawString(messier_type[idx], tx, 32);
  if (messier_mag[idx] < 90.0f) { snprintf(line, sizeof(line), "mag %.1f", messier_mag[idx]); g.drawString(line, tx, 44); }
  if (messier_con[idx] && messier_con[idx][0]) g.drawString(messier_con[idx], tx, 56);
  snprintf(line, sizeof(line), "RA  %s", rabuf);  g.drawString(line, tx, 72);
  snprintf(line, sizeof(line), "Dec %s", decbuf); g.drawString(line, tx, 84);
  snprintf(line, sizeof(line), "Alt %+.1f", alt_of(e, t) * RAD); g.drawString(line, tx, 100);
  snprintf(line, sizeof(line), "Az  %03d", az_int(az_of(e, t))); g.drawString(line, tx, 112);
}

static void panel_body(M5Canvas& g, const Triad& t, int idx, double d, int tx) {
  SkyBody b = ephem_body(idx, d);
  Vec3 e = vec_from_radec(b.ra, b.dec);
  char rabuf[12], decbuf[12], line[40];
  fmt_ra(b.ra, rabuf); fmt_dec(b.dec, decbuf);
  g.setTextColor(COL_ACCENT);
  g.drawString(ephem_name(idx), tx, 4);
  g.setTextColor(COL_ACCENT);
  snprintf(line, sizeof(line), "mag %.1f", b.mag); g.drawString(line, tx, 20);
  snprintf(line, sizeof(line), "RA  %s", rabuf);   g.drawString(line, tx, 36);
  snprintf(line, sizeof(line), "Dec %s", decbuf);  g.drawString(line, tx, 48);
  snprintf(line, sizeof(line), "Alt %+.1f", alt_of(e, t) * RAD); g.drawString(line, tx, 64);
  snprintf(line, sizeof(line), "Az  %03d", az_int(az_of(e, t)));  g.drawString(line, tx, 76);
  if (b.isMoon) {
    snprintf(line, sizeof(line), "Phase %d%%", (int)lroundf(b.phase * 100));
    g.drawString(line, tx, 92);
    g.drawString(b.waxing ? "waxing" : "waning", tx, 104);
  }
}

void draw_panel(M5Canvas& g, const Triad& t, int kind, int idx, double d, float panelX) {
  if (kind == SEL_NONE && panelX <= -(float)PANEL_W + 0.5f) return;
  int x0 = (int)lroundf(panelX);
  g.fillRect(x0, 0, PANEL_W, SCR_H, COL_PANEL_BG);
  if (kind == SEL_NONE) return;
  g.setTextDatum(top_left);
  if (kind == SEL_STAR)         panel_star(g, t, idx, x0 + 4);
  else if (kind == SEL_MESSIER) panel_messier(g, t, idx, x0 + 4);
  else                          panel_body(g, t, idx, d, x0 + 4);
}

static const char* compass16(float deg) {
  static const char* n[16] = { "N","NNE","NE","ENE","E","ESE","SE","SSE",
                               "S","SSW","SW","WSW","W","WNW","NW","NNW" };
  int idx = ((int)floorf(deg / 22.5f + 0.5f)) % 16;
  if (idx < 0) idx += 16;
  return n[idx];
}
static void blend_dark_rect(M5Canvas& g, int x, int y, int w, int h, float a) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCR_W) w = SCR_W - x;
  if (y + h > SCR_H) h = SCR_H - y;
  float keep = 1.0f - a;
  for (int py = y; py < y + h; py++)
    for (int px = x; px < x + w; px++) {
      uint16_t c = g.readPixel(px, py);
      int r = (c >> 11) & 0x1F, gg = (c >> 5) & 0x3F, b = c & 0x1F;
      r = (int)(r * keep); gg = (int)(gg * keep); b = (int)(b * keep);
      g.drawPixel(px, py, (uint16_t)((r << 11) | (gg << 5) | b));
    }
}
void draw_hud(M5Canvas& g, float az_rad, float alt_rad) {
  int bw = 64, bh = 24, bx = SCR_W - bw - 2, by = SCR_H - bh - 2;
  blend_dark_rect(g, bx, by, bw, bh, 0.45f);
  float azd = az_rad * RAD; if (azd < 0) azd += 360; if (azd >= 360) azd -= 360;
  char l1[24], l2[24];
  snprintf(l1, sizeof(l1), "ALT %+d", (int)lroundf(alt_rad * RAD));
  snprintf(l2, sizeof(l2), "AZ %03d %s", az_int(az_rad), compass16(azd));
  g.setTextDatum(bottom_right);
  g.setTextColor(COL_TEXT);
  g.drawString(l1, SCR_W - 5, SCR_H - 14);
  g.drawString(l2, SCR_W - 5, SCR_H - 4);
}

static uint16_t sat_col(uint8_t sys) {
  switch (sys) {
    case 0: return COL_SAT_GPS; case 1: return COL_SAT_GLO; case 2: return COL_SAT_GAL;
    case 3: return COL_SAT_BDS; case 4: return COL_SAT_QZS; default: return COL_SAT_OTHER;
  }
}
void draw_satellites(M5Canvas& g, const Projector& p, const Triad& t) {
  const GnssSat* s; int n = gnss_sats(&s);
  if (n <= 0) return;
  g.setTextDatum(middle_left);
  for (int i = 0; i < n; i++) {
    if (s[i].el < 0) continue;
    float A = s[i].az * DEG, E = s[i].el * DEG;
    float ce = cosf(E), se = sinf(E), ca = cosf(A), sa = sinf(A);
    Vec3 e = v_norm(v_add(v_add(v_scale(t.north, ce * ca), v_scale(t.east, ce * sa)),
                          v_scale(t.zenith, se)));
    if (v_dot(e, t.zenith) < -0.02f) continue;
    float sx, sy;
    if (!project(p, e, sx, sy) || !on_screen(sx, sy, 4)) continue;
    int ix = (int)sx, iy = (int)sy;
    if (s[i].snr > 0) {
      uint16_t c = sat_col(s[i].sys);
      g.fillCircle(ix, iy, 2, c);
      g.drawCircle(ix, iy, 3, c);
      char b[8]; snprintf(b, sizeof(b), "%u", s[i].prn);
      g.setTextColor(c);
      g.drawString(b, ix + 5, iy);
    } else {
      g.drawCircle(ix, iy, 1, COL_SAT_DIM);
    }
  }
}

void draw_sat_indicator(M5Canvas& g, bool fixValid, int count, bool timeOK, bool posOK) {
  int gx = SCR_W - 30, gy = 24;
  uint16_t c = fixValid ? COL_SAT_HUD : COL_SAT_DIM;
  g.drawRect(gx, gy, 4, 4, c);
  g.drawFastHLine(gx - 4, gy + 1, 3, c);
  g.drawFastHLine(gx - 4, gy + 2, 3, c);
  g.drawFastHLine(gx + 5, gy + 1, 3, c);
  g.drawFastHLine(gx + 5, gy + 2, 3, c);
  g.drawLine(gx + 2, gy, gx + 5, gy - 3, c);
  char b[8]; snprintf(b, sizeof(b), "%d", count);
  g.setTextDatum(top_left);
  g.setTextColor(c);
  g.drawString(b, gx + 11, gy - 3);
  g.setTextDatum(top_center);
  if (timeOK) { g.setTextColor(COL_SAT_HUD); g.drawString("t", gx - 2, gy + 7); }
  if (posOK)  { g.setTextColor(COL_SAT_HUD); g.drawString("p", gx + 4, gy + 7); }
}

void draw_crosshair(M5Canvas& g, float axisX) {
  int x = (int)lroundf(axisX), y = CENTER_Y;
  uint16_t c = COL_RETICLE_DIM;
  g.drawFastHLine(x - 5, y, 3, c);
  g.drawFastHLine(x + 3, y, 3, c);
  g.drawFastVLine(x, y - 5, 3, c);
  g.drawFastVLine(x, y + 3, 3, c);
}

void draw_timebar(M5Canvas& g, double simJD, float rate, bool paused) {
  double f = simJD + 0.5; double day = f - floor(f); double H = day * 24.0;
  int hh = (int)H; int mm = (int)((H - hh) * 60.0);
  char t1[20], t2[16];
  snprintf(t1, sizeof(t1), "%02d:%02d UT", hh, mm);
  if (paused) snprintf(t2, sizeof(t2), "PAUSE");
  else if (rate <= 1.0f) snprintf(t2, sizeof(t2), "x1");
  else snprintf(t2, sizeof(t2), "x%g", rate);
  g.setTextDatum(top_right);
  g.setTextColor(COL_LABEL);
  g.drawString(t1, SCR_W - 3, 2);
  g.drawString(t2, SCR_W - 3, 12);
}
