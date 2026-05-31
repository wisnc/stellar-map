#include "ephem.h"
#include <cmath>

static const double DEG = M_PI / 180.0, RAD = 180.0 / M_PI;

static double norm360(double x) { x = fmod(x, 360.0); return x < 0 ? x + 360.0 : x; }

static double kepler(double M, double e) {
  double E = M + RAD * e * sin(M * DEG) * (1 + e * cos(M * DEG));
  for (int k = 0; k < 8; k++)
    E = E - (E - RAD * e * sin(E * DEG) - M) / (1 - e * cos(E * DEG));
  return E;
}

struct Elem { double N, i, w, a, e, M; };

static double sun_w(double d) { return 282.9404 + 4.70935e-5 * d; }

static void sun_pos(double d, double& lon, double& r, double& M) {
  double w = sun_w(d), e = 0.016709 - 1.151e-9 * d;
  M = norm360(356.0470 + 0.9856002585 * d);
  double E = kepler(M, e);
  double xv = cos(E * DEG) - e, yv = sqrt(1 - e * e) * sin(E * DEG);
  double v = atan2(yv, xv) * RAD;
  r = hypot(xv, yv);
  lon = norm360(v + w);
}

static double obl(double d) { return 23.4393 - 3.563e-7 * d; }

static void rect_to_eq(double x, double y, double z, double ec, double& ra, double& dec) {
  double xe = x;
  double ye = y * cos(ec * DEG) - z * sin(ec * DEG);
  double ze = y * sin(ec * DEG) + z * cos(ec * DEG);
  ra = atan2(ye, xe); if (ra < 0) ra += 2 * M_PI;
  dec = atan2(ze, sqrt(xe * xe + ye * ye));
}

static Elem planet_elem(int p, double d) {
  switch (p) {
    case MERCURY: return { norm360(48.3313 + 3.24587e-5 * d), 7.0047 + 5.00e-8 * d,
                           norm360(29.1241 + 1.01444e-5 * d), 0.387098,
                           0.205635 + 5.59e-10 * d, norm360(168.6562 + 4.0923344368 * d) };
    case VENUS:   return { norm360(76.6799 + 2.46590e-5 * d), 3.3946 + 2.75e-8 * d,
                           norm360(54.8910 + 1.38374e-5 * d), 0.723330,
                           0.006773 - 1.302e-9 * d, norm360(48.0052 + 1.6021302244 * d) };
    case MARS:    return { norm360(49.5574 + 2.11081e-5 * d), 1.8497 - 1.78e-8 * d,
                           norm360(286.5016 + 2.92961e-5 * d), 1.523688,
                           0.093405 + 2.516e-9 * d, norm360(18.6021 + 0.5240207766 * d) };
    case JUPITER: return { norm360(100.4542 + 2.76854e-5 * d), 1.3030 - 1.557e-7 * d,
                           norm360(273.8777 + 1.64505e-5 * d), 5.20256,
                           0.048498 + 4.469e-9 * d, norm360(19.8950 + 0.0830853001 * d) };
    case SATURN:  return { norm360(113.6634 + 2.38980e-5 * d), 2.4886 - 1.081e-7 * d,
                           norm360(339.3939 + 2.97661e-5 * d), 9.55475,
                           0.055546 - 9.499e-9 * d, norm360(316.9670 + 0.0334442282 * d) };
  }
  return { 0, 0, 0, 1, 0, 0 };
}

static SkyBody compute_sun(double d) {
  double lon, r, M; sun_pos(d, lon, r, M);
  double xs = r * cos(lon * DEG), ys = r * sin(lon * DEG);
  double ra, dec; rect_to_eq(xs, ys, 0.0, obl(d), ra, dec);
  return { ra, dec, -26.74f, 1.0f, false, false };
}

static SkyBody compute_moon(double d) {
  double N = norm360(125.1228 - 0.0529538083 * d), i = 5.1454;
  double w = norm360(318.0634 + 0.1643573223 * d), a = 60.2666, e = 0.054900;
  double M = norm360(115.3654 + 13.0649929509 * d);
  double E = kepler(M, e);
  double x = a * (cos(E * DEG) - e), y = a * sqrt(1 - e * e) * sin(E * DEG);
  double r = hypot(x, y), v = atan2(y, x) * RAD;
  double xh = r * (cos(N * DEG) * cos((v + w) * DEG) - sin(N * DEG) * sin((v + w) * DEG) * cos(i * DEG));
  double yh = r * (sin(N * DEG) * cos((v + w) * DEG) + cos(N * DEG) * sin((v + w) * DEG) * cos(i * DEG));
  double zh = r * (sin((v + w) * DEG) * sin(i * DEG));
  double lon = atan2(yh, xh) * RAD, lat = atan2(zh, hypot(xh, yh)) * RAD;

  double slon, sr, Ms; sun_pos(d, slon, sr, Ms);
  double Ls = norm360(sun_w(d) + Ms), Lm = norm360(N + w + M);
  double Dm = norm360(Lm - Ls), F = norm360(Lm - N);
  lon += -1.274 * sin((M - 2 * Dm) * DEG) + 0.658 * sin((2 * Dm) * DEG) - 0.186 * sin(Ms * DEG)
       -  0.059 * sin((2 * M - 2 * Dm) * DEG) - 0.057 * sin((M - 2 * Dm + Ms) * DEG)
       +  0.053 * sin((M + 2 * Dm) * DEG) + 0.046 * sin((2 * Dm - Ms) * DEG)
       +  0.041 * sin((M - Ms) * DEG) - 0.035 * sin(Dm * DEG) - 0.031 * sin((M + Ms) * DEG)
       -  0.015 * sin((2 * F - 2 * Dm) * DEG) + 0.011 * sin((M - 4 * Dm) * DEG);
  lat += -0.173 * sin((F - 2 * Dm) * DEG) - 0.055 * sin((M - F - 2 * Dm) * DEG)
       -  0.046 * sin((M + F - 2 * Dm) * DEG) + 0.033 * sin((F + 2 * Dm) * DEG)
       +  0.017 * sin((2 * M + F) * DEG);

  double xr = r * cos(lon * DEG) * cos(lat * DEG);
  double yr = r * sin(lon * DEG) * cos(lat * DEG);
  double zr = r * sin(lat * DEG);
  double ra, dec; rect_to_eq(xr, yr, zr, obl(d), ra, dec);

  double elong = acos(fmax(-1.0, fmin(1.0, cos((lon - slon) * DEG) * cos(lat * DEG)))) * RAD;
  double k = (1 + cos((180 - elong) * DEG)) / 2;
  bool waxing = norm360(lon - slon) < 180.0;
  return { ra, dec, -12.7f, (float)k, waxing, true };
}

static SkyBody compute_planet(int p, double d) {
  Elem el = planet_elem(p, d);
  double E = kepler(el.M, el.e);
  double x = el.a * (cos(E * DEG) - el.e), y = el.a * sqrt(1 - el.e * el.e) * sin(E * DEG);
  double r = hypot(x, y), v = atan2(y, x) * RAD;
  double xh = r * (cos(el.N * DEG) * cos((v + el.w) * DEG) - sin(el.N * DEG) * sin((v + el.w) * DEG) * cos(el.i * DEG));
  double yh = r * (sin(el.N * DEG) * cos((v + el.w) * DEG) + cos(el.N * DEG) * sin((v + el.w) * DEG) * cos(el.i * DEG));
  double zh = r * (sin((v + el.w) * DEG) * sin(el.i * DEG));
  double lon = atan2(yh, xh) * RAD, lat = atan2(zh, hypot(xh, yh)) * RAD;

  if (p == JUPITER || p == SATURN) {
    double Mj = planet_elem(JUPITER, d).M, Msat = planet_elem(SATURN, d).M;
    if (p == JUPITER) {
      lon += -0.332 * sin((2 * Mj - 5 * Msat - 67.6) * DEG)
           -  0.056 * sin((2 * Mj - 2 * Msat + 21) * DEG)
           +  0.042 * sin((3 * Mj - 5 * Msat + 21) * DEG)
           -  0.036 * sin((Mj - 2 * Msat) * DEG)
           +  0.022 * cos((Mj - Msat) * DEG)
           +  0.023 * sin((2 * Mj - 3 * Msat + 52) * DEG)
           -  0.016 * sin((Mj - 5 * Msat - 69) * DEG);
    } else {
      lon += +0.812 * sin((2 * Mj - 5 * Msat - 67.6) * DEG)
           -  0.229 * cos((2 * Mj - 4 * Msat - 2) * DEG)
           +  0.119 * sin((Mj - 2 * Msat - 3) * DEG)
           +  0.046 * sin((2 * Mj - 6 * Msat - 69) * DEG)
           +  0.014 * sin((Mj - 3 * Msat + 32) * DEG);
      lat += -0.020 * cos((2 * Mj - 4 * Msat - 2) * DEG)
           +  0.018 * sin((2 * Mj - 6 * Msat - 49) * DEG);
    }
  }

  xh = r * cos(lon * DEG) * cos(lat * DEG);
  yh = r * sin(lon * DEG) * cos(lat * DEG);
  zh = r * sin(lat * DEG);
  double slon, sr, sM; sun_pos(d, slon, sr, sM);
  double xs = sr * cos(slon * DEG), ys = sr * sin(slon * DEG);
  double xg = xh + xs, yg = yh + ys, zg = zh;
  double ra, dec; rect_to_eq(xg, yg, zg, obl(d), ra, dec);

  double Rg = sqrt(xg * xg + yg * yg + zg * zg);
  double cosFV = (r * r + Rg * Rg - sr * sr) / (2 * r * Rg);
  if (cosFV > 1) cosFV = 1; if (cosFV < -1) cosFV = -1;
  double FV = acos(cosFV) * RAD;
  double lr = 5 * log10(r * Rg);
  double mag = 0;
  switch (p) {
    case MERCURY: mag = -0.36 + lr + 0.027 * FV + 2.2e-13 * pow(FV, 6); break;
    case VENUS:   mag = -4.34 + lr + 0.013 * FV + 4.2e-7 * FV * FV * FV; break;
    case MARS:    mag = -1.51 + lr + 0.016 * FV; break;
    case JUPITER: mag = -9.25 + lr + 0.014 * FV; break;
    case SATURN:  mag = -9.0  + lr + 0.044 * FV; break;
  }
  return { ra, dec, (float)mag, 1.0f, false, false };
}

SkyBody ephem_body(int which, double d) {
  if (which == SUN)  return compute_sun(d);
  if (which == MOON) return compute_moon(d);
  return compute_planet(which, d);
}

const char* ephem_name(int which) {
  switch (which) {
    case SUN: return "Sun";   case MOON: return "Moon";  case MERCURY: return "Mercury";
    case VENUS: return "Venus"; case MARS: return "Mars"; case JUPITER: return "Jupiter";
    case SATURN: return "Saturn";
  }
  return "?";
}
