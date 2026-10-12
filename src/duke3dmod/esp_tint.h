#pragma once
// Combination switch tint, pure half; esp_tint.cpp (DLL line only) pushes the in-world quads (docs/re/polymost-depth.md C).
#include <string.h>
#include <math.h>
#include "esp_switch.h"
#include "world.h"
#include "common.h"

// Only combination locks get a verdict: "Needs blue card" is a fact about keys, not an order.
enum { TINT_NONE = 0, TINT_FLIP = 1, TINT_LEAVE = 2 };

// `line` is compared, not re-derived, so tint and label agree.
inline int tint_verdict(int family, const char* line) {
  if (family != SW_COMBINATION || !line) return TINT_NONE;
  if (strcmp(line, "Flip") == 0)  return TINT_FLIP;
  if (strcmp(line, "Leave") == 0) return TINT_LEAVE;
  return TINT_NONE;
}

// 0 (TINT_NONE) is not a colour: the producer refuses the quad.
inline unsigned tint_colour(int verdict, unsigned flip, unsigned leave) {
  return verdict == TINT_FLIP ? flip : verdict == TINT_LEAVE ? leave : 0u;
}

// A wall sprite's span end, Build units; unrounded (the endpoints are off the integer lattice).
struct WorldXY { int x, y; };
struct WorldXYd { double x, y; };

// polymost_drawsprite's wall-sprite branch (docs/re/polymost-depth.md C). y uses sine at ang+1536, so the span runs
// across the facing. Unrounded as the engine: rounding tilts the plane (world.h).
inline void tint_wall_sprite_endpoints(int x, int y, int ang, int xrepeat, int tilesizx,
                                       int xoff, const short* sintable,
                                       WorldXYd* a, WorldXYd* b) {
  if (!a || !b || !sintable) return;
  const double xv = (double)xrepeat * (double)sintable[ang & 2047] / 65536.0;
  const double yv = (double)xrepeat * (double)sintable[(ang + 1536) & 2047] / 65536.0;
  const double f  = (double)(tilesizx >> 1) + (double)xoff;
  const double x0 = (double)x - xv * f, y0 = (double)y - yv * f;
  a->x = x0;
  a->y = y0;
  b->x = x0 + xv * (double)tilesizx;
  b->y = y0 + yv * (double)tilesizx;
}

// `centred` is cstat bit 7: clear, z is the bottom; set, half the height is added back (plus one yrepeat for odd
// tilesizy). Widened before every multiply: unchecked engine fields.
inline void tint_wall_sprite_zrange(int z, int yrepeat, int tilesizy, int yoff,
                                    bool centred, int* top, int* bottom) {
  if (!top || !bottom) return;
  long long zz = (long long)z - 4LL * (long long)yrepeat * (long long)yoff;
  if (centred) {
    zz += 2LL * (long long)tilesizy * (long long)yrepeat;
    if (tilesizy & 1) zz += 2LL * (long long)yrepeat;
  }
  *bottom = clamp_ll_to_int(zz);
  *top    = clamp_ll_to_int(zz - 4LL * (long long)yrepeat * (long long)tilesizy);
}

// Clamp to the sector's flat ceilingz/floorz (not slope height), each side refused on its own parallax bit.
inline void tint_clamp_to_sector(int ceilingz, bool ceilingParallax,
                                 int floorz, bool floorParallax,
                                 int* top, int* bottom) {
  if (!top || !bottom) return;
  if (!ceilingParallax && *top < ceilingz)  *top = ceilingz;
  if (!floorParallax   && *bottom > floorz) *bottom = floorz;
}

// esp_tint.cpp's half, for world.cpp's producer slot.
void esp_tint_collect();
int  esp_tint_last_count();
// Same total by shape: face sprite, wall sprite, wall texture quads. Any pointer may be null.
void esp_tint_kind_counts(int* face, int* wall, int* tex);
