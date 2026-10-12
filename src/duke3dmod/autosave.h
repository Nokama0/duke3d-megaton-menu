#pragma once
#include "common.h"

// Autosave as a pure rule. autosave.cpp holds only the engine reads, the one call into the game's dnSaveGame and the log
// lines; every decision about WHEN a save is due is here.

const int AUTOSAVE_MINS_MIN = 0, AUTOSAVE_MINS_MAX = 30;
const int AUTOSAVE_MINS_DEFAULT = AUTOSAVE_MINS_MIN;
const int AUTOSAVE_SLOT_MIN = 0, AUTOSAVE_SLOT_MAX = 9;
const int AUTOSAVE_SLOT_DEFAULT = AUTOSAVE_SLOT_MAX;
const unsigned AUTOSAVE_MS_PER_MIN = 60000u;

inline int autosave_mins_clamped(int v) { return clamp_int(v, AUTOSAVE_MINS_MIN, AUTOSAVE_MINS_MAX); }
inline int autosave_slot_clamped(int v) { return clamp_int(v, AUTOSAVE_SLOT_MIN, AUTOSAVE_SLOT_MAX); }

// int flags only, never bool: MSVC 19.50 at /O2 loses a bool member of a struct an inline function returns.
struct AutosaveState { unsigned lastMs; unsigned levelSig; int armed; };

// Returns 1 when a save is due now; updates *s. intervalMins 0 = never. `sig` is the map signature (trigger_map_signature);
// `inPlay` is gm == 4 and none of paused / game menu / this panel / free camera / dead.
inline int autosave_step(AutosaveState* s, int intervalMins, unsigned nowMs, unsigned sig, int inPlay) {
  if (!s) return 0;
  // A new map (and the first call) restarts the timer and never saves on it: a save at level start would duplicate it.
  if (!s->armed || sig != s->levelSig) {
    s->armed = 1;
    s->levelSig = sig;
    s->lastMs = nowMs;
    return 0;
  }
  const int mins = autosave_mins_clamped(intervalMins);
  if (mins <= 0) { s->lastMs = nowMs; return 0; }
  if (!inPlay) return 0;
  if ((unsigned)(nowMs - s->lastMs) < (unsigned)mins * AUTOSAVE_MS_PER_MIN) return 0;
  s->lastMs = nowMs;
  return 1;
}

void autosave_frame();
