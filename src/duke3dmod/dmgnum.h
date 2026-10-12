#pragma once
#include "canvas.h"
#include <math.h>

// Floating damage numbers: every rule. dmgnum.cpp hooks `ifhitbyweapon` and draws through the ESP projection (DLL only). The
// clock is an unsigned ms counter (wallclock.h) that WRAPS: ages are `now - t0`, exact for any age under 2^32 ms.

const int      DMGNUM_MAX     = 32;
const unsigned DMGNUM_LIFE_MS = 750;

struct DmgEvent { int x, y, z; int damage; bool kill; int victim; unsigned t0; bool live; };
struct DmgRing  { DmgEvent e[DMGNUM_MAX]; int next; };

inline void dmgnum_reset(DmgRing* r) {
  if (!r) return;
  for (int i = 0; i < DMGNUM_MAX; i++) { r->e[i] = DmgEvent(); r->e[i].live = false; }
  r->next = 0;
}
inline void dmgnum_push(DmgRing* r, const DmgEvent& ev) {
  if (!r) return;
  r->e[r->next] = ev; r->e[r->next].live = true;
  r->next = (r->next + 1) % DMGNUM_MAX;
}
// Owner must be the local player's sprite, the victim a live enemy, the damage positive: ifhitbyweapon reports -1 for "no
// hit" and 0 for a spark that did nothing.
inline bool dmgnum_event_wanted(int owner, int localPlayerSprite, bool victimIsLiveEnemy, int damage) {
  return owner >= 0 && owner == localPlayerSprite && victimIsLiveEnemy && damage > 0;
}
inline bool dmgnum_is_live(const DmgEvent& ev, unsigned now) {
  return ev.live && (now - ev.t0) < DMGNUM_LIFE_MS;
}
inline double dmgnum_progress(unsigned t0, unsigned now) {
  const unsigned age = now - t0;
  if (age >= DMGNUM_LIFE_MS) return 1.0;
  return (double)age / (double)DMGNUM_LIFE_MS;
}
inline int dmgnum_rise_px(double p, int textH) {
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;
  const double q = 1.0 - (1.0 - p) * (1.0 - p);
  return (int)floor(q * textH + 0.5);
}
inline int dmgnum_alpha(double p) {
  if (p <= 2.0 / 3.0) return 255;
  if (p >= 1.0) return 0;
  return (int)floor(255.0 * (1.0 - p) * 3.0 + 0.5);
}
inline unsigned dmgnum_colour(bool kill, unsigned hit, unsigned killColour) { return kill ? killColour : hit; }
inline int dmgnum_stack_index(const DmgRing& r, int idx, unsigned now) {
  if (idx < 0 || idx >= DMGNUM_MAX || !dmgnum_is_live(r.e[idx], now)) return 0;
  int n = 0;
  for (int k = 0; k < DMGNUM_MAX; k++) {
    const int s = (r.next + k) % DMGNUM_MAX;
    if (s == idx) break;
    if (dmgnum_is_live(r.e[s], now) && r.e[s].victim == r.e[idx].victim) n++;
  }
  return n;
}

// dmgnum.cpp's entry points. Never linked into test_host.
bool dmgnum_install();                    // the ifhitbyweapon thunk hook; feeds engine_mark_hook_result once
void dmgnum_draw(int screenW, int screenH);   // the render-clock pass, called from overlay_frame after esp_draw

// Zeroes the hit instrument's session budget (`s_hitLines`); called only from actorstate.cpp on a level change, kept here so
// the counter stays this file's static.
void dmgnum_hit_budget_reset();
