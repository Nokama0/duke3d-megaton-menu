#pragma once
// Camera debug readout text (esp.cpp), for the clipboard shortcut.
const char* esp_debug_readout_text();
#include "esp_switch.h"

// One shared switch-sprite walk so esp_draw's markers and the esp_tint.cpp tint agree on what a switch is; run
// sequentially with other list walks (engine.cpp's single-slot cycle counter).
struct EspSwitchSprite {
  int i;
  int pic, cstat;
  SwitchInfo si;
};
typedef void (*EspSwitchSpriteFn)(const EspSwitchSprite& s, void* ctx);

// `gotAccess` is the caller's once-per-frame key-bit sample.
void esp_switch_sprite_walk(int gotAccess, EspSwitchSpriteFn fn, void* ctx);

void esp_draw(int screenW, int screenH);

// True when any ESP category is on (overlay.cpp's GL-bracket decision, esp_layout.h).
bool esp_any_on();
