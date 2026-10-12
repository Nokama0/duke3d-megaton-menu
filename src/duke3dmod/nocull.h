#pragma once
#include "common.h"

// "Disable culling": drawrooms seeds only the player's sector into `gotsector`, so a floating camera leaves the rest
// undrawn; the fix scans every unmarked sector (opt-in: O(bunches^2) frame time). RVAs: docs/re/polymost-depth.md G.

// Needs cfgOn, the render-clock cameraActive, bracketArmed (cam.active can be true while the bracket refuses to arm) and not
// inMirror (a mirror runs drawrooms twice).
inline bool nocull_should_scan(bool cfgOn, bool cameraActive, bool bracketArmed, bool inMirror) {
  return cfgOn && cameraActive && bracketArmed && !inMirror;
}

// Engine-array budget: the sweep removes the natural bound on numscans/numbunches (6144, rva::MAXWALLSB), so it spends at
// most half the free room, keeps NOCULL_CALL_ALLOWANCE per sector for scansector's BFS, and stops above MAXWALLSB -
// NOCULL_FLOOR_MARGIN. Negative inputs refuse.
const int NOCULL_CALL_ALLOWANCE = 256;
const int NOCULL_FLOOR_MARGIN   = 1024;

inline bool nocull_budget_allows(int numscansEntry, int numbunchesEntry,
                                 int numscansNow, int numbunchesNow,
                                 int wallnum) {
  if (numscansEntry < 0 || numbunchesEntry < 0 ||
      numscansNow   < 0 || numbunchesNow   < 0 || wallnum < 0) return false;

  const int floorAt = rva::MAXWALLSB - NOCULL_FLOOR_MARGIN;
  if (numscansNow > floorAt || numbunchesNow > floorAt) return false;

  const int entryMax = numscansEntry > numbunchesEntry ? numscansEntry : numbunchesEntry;
  const int free = rva::MAXWALLSB - entryMax;
  const int budget = free / 2;

  const int nowMax = numscansNow > numbunchesNow ? numscansNow : numbunchesNow;
  const int usedSinceEntry = nowMax - entryMax;

  if (usedSinceEntry + wallnum + NOCULL_CALL_ALLOWANCE > budget) return false;
  return true;
}

// Next sector after `from` whose bit in `gotsector` is clear, or -1. Bit `s & 7` of byte `s >> 3`. `numsectors <= 0` refuses
// rather than loop on an untrusted count.
inline int nocull_next_unscanned(const unsigned char* gotsector, int numsectors, int from) {
  if (!gotsector || numsectors <= 0) return -1;
  const int start = from < 0 ? 0 : from + 1;
  for (int s = start; s < numsectors; s++) {
    if ((gotsector[s >> 3] & (unsigned char)(1 << (s & 7))) == 0) return s;
  }
  return -1;
}

inline bool& nocull_available_slot() { static bool avail = true; return avail; }
inline bool nocull_available() { return nocull_available_slot(); }
inline void nocull_mark_unavailable() { nocull_available_slot() = false; }

inline void nocull_reset_available() { nocull_available_slot() = true; }

inline bool nocull_row_unavailable() { return !nocull_available(); }


// Redirects polymost_drawrooms' seed call to polymost_scansector. Never fatal: a failed install marks the option unavailable.
bool nocull_install();
