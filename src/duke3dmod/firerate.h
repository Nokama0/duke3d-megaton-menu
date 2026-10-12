#pragma once
#include <math.h>

// Fire rate as pure rules. The engine advances a per-weapon counter (player_struct.kickback_pic) once per tic and most steps
// only count; skipping those, never a real event, shortens the cadence without losing a shot, sound, shell or reset.

const int FIRERATE_MIN = 100, FIRERATE_MAX = 400, FIRERATE_STEP = 50;
const int FIRERATE_DEFAULT = FIRERATE_MIN;
const int FIRERATE_EVENTS_MAX = 8;
struct FireRateWeapon { int weapon; int nEvents; int events[FIRERATE_EVENTS_MAX]; };

// Pipebomb, tripbomb and hand remote have no entry (not cooldown counters). Function-local table: a file-scope array would
// give each of five including units its own copy.
inline const FireRateWeapon* firerate_weapon(int weapon) {
  static const FireRateWeapon FIRERATE_TABLE[9] = {
    { 0,  2, { 6, 13 } },
    { 1,  5, { 1, 2, 4, 7, 26 } },
    { 2,  7, { 3, 12, 14, 16, 19, 23, 30 } },
    { 3,  5, { 2, 5, 8, 11, 12 } },
    { 4,  2, { 3, 19 } },
    { 6,  1, { 11 } },
    { 7,  3, { 2, 4, 5 } },
    { 9,  3, { 2, 3, 4 } },
    { 11, 1, { 4 } },
  };
  for (int i = 0; i < 9; i++) if (FIRERATE_TABLE[i].weapon == weapon) return &FIRERATE_TABLE[i];
  return 0;
}

inline int firerate_rate_clamped(int v) {
  if (v < FIRERATE_MIN) v = FIRERATE_MIN;
  if (v > FIRERATE_MAX) v = FIRERATE_MAX;
  return ((v - FIRERATE_MIN + FIRERATE_STEP / 2) / FIRERATE_STEP) * FIRERATE_STEP + FIRERATE_MIN;
}

inline int firerate_next_event(const FireRateWeapon& t, int k) {
  for (int i = 0; i < t.nEvents; i++) if (t.events[i] >= k) return t.events[i];
  return -1;
}

// Forward by up to `steps`, never past the next event, never from rest: k <= 0 is not firing, advancing would fire.
inline int firerate_advance(const FireRateWeapon& t, int k, int steps) {
  if (k <= 0 || steps <= 0) return k;
  const int nxt = firerate_next_event(t, k);
  if (nxt < 0) return k;
  const int target = k + steps;
  return target > nxt ? nxt : target;
}

// Extra steps this tic for a rate in percent, with a carry in [0, 1): 150 is one extra every other tic. Caller owns the carry.
inline int firerate_steps(int ratePct, double* carry) {
  const double extra = (firerate_rate_clamped(ratePct) - 100) / 100.0;
  if (!carry) return (int)floor(extra);
  *carry += extra;
  const int n = (int)floor(*carry);
  *carry -= n;
  if (*carry < 0.0) *carry = 0.0;
  return n;
}

inline bool firerate_gate(bool verified, bool localPlayer, bool freezeActive, bool cameraActive,
                          int ratePct, int kickback, bool hasTable) {
  return verified && localPlayer && !freezeActive && !cameraActive
      && firerate_rate_clamped(ratePct) > 100 && kickback > 0 && hasTable;
}

// One-word reason for the throttled `firerate:` diagnostic; "on" iff the gate returns true.
inline const char* firerate_gate_reason(bool verified, bool localPlayer, bool freezeActive, bool cameraActive,
                                        int ratePct, int kickback, bool hasTable) {
  if (!verified || !localPlayer) return "unverified";
  if (freezeActive)               return "frozen";
  if (cameraActive)               return "camera";
  if (firerate_rate_clamped(ratePct) <= 100) return "off";
  if (kickback <= 0)              return "rest";
  if (!hasTable)                  return "notable";
  return "on";
}

inline double firerate_carry_for_weapon(double carry, int lastWeapon, int weapon) {
  return weapon != lastWeapon ? 0.0 : carry;
}

// firerate.cpp (DLL only), tic clock: reads curr_weapon and kickback_pic and, when the gate allows, writes the advanced
// counter. hooked_processinput gates on localPlayer first.
void firerate_tic(bool freezeActive);
