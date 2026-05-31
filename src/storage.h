#pragma once
#include <M5Cardputer.h>

struct SiteConfig { float lat; float lon; };

bool storage_begin();
bool storage_ok();
void storage_load_config(SiteConfig& c);
bool storage_save_pos(float lat, float lon);
bool storage_screenshot(M5Canvas& canvas, const char* path);
