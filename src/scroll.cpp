#include "scroll.h"
#include "config.h"
#include <Wire.h>

static bool g_ok = false;
static bool g_btnPrev = false;

static int32_t read_inc() {
  int32_t val = 0;
  Wire.beginTransmission(SCROLL_ADDR);
  Wire.write((uint8_t)0x50);
  Wire.endTransmission(false);
  if (Wire.requestFrom((uint8_t)SCROLL_ADDR, (uint8_t)4) == 4) {
    uint8_t* p = (uint8_t*)&val;
    for (int i = 0; i < 4; i++) p[i] = Wire.read();
  }
  return val;
}

void scroll_begin() {
  Wire.begin(SCROLL_SDA, SCROLL_SCL, SCROLL_I2C_HZ);
  Wire.beginTransmission(SCROLL_ADDR);
  g_ok = (Wire.endTransmission() == 0);
  g_btnPrev = false;
  if (g_ok) read_inc();
}

bool scroll_ok() { return g_ok; }

int scroll_delta() {
  if (!g_ok) return 0;
  return (int)read_inc();
}

bool scroll_button_edge() {
  if (!g_ok) return false;
  Wire.beginTransmission(SCROLL_ADDR);
  Wire.write((uint8_t)0x20);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)SCROLL_ADDR, (uint8_t)1) != 1) return false;
  uint8_t s = Wire.read();
  bool pressed = (s == 0);
  bool edge = pressed && !g_btnPrev;
  g_btnPrev = pressed;
  return edge;
}
