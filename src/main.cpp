#include <M5Cardputer.h>
#include <cmath>
#include <cstring>
#include "config.h"
#include "astro.h"
#include "ephem.h"
#include "render.h"
#include "sim.h"
#include "scroll.h"
#include "gnss.h"
#include "storage.h"
#include "boot_splash.h"

static M5Canvas canvas(&M5Cardputer.Display);

static Camera   cam;
static double   g_simJD = 0;
static uint32_t g_lastMs = 0;
static bool     g_paused = false;
static int      g_rateIdx = 0;
static int      g_selKind = SEL_NONE;
static int      g_selIdx = -1;
static int      g_labelMode = LM_ALL;
static bool     g_sim = false;
static bool     g_imu = false;
static bool     g_follow = false;
static CenterObj g_centerB = { -1, 0, 0, false };
static float    g_lat = DEF_LAT;
static float    g_lon = DEF_LON;
static bool     g_g0Last = true;
static bool     g_gnssOn = false;
static char     g_msg[48] = "";
static uint32_t g_msgUntil = 0;

static const float HORIZON_DOT = -0.01745f;

static const uint16_t BODY_COL[BODY_COUNT] = {
  rgb565(255, 220, 90),
  COL_MOON,
  rgb565(180, 180, 185),
  rgb565(255, 250, 225),
  rgb565(235, 120, 80),
  rgb565(225, 205, 165),
  rgb565(220, 210, 150),
};

static void set_msg(const char* m, uint32_t ms) {
  strncpy(g_msg, m, sizeof(g_msg) - 1);
  g_msg[sizeof(g_msg) - 1] = 0;
  g_msgUntil = millis() + ms;
}

static void draw_centered(const char* s, int y, uint16_t color) {
  canvas.setTextColor(color);
  canvas.setTextDatum(middle_center);
  canvas.drawString(s, CENTER_X, y);
}
static void draw_left(const char* s, int x, int y, uint16_t color) {
  canvas.setTextColor(color);
  canvas.setTextDatum(top_left);
  canvas.drawString(s, x, y);
}

static void boot_prompt() {
  String buf;
  bool done = false;
  while (!done) {
    M5Cardputer.update();
    gnss_poll();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      auto ks = M5Cardputer.Keyboard.keysState();
      for (char c : ks.word)
        if (c >= '0' && c <= '9' && buf.length() < 10) buf += c;
      if (ks.del && buf.length() > 0) buf.remove(buf.length() - 1);
      if (ks.enter && buf.length() == 10) done = true;
    }
    canvas.fillScreen(COL_BG);
    draw_centered("ENTER UTC  (YYMMDDHHMM)", 12, COL_ACCENT);
    String shown = buf + String("__________").substring(buf.length());
    draw_centered(shown.c_str(), 26, COL_STAR);
    draw_centered("[enter] to start", 38, COL_LABEL);

    int x = 10, y = 50;
    draw_left("pan ,;./   zoom =/-   time [ ]",   x, y, COL_LABEL); y += 10;
    draw_left("r rate  l labels  s scope  i imu", x, y, COL_LABEL); y += 10;
    draw_left("g gnss on/off  p pos  t time",     x, y, COL_LABEL); y += 10;
    draw_left("enter/click select   del back",    x, y, COL_LABEL); y += 10;
    draw_left("space pause   btn0 screenshot",    x, y, COL_LABEL);

    {
      const char* L[3] = { "Scroll", "SD", "LoRa" };
      bool ok[3] = { scroll_ok(), storage_ok(), gnss_present() };
      const int sy = 120, dotR = 3, dgap = 4, itemGap = 12;
      canvas.setTextDatum(middle_left);
      canvas.setTextColor(COL_TEXT);
      int total = 0;
      for (int i = 0; i < 3; i++) total += dotR * 2 + dgap + canvas.textWidth(L[i]) + (i < 2 ? itemGap : 0);
      int px = (SCR_W - total) / 2;
      for (int i = 0; i < 3; i++) {
        canvas.fillCircle(px + dotR, sy, dotR, ok[i] ? COL_OK : COL_BAD);
        px += dotR * 2 + dgap;
        canvas.drawString(L[i], px, sy);
        px += canvas.textWidth(L[i]) + itemGap;
      }
    }
    canvas.pushSprite(0, 0);
    delay(16);
  }
  int yy = buf.substring(0, 2).toInt();
  int mo = buf.substring(2, 4).toInt();
  int dd = buf.substring(4, 6).toInt();
  int hh = buf.substring(6, 8).toInt();
  int mi = buf.substring(8, 10).toInt();
  g_simJD = jd_from_utc(2000 + yy, mo, dd, hh, mi, 0.0);
}

static Vec3 sel_vec(int kind, int idx, double d) {
  if (kind == SEL_STAR)    return star_vec_at(idx);
  if (kind == SEL_MESSIER) return messier_vec_at(idx);
  if (kind == SEL_BODY)    return body_vec_at(idx, d);
  return { 0, 0, 1 };
}

static inline float d2axis(const CenterObj& o, float axisX) {
  float dx = o.sx - axisX, dy = o.sy - CENTER_Y;
  return dx * dx + dy * dy;
}

static void dismiss_panel() {
  cam.panelXT = -(float)PANEL_W;
  cam.ppxT = 0.0f;
}
static void detach() {
  if (g_selKind != SEL_NONE) { g_follow = false; dismiss_panel(); }
}

static void select_nearest(const Triad& t, double d) {
  const CenterObj& cs = center_star();
  const CenterObj& cm = center_messier();
  const CenterObj& cb = g_centerB;
  if (!cs.valid && !cm.valid && !cb.valid) return;
  float axis = CENTER_X + cam.ppx;
  float ds = cs.valid ? d2axis(cs, axis) : 1e9f;
  float dm = cm.valid ? d2axis(cm, axis) : 1e9f;
  float db = cb.valid ? d2axis(cb, axis) : 1e9f;
  if (db <= ds && db <= dm)      { g_selKind = SEL_BODY;    g_selIdx = cb.idx; }
  else if (dm <= ds)             { g_selKind = SEL_MESSIER; g_selIdx = cm.idx; }
  else                           { g_selKind = SEL_STAR;    g_selIdx = cs.idx; }
  g_follow = true;
  Vec3 e = sel_vec(g_selKind, g_selIdx, d);
  cam.azT = az_of(e, t);
  cam.altT = alt_of(e, t);
  cam.ppxT = PP_OFFSET_DOCKED;
  cam.panelXT = 0.0f;
  float fmax = compute_fov_max(cam.ppxT);
  if (cam.fovT > fmax) cam.fovT = fmax;
}

static void handle_char(char ch, const Triad& t, double d) {
  switch (ch) {
    case KEY_PAN_LEFT:  detach(); camera_pan(cam, -pan_fraction(cam), 0); break;
    case KEY_PAN_RIGHT: detach(); camera_pan(cam, +pan_fraction(cam), 0); break;
    case KEY_PAN_UP:    detach(); camera_pan(cam, 0, +pan_fraction(cam)); break;
    case KEY_PAN_DOWN:  detach(); camera_pan(cam, 0, -pan_fraction(cam)); break;
    case KEY_ZOOM_IN:   camera_zoom(cam, ZOOM_FACTOR); break;
    case KEY_ZOOM_OUT:  camera_zoom(cam, 1.0f / ZOOM_FACTOR); break;
    case KEY_TIME_BACK: g_simJD -= TIME_STEP_MIN / 1440.0; break;
    case KEY_TIME_FWD:  g_simJD += TIME_STEP_MIN / 1440.0; break;
    case KEY_RATE:      g_rateIdx = (g_rateIdx + 1) % TIME_RATE_COUNT; break;
    case KEY_LABELS: {
      int modes = (g_gnssOn && gnss_present()) ? 4 : 3;
      g_labelMode = (g_labelMode + 1) % modes;
      break;
    }
    case KEY_GNSS_POLL:
      g_gnssOn = !g_gnssOn;
      if (!g_gnssOn && g_labelMode == LM_SATS) g_labelMode = LM_ALL;
      set_msg(g_gnssOn ? "GNSS polling on" : "GNSS polling off", 2000);
      break;
    case KEY_SIM:       if (g_selKind != SEL_NONE) g_sim = true; break;
    case KEY_IMU:       g_imu = !g_imu; break;
    case KEY_GNSS_POS:
      if (gnss_pos_valid()) {
        float la, lo;
        if (gnss_get_pos(la, lo)) { g_lat = la; g_lon = lo; storage_save_pos(la, lo); }
      } else set_msg("no GNSS position yet", 2500);
      break;
    case KEY_GNSS_TIME:
      if (gnss_time_valid()) {
        int Y, Mo, D, h, mi, s;
        if (gnss_get_datetime(Y, Mo, D, h, mi, s))
          g_simJD = jd_from_utc(Y, Mo, D, h, mi, (double)s);
      } else set_msg("no GNSS time yet", 2500);
      break;
    default: break;
  }
}

static void handle_input(const Triad& t, double d) {
  if (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) return;
  auto ks = M5Cardputer.Keyboard.keysState();
  if (g_sim) {
    for (char ch : ks.word) if (ch == KEY_SIM) g_sim = false;
    if (ks.del) g_sim = false;
    return;
  }
  for (char ch : ks.word) handle_char(ch, t, d);
  if (ks.enter) select_nearest(t, d);
  if (ks.del)   { g_follow = false; dismiss_panel(); }
  if (ks.space) g_paused = !g_paused;
}

static void jd_to_utc(double jd, int& Y, int& M, int& D, int& h, int& mi, int& s) {
  double z = floor(jd + 0.5), f = (jd + 0.5) - z, A;
  if (z < 2299161) A = z;
  else { double al = floor((z - 1867216.25) / 36524.25); A = z + 1 + al - floor(al / 4); }
  double B = A + 1524, C = floor((B - 122.1) / 365.25);
  double Dd = floor(365.25 * C), E = floor((B - Dd) / 30.6001);
  double day = B - Dd - floor(30.6001 * E) + f;
  D = (int)floor(day); double frac = day - D;
  M = (E < 14) ? (int)(E - 1) : (int)(E - 13);
  Y = (M > 2) ? (int)(C - 4716) : (int)(C - 4715);
  double hours = frac * 24; h = (int)hours; mi = (int)((hours - h) * 60.0);
  s = (int)(((((hours - h) * 60.0) - mi) * 60.0) + 0.5);
  if (s >= 60) { s -= 60; mi++; } if (mi >= 60) { mi -= 60; h++; } if (h >= 24) { h -= 24; D++; }
}
static void do_screenshot(double simJD, float azRad, float altRad) {
  int Y, Mo, D, h, mi, s; jd_to_utc(simJD, Y, Mo, D, h, mi, s);
  int alt = (int)lroundf(altRad * 57.29578f);
  int az  = (((int)lroundf(azRad * 57.29578f)) % 360 + 360) % 360;
  char path[80];
  snprintf(path, sizeof(path), "/stellar-map/%02d%02d%02d%02d%02d-alt%+03d-az%03d.bmp",
           Y % 100, Mo, D, h, mi, alt, az);
  bool ok = storage_screenshot(canvas, path);
  if (ok) { char m[48]; snprintf(m, sizeof(m), "saved %s", path + 13); set_msg(m, 2500); }
  else set_msg("screenshot: no SD", 2500);
}
static void draw_msg() {
  if (g_msg[0] == 0 || millis() >= g_msgUntil) return;
  canvas.setTextDatum(bottom_left);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(g_msg, 3, SCR_H - 3);
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(160);

  canvas.setColorDepth(16);
  if (!canvas.createSprite(SCR_W, SCR_H)) {
    M5Cardputer.Display.fillScreen(rgb565(180, 0, 0));
    M5Cardputer.Display.drawString("canvas alloc failed", 4, 4);
    while (true) delay(1000);
  }
  canvas.setTextFont(&fonts::Font0);

  storage_begin();
  SiteConfig site; storage_load_config(site);
  g_lat = site.lat; g_lon = site.lon;
#if SCROLL_ENABLE
  scroll_begin();
#endif
#if GNSS_ENABLE
  gnss_begin();
#endif
  pinMode(BTN_G0, INPUT_PULLUP);

  boot_splash(canvas);
  boot_prompt();
  camera_init(cam);
  g_lastMs = millis();
}

void loop() {
  uint32_t t0 = millis();
  M5Cardputer.update();
  if (g_gnssOn) gnss_poll();
  bool g0 = digitalRead(BTN_G0);
  bool wantShot = (!g0 && g_g0Last);
  g_g0Last = g0;

  uint32_t now = millis();
  float dt = (float)(now - g_lastMs);
  g_lastMs = now;
  if (!g_paused) g_simJD += (dt / 86400000.0) * TIME_RATES[g_rateIdx];

  double lst = lst_rad(g_simJD, g_lon);
  Triad triad = horizon_triad(g_lat * (M_PI / 180.0), lst);
  double d = g_simJD - 2451543.5;

  handle_input(triad, d);

#if SCROLL_ENABLE
  int  dz   = scroll_delta();
  bool sbtn = scroll_button_edge();
  if (!g_sim) {
    if (dz != 0) camera_zoom(cam, powf(SCROLL_ZOOM_STEP, (float)(dz * SCROLL_DIR)));
    if (sbtn) select_nearest(triad, d);
  }
#endif

  if (g_imu && !g_sim) {
    M5.Imu.update();
    float gv[3];
    if (M5.Imu.getGyro(&gv[0], &gv[1], &gv[2])) {
      float azr  = gv[IMU_AZ_AXIS]  * IMU_AZ_SIGN;
      float altr = gv[IMU_ALT_AXIS] * IMU_ALT_SIGN;
      if (fabsf(azr)  < IMU_DEADZONE) azr  = 0;
      if (fabsf(altr) < IMU_DEADZONE) altr = 0;
      if (azr != 0.0f || altr != 0.0f) {
        detach();
        float k = IMU_GAIN * (float)(M_PI / 180.0) * (dt / 1000.0f);
        camera_pan(cam, (azr * k) / cam.fov, (altr * k) / cam.fov);
      }
    }
  }

  if (g_follow && g_selKind != SEL_NONE) {
    Vec3 e = sel_vec(g_selKind, g_selIdx, d);
    cam.azT = az_of(e, triad);
    cam.altT = alt_of(e, triad);
  }
  camera_ease(cam, dt);

  if (cam.panelXT <= -(float)PANEL_W + 0.1f && cam.panelX <= -(float)PANEL_W + 0.5f) {
    g_selKind = SEL_NONE; g_selIdx = -1;
  }

  Projector proj;
  projector_build(proj, cam, triad);

  if (g_sim) {
    sim_draw(canvas, g_selKind, g_selIdx, d);
    draw_msg();
    canvas.pushSprite(0, 0);
    if (wantShot) do_screenshot(g_simJD, cam.az, cam.alt);
    uint32_t s = millis() - t0;
    if (s < 16) delay(16 - s);
    return;
  }

  bool showSome = g_labelMode >= LM_SOME;
  bool showAll  = g_labelMode >= LM_ALL;
  bool showSats = (g_labelMode == LM_SATS) && g_gnssOn && gnss_present();

  canvas.fillScreen(COL_BG);
  draw_horizon(canvas, proj, cam, triad);
  if (showSome) draw_cardinals(canvas, proj, triad);
  if (showSome) draw_constellations(canvas, proj, triad);
  draw_stars(canvas, proj, triad, showAll);
  draw_messier(canvas, proj, triad, showAll);

  g_centerB = { -1, 0, 0, false };
  float bbest = 1e9f;
  for (int bi = 0; bi < BODY_COUNT; bi++) {
    SkyBody b = ephem_body(bi, d);
    draw_body(canvas, proj, triad, b, ephem_name(bi), BODY_COL[bi], showSome);
    Vec3 e = vec_from_radec(b.ra, b.dec);
    if (v_dot(e, triad.zenith) < HORIZON_DOT) continue;
    float sx, sy;
    if (project(proj, e, sx, sy) && on_screen(sx, sy, 4)) {
      float dx = sx - proj.ppx_x, dy = sy - CENTER_Y, dd = dx * dx + dy * dy;
      if (dd < bbest) { bbest = dd; g_centerB = { bi, sx, sy, true }; }
    }
  }
  if (bbest > (float)(SELECT_RADIUS_PX * SELECT_RADIUS_PX)) g_centerB.valid = false;

  if (showSats) draw_satellites(canvas, proj, triad);

  draw_highlight(canvas, proj, triad, g_selKind, g_selIdx, d);
  draw_panel(canvas, triad, g_selKind, g_selIdx, d, cam.panelX);
  draw_crosshair(canvas, proj.ppx_x);
  draw_timebar(canvas, g_simJD, TIME_RATES[g_rateIdx], g_paused);
  draw_hud(canvas, cam.az, cam.alt);
  if (g_gnssOn && gnss_present())
    draw_sat_indicator(canvas, gnss_fix(), gnss_count_display(),
                       gnss_time_valid(), gnss_pos_valid());
  draw_msg();

  canvas.pushSprite(0, 0);
  if (wantShot) do_screenshot(g_simJD, cam.az, cam.alt);

  uint32_t spent = millis() - t0;
  if (spent < 16) delay(16 - spent);
}
