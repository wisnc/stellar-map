#include "astro.h"

static const double DEG = M_PI / 180.0;

double jd_from_utc(int y, int mo, int d, int h, int mi, double s) {
  int a = (14 - mo) / 12;
  int yy = y + 4800 - a;
  int m = mo + 12 * a - 3;
  long jdn = d + (153L * m + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
  return (double)jdn + (h - 12) / 24.0 + mi / 1440.0 + s / 86400.0;
}

double gmst_deg(double jd) {
  double T = (jd - 2451545.0) / 36525.0;
  double g = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
           + 0.000387933 * T * T - T * T * T / 38710000.0;
  g = fmod(g, 360.0);
  if (g < 0) g += 360.0;
  return g;
}

double lst_rad(double jd, double lon_e_deg) {
  double l = fmod(gmst_deg(jd) + lon_e_deg, 360.0);
  if (l < 0) l += 360.0;
  return l * DEG;
}

Triad horizon_triad(double lat, double lst) {
  double cphi = cos(lat), sphi = sin(lat);
  Vec3 z = { (float)(cphi * cos(lst)), (float)(cphi * sin(lst)), (float)sphi };
  Vec3 ncp = { 0, 0, 1 };
  float d = v_dot(ncp, z);
  Vec3 nh = v_norm(v_sub(ncp, v_scale(z, d)));
  Vec3 e = v_cross(nh, z);
  return { z, nh, e };
}

Vec3 vec_from_radec(double ra, double dec) {
  double cd = cos(dec);
  return { (float)(cd * cos(ra)), (float)(cd * sin(ra)), (float)sin(dec) };
}

float alt_of(const Vec3& e, const Triad& t) {
  float d = v_dot(e, t.zenith);
  if (d > 1) d = 1; if (d < -1) d = -1;
  return asinf(d);
}

float az_of(const Vec3& e, const Triad& t) {
  float a = atan2f(v_dot(e, t.east), v_dot(e, t.north));
  if (a < 0) a += 2 * (float)M_PI;
  return a;
}
