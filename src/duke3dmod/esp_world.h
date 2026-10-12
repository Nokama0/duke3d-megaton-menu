#pragma once
#include "common.h"   // clamp_ll_to_int

// Explodables: crack sprites and breakable walls. esp.cpp reads live map memory and cannot link into test_host, so every
// decision is pure and header-only.

// Build's STANDABLE status list, where every crack sprite lives (docs/re/switches-breakables.md B).
const int ESP_STAT_STANDABLE = 6;

// A crack detonates only with a nonzero hitag (it links the crack to barrels and effectors); hitag 0 is inert. The game
// deletes the sprite on detonation, so presence on statnum 6 is liveness.
inline bool esp_crack_visible(int hitag) { return hitag > 0; }

inline int esp_wall_mid(int a, int b) {
  return clamp_ll_to_int(((long long)a + (long long)b) / 2);
}

// Halfway between floor and ceiling at the midpoint via getflorzofslope/getceilzofslope, not sector[].floorz, so a sloped
// room puts the marker on the panel.
inline int esp_wall_mid_z(int florz, int ceilz) {
  return clamp_ll_to_int(((long long)florz + (long long)ceilz) / 2);
}

const int ESP_WALL_MAX = 128;

enum { ESP_WKIND_BREAKABLE = 0, ESP_WKIND_SWITCH = 1 };

struct EspWallMark {
  int kind;                        // ESP_WKIND_*
  int wallnum;
  int x, y, z;                     // the marker's world point
  int picnum, pal, hitag, lotag;   // ESP_WKIND_SWITCH only; 0 otherwise
  // Last so offsets hold. The combination tint draws a switch as the whole wall face from this sector's slope functions,
  // and nextsector is the far side, so the scan's loop variable is the only source.
  int sectnum;
};

struct EspWallSnapshot {
  unsigned seq;
  int count;
  int overflow;
  EspWallMark m[ESP_WALL_MAX];
};

// The scan is the shared producer for both mark kinds, so "Switches and locks" alone must not go without it. Off means no
// walk and an EMPTY snapshot, so re-enabling never starts stale. espexplodables gates only the crack-sprite walk.
inline bool esp_world_should_scan(int espbreakables, int espswitches) {
  return espbreakables != 0 || espswitches != 0;
}

inline bool esp_wall_mark_wanted(int kind, int espbreakables, int espswitches) {
  return kind == ESP_WKIND_BREAKABLE ? espbreakables != 0 : espswitches != 0;
}

inline int esp_wall_count_clamped(int n) {
  return clamp_int(n, 0, ESP_WALL_MAX);
}

void esp_world_tic();

bool esp_wall_snapshot(EspWallSnapshot* out);

// Secret-exit scan, tic clock, once per level (a numwalls change): whether there is a secret exit and, when that sector has
// no NUKEBUTTON, the touch-exit marker's world point. Off clears both.
void esp_secret_exit_tic();

bool esp_level_has_secret_exit();
