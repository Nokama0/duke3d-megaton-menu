#pragma once
#include "common.h"

// Perfect accuracy: `shoot` subtracts a krand-masked spread at six 5-byte `E8 rel32` sites (docs/re/freecam-accuracy.md
// C); a same-length `B8 <centre> 00 00 00` makes one 0 and PatchBracket restores it. The pistol's sites sit inside
// `aim() == -1`. ONE PAIR PER SHOT, NEVER A UNION: chaingun and shotgun share ACC_HITSCAN; ORing lists a site twice.
enum AccPair {
  ACC_PAIR_NONE     = -1,
  ACC_PAIR_PISTOL   = 0,
  ACC_PAIR_HITSCAN  = 1,
  ACC_PAIR_EXPANDER = 2,
  ACC_PAIR_COUNT    = 3
};

const int ACC_PAIR_SITES = 2;

// The pair this shot needs, from the `atwith` the engine passed `shoot` and the four toggles (docs/re/targeting.md D):
// pistol 0xa23, chaingun 0x9e8, shotgun 0xa35, expander 0x990. Everything else (enemy non-hitscan, checkhitsprite blood
// hitscans 0x8f8..0x8fb, spreadless weapons) is NONE.
inline int accuracy_pair_for(short atwith, bool pistol, bool chaingun,
                             bool shotgun, bool expander) {
  switch (atwith) {
    case 0x0a23: return pistol   ? ACC_PAIR_PISTOL   : ACC_PAIR_NONE;
    case 0x09e8: return chaingun ? ACC_PAIR_HITSCAN  : ACC_PAIR_NONE;
    case 0x0a35: return shotgun  ? ACC_PAIR_HITSCAN  : ACC_PAIR_NONE;
    case 0x0990: return expander ? ACC_PAIR_EXPANDER : ACC_PAIR_NONE;
    default:     return ACC_PAIR_NONE;
  }
}

inline const rva::AccSite* accuracy_pair_sites(int pair) {
  switch (pair) {
    case ACC_PAIR_PISTOL:   return rva::ACC_PISTOL;
    case ACC_PAIR_HITSCAN:  return rva::ACC_HITSCAN;
    case ACC_PAIR_EXPANDER: return rva::ACC_EXPANDER;
    default:                return 0;
  }
}

inline int accuracy_sites_for(short atwith, bool pistol, bool chaingun, bool shotgun,
                              bool expander, const rva::AccSite** outSites) {
  if (outSites) *outSites = 0;
  const rva::AccSite* sites =
      accuracy_pair_sites(accuracy_pair_for(atwith, pistol, chaingun, shotgun, expander));
  if (!sites) return 0;
  if (outSites) *outSites = sites;
  return ACC_PAIR_SITES;
}

// Per-pair session latch: patch_bracket_arm refuses the whole bracket on one differing byte, keeping hardcoded addresses
// safe on an unrecognised binary. A refusal will not change on retry, so it LATCHES THAT PAIR OFF and logs once.
inline bool* accuracy_latch_slots() {
  static bool slots[ACC_PAIR_COUNT] = { false, false, false };
  return slots;
}

inline bool accuracy_pair_latched(int pair) {
  if (pair < 0 || pair >= ACC_PAIR_COUNT) return true;
  return accuracy_latch_slots()[pair];
}

inline void accuracy_latch_pair(int pair) {
  if (pair < 0 || pair >= ACC_PAIR_COUNT) return;
  accuracy_latch_slots()[pair] = true;
}

inline void accuracy_latch_reset_all() {
  for (int i = 0; i < ACC_PAIR_COUNT; i++) accuracy_latch_slots()[i] = false;
}

// Whether this shot may arm: the LOCAL PLAYER'S own, a real pair, not latched. `isPlayerShot` is needed because an
// enemy hitscan would arm the patch and a transient VirtualProtect refusal would latch a weapon off for the session.
inline bool accuracy_should_arm(int pair, bool isPlayerShot) {
  return isPlayerShot && pair != ACC_PAIR_NONE && !accuracy_pair_latched(pair);
}


inline bool accuracy_note_arm_result(int pair, bool armed) {
  if (!armed) accuracy_latch_pair(pair);
  return armed;
}

// A latched pair must not read as live in the panel: config.cpp never writes the latch back to the switch (a saved
// `accpistol=1` must survive onto a binary where the sites are fine), so the strip and "(Unavailable)" need `value &&
// !latched`.
inline bool accuracy_row_live(int value, bool latched) {
  return value != 0 && !latched;
}
inline bool accuracy_row_unavailable(bool latched) { return latched; }

// Whether `hooked_shoot` may ATTEMPT to arm. `shoot` re-enters itself (checkhitsprite, atwith 0x8f8..0x8fb); a nested arm
// would find the outer `B8` instead of the fingerprint's `E8`, refuse and FALSELY LATCH a pair.

inline bool accuracy_may_attempt_arm(bool alreadyArmed) {
  return !alreadyArmed;
}
