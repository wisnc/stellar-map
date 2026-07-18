#pragma once
#include <M5Cardputer.h>

struct SiteConfig {
  float lat;
  float lon;
  int   utc_offset_min;
  int   last_date;
  float const_label_fov;
  float messier_fov;
};

bool storage_begin();
bool storage_ok();
void storage_load_config(SiteConfig& c);
bool storage_save_config(const SiteConfig& c);
bool storage_screenshot(M5Canvas& canvas, const char* path);
