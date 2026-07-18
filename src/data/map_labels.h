#pragma once
#include <cstdint>

constexpr int MAP_LABEL_COUNT = 13;

const uint16_t map_label_idx[MAP_LABEL_COUNT > 0 ? MAP_LABEL_COUNT : 1] = {
  386,4868,5417,5418,5939,5958,6555,7071,7321,8970,8976,8984,8996,
};

const char* const map_label_txt[MAP_LABEL_COUNT > 0 ? MAP_LABEL_COUNT : 1] = {
  "",
  "",
  "",
  "Alpha Centauri",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
};
