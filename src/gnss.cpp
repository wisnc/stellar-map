#include "gnss.h"
#include "config.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>

static bool     g_present = false;
static bool     g_haveTime = false;
static bool     g_havePos  = false;
static int      g_y = 2000, g_mo = 1, g_d = 1, g_h = 0, g_mi = 0, g_s = 0;
static float    g_lat = 0, g_lon = 0;
static int      g_fixQual = 0, g_satsUsed = 0;

static GnssSat  g_sats[GNSS_MAX_SATS];
static int      g_satCount = 0;

static char     g_line[100];
static uint8_t  g_len = 0;

static int hexd(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
static bool checksum_ok(const char* s) {
  if (s[0] != '$') return false;
  const char* p = s + 1; uint8_t cs = 0;
  while (*p && *p != '*') { cs ^= (uint8_t)*p; ++p; }
  if (*p != '*') return false;
  int hi = hexd(p[1]), lo = hexd(p[2]);
  if (hi < 0 || lo < 0) return false;
  return cs == (uint8_t)((hi << 4) | lo);
}
static int split(char* s, char** f, int maxf) {
  int n = 0; f[n++] = s;
  for (; *s && n < maxf; ++s)
    if (*s == ',' || *s == '*') { *s = 0; f[n++] = s + 1; }
  return n;
}
static int fi(const char* s) { return (s && *s) ? atoi(s) : 0; }
static float nmea_coord(const char* s, const char* hemi) {
  if (!s || !*s) return 0;
  double v = atof(s);
  int deg = (int)(v / 100.0);
  double mn = v - deg * 100.0;
  double d = deg + mn / 60.0;
  if (hemi && (hemi[0] == 'S' || hemi[0] == 'W')) d = -d;
  return (float)d;
}
static uint8_t sys_of(char a, char b) {
  if (a == 'G') switch (b) {
    case 'P': return 0; case 'L': return 1; case 'A': return 2;
    case 'B': return 3; case 'Q': return 4; default: return 5;
  }
  if (a == 'B' && b == 'D') return 3;
  return 5;
}
static void remove_sys(uint8_t sys) {
  int w = 0;
  for (int r = 0; r < g_satCount; r++)
    if (g_sats[r].sys != sys) g_sats[w++] = g_sats[r];
  g_satCount = w;
}
static void add_sat(uint16_t prn, uint8_t sys, int az, int el, int snr) {
  if (g_satCount >= GNSS_MAX_SATS) return;
  GnssSat& s = g_sats[g_satCount++];
  s.prn = prn; s.sys = sys;
  s.az  = (int16_t)az;
  s.el  = (int8_t)el;
  s.snr = (uint8_t)(snr < 0 ? 0 : (snr > 99 ? 99 : snr));
}

static void parse_line(char* s) {
  if (!checksum_ok(s)) return;
  char* f[28];
  int n = split(s, f, 28);
  if (n < 1 || strlen(f[0]) < 6) return;
  g_present = true;
  const char* typ = f[0] + 3;

  if (!strncmp(typ, "RMC", 3)) {
    bool valid = (n > 2 && f[2] && f[2][0] == 'A');
    if (n > 1 && f[1] && strlen(f[1]) >= 6) {
      g_h  = (f[1][0]-'0')*10 + (f[1][1]-'0');
      g_mi = (f[1][2]-'0')*10 + (f[1][3]-'0');
      g_s  = (f[1][4]-'0')*10 + (f[1][5]-'0');
    }
    if (n > 9 && f[9] && strlen(f[9]) >= 6) {
      g_d  = (f[9][0]-'0')*10 + (f[9][1]-'0');
      g_mo = (f[9][2]-'0')*10 + (f[9][3]-'0');
      g_y  = 2000 + (f[9][4]-'0')*10 + (f[9][5]-'0');
      if (g_d >= 1 && g_mo >= 1) g_haveTime = true;
    }
    if (valid && n > 6) {
      g_lat = nmea_coord(f[3], f[4]);
      g_lon = nmea_coord(f[5], f[6]);
      g_havePos = true;
    }
  } else if (!strncmp(typ, "GGA", 3)) {
    if (n > 7) { g_fixQual = fi(f[6]); g_satsUsed = fi(f[7]); }
    if (g_fixQual > 0 && n > 5) {
      g_lat = nmea_coord(f[2], f[3]);
      g_lon = nmea_coord(f[4], f[5]);
      g_havePos = true;
    }
  } else if (!strncmp(typ, "GSV", 3)) {
    uint8_t sys = sys_of(f[0][1], f[0][2]);
    int msgNum = (n > 2) ? fi(f[2]) : 0;
    if (msgNum == 1) remove_sys(sys);
    for (int i = 4; i + 3 < n; i += 4) {
      uint16_t prn = (uint16_t)fi(f[i]);
      if (!prn) continue;
      add_sat(prn, sys, fi(f[i + 2]), fi(f[i + 1]), fi(f[i + 3]));
    }
  }
}

void gnss_begin() {
#if GNSS_ENABLE
  Serial1.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX, GNSS_TX);
#endif
}
void gnss_poll() {
#if GNSS_ENABLE
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') { g_line[g_len] = 0; if (g_len) parse_line(g_line); g_len = 0; }
    else if (g_len < sizeof(g_line) - 1) g_line[g_len++] = c;
    else g_len = 0;
  }
#endif
}
bool gnss_present()    { return g_present; }
bool gnss_time_valid() { return g_haveTime; }
bool gnss_pos_valid()  { return g_havePos; }
bool gnss_fix()        { return g_fixQual > 0; }
int  gnss_sats_used()  { return g_satsUsed; }
int  gnss_count_display() {
  if (g_fixQual > 0) return g_satsUsed;
  int tracked = 0;
  for (int i = 0; i < g_satCount; i++) if (g_sats[i].snr > 0) tracked++;
  return tracked > 0 ? tracked : g_satCount;
}
bool gnss_get_datetime(int& y, int& mo, int& d, int& h, int& mi, int& s) {
  if (!g_haveTime) return false;
  y = g_y; mo = g_mo; d = g_d; h = g_h; mi = g_mi; s = g_s; return true;
}
bool gnss_get_pos(float& lat, float& lon) {
  if (!g_havePos) return false;
  lat = g_lat; lon = g_lon; return true;
}
int gnss_sats(const GnssSat** out) { *out = g_sats; return g_satCount; }
