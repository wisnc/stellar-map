#pragma once
#include <M5Cardputer.h>

struct SearchPick { bool valid; uint8_t kind; uint16_t ref; };

void       search_open();
void       search_close();
bool       search_active();
SearchPick search_key(const Keyboard_Class::KeysState& ks);
void       search_draw(M5Canvas& g);
