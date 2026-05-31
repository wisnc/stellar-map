#pragma once
#include <cstdint>

struct GnssSat {
  uint16_t prn;
  uint8_t  sys;
  int16_t  az;
  int8_t   el;
  uint8_t  snr;
};

void gnss_begin();
void gnss_poll();
bool gnss_present();
bool gnss_time_valid();
bool gnss_pos_valid();
bool gnss_fix();
int  gnss_sats_used();
int  gnss_count_display();
bool gnss_get_datetime(int& y, int& mo, int& d, int& h, int& mi, int& s);
bool gnss_get_pos(float& lat, float& lon);
int  gnss_sats(const GnssSat** out);
