#pragma once
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Vec3 { float x, y, z; };

static inline float v_dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline Vec3  v_cross(const Vec3& a, const Vec3& b) {
  return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
static inline Vec3  v_sub(const Vec3& a, const Vec3& b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
static inline Vec3  v_add(const Vec3& a, const Vec3& b) { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
static inline Vec3  v_scale(const Vec3& a, float s)      { return { a.x*s, a.y*s, a.z*s }; }
static inline Vec3  v_norm(const Vec3& a) { float n = std::sqrt(v_dot(a, a)); return n > 0 ? v_scale(a, 1.0f/n) : a; }

double jd_from_utc(int y, int mo, int d, int h, int mi, double s);
double gmst_deg(double jd);
double lst_rad(double jd, double lon_e_deg);

struct Triad { Vec3 zenith, north, east; };
Triad horizon_triad(double lat_rad, double lst_rad);

Vec3  vec_from_radec(double ra, double dec);
float alt_of(const Vec3& e, const Triad& t);
float az_of (const Vec3& e, const Triad& t);
