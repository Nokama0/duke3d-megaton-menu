#pragma once

// Enemy names and per-type maximum health for the ESP health bar. Pure.

const char* esp_enemy_name(int picnum);

// Which expansion is loaded (rva::ADDON): they keep base picnums but put different creatures on some.
enum { ESP_ADDON_BASE = 0, ESP_ADDON_DC = 1, ESP_ADDON_NW = 2, ESP_ADDON_CARIBBEAN = 3 };

const char* esp_enemy_name_for(int picnum, int addon);

int esp_enemy_name_count();
int esp_enemy_name_picnum(int i);

int esp_addon_name_count();
int esp_addon_name_addon(int i);
int esp_addon_name_picnum(int i);

const char* esp_item_name(int picnum);

int esp_item_name_count();
int esp_item_name_picnum(int i);

// Access card named by `pal`: checkhitswitch (VA 0x0050ec7c-0x0050ed6d) compares a lock's pal to 0/21/23; spawn() case
// 0x3c (VA 0x004c1e00) skips the pal reset, so the card keeps it.
const int ESP_PIC_ACCESSCARD = 60;
const int ESP_PAL_KEY_BLUE   = 0;
const int ESP_PAL_KEY_RED    = 21;
const int ESP_PAL_KEY_YELLOW = 23;

const char* esp_item_name_pal(int picnum, int pal);

// A pal-21 trooper gets `addstrength TROOPSTRENGTH` on its first tic (GAME.CON:4709), 30 HP to 60, then `cactor
// LIZTROOP` rewrites picnum to 1680 keeping the boosted `extra`.

const int ESP_PIC_LIZTROOP    = 1680;
const int ESP_TROOP_BOOST_PAL = 21;

// BOSS1: GAME.CON:6465 gives a coloured Battlelord a compiled-operand strength with no nameable address (unreadable).
const int ESP_PIC_BOSS1 = 2630;
// USER.CON: BOSS1STRENGTH 4500, BOSS1PALSTRENGTH 1000; applied only at the stock spawn strength 4500.
const int ESP_BOSS1_STRENGTH     = 4500;
const int ESP_BOSS1_PAL_STRENGTH = 1000;
// Both pal overrides sit in the CON's `ifai 0` block (GAME.CON 4710, 6464), which fires on the first running tic; until
// then the ruled maximum is the truer number.
inline int esp_display_hp(int hp, int ruledMax, bool hasRun) {
  return (!hasRun && ruledMax > 0) ? ruledMax : hp;
}

bool esp_is_trooper(int picnum);

// CON overrides applied to a spawn strength (base = actorscrptr[picnum][0]; <= 0 when unreadable). 0: no truthful maximum.
int esp_rule_max_health(int picnum, int pal, int base, int troopBase);

// Last resort: highest health seen per (picnum, pal). Past capacity a new pair reports 0 (an empty bar).
int esp_track_max_health(int picnum, int pal, int current);

// esp.cpp's entry: the rule first, the learned ceiling only without an answer; never below `current`.
int esp_max_health(int picnum, int pal, int current, int base, int troopBase);

int esp_max_health_overflow();

void esp_max_health_reset();

const int ESP_MAX_HEALTH_TYPES = 64;

// Explodables ESP: crack sprites and breakable walls; esp_crack_visible (esp_world.h) is the separate hitag gate.
const char* esp_explodable_name(int picnum);
int esp_explodable_name_count();
int esp_explodable_name_picnum(int i);

// One of the nineteen panels checkhitwall breaks (docs/re/switches-breakables.md C). Excludes barrels and glass walls.
const char* esp_breakable_name(int picnum);
int esp_breakable_name_count();
int esp_breakable_name_picnum(int i);
