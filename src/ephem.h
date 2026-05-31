#pragma once

struct SkyBody {
  double ra, dec;
  float  mag;
  float  phase;
  bool   waxing;
  bool   isMoon;
};

enum Planet { SUN = 0, MOON, MERCURY, VENUS, MARS, JUPITER, SATURN, BODY_COUNT };

SkyBody ephem_body(int which, double d);
const char* ephem_name(int which);
