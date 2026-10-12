#pragma once
#include "firerate.h"

// Weapon switch speed. A change sets weapon_pos to -1 (lowers to -8); the engine flips -9 to 10 and finishes 1 at 0.
// Advance only past frames that merely count; the flip and the rest value belong to the engine.
inline int wswitch_advance(int pos, int steps) {
  if (steps <= 0 || pos == 0 || pos == -9) return pos;
  if (pos < 0) {
    const int target = pos - steps;
    return target < -8 ? -8 : target;
  }
  const int target = pos - steps;
  return target < 1 ? 1 : target;
}

// The one player-state write needs: verified engine, LOCAL player, no freeze/free camera, rate above identity.
inline bool wswitch_gate(bool verified, bool localPlayer, bool freezeActive, bool cameraActive, int ratePct, int pos) {
  return verified && localPlayer && !freezeActive && !cameraActive
      && firerate_rate_clamped(ratePct) > 100 && pos != 0 && pos != -9;
}

// Reason word for the debug line's switch= field; same order as wswitch_gate.
inline const char* wswitch_gate_reason(bool verified, bool localPlayer, bool freezeActive, bool cameraActive, int ratePct, int pos) {
  if (!verified)  return "unverified";
  if (!localPlayer) return "remote";
  if (freezeActive) return "frozen";
  if (cameraActive) return "camera";
  if (firerate_rate_clamped(ratePct) <= 100) return "off";
  if (pos == 0)  return "rest";
  if (pos == -9) return "holster";
  return "on";
}

// DLL only: writes the advanced weapon_pos back from hooked_processinput's tail when the gate allows.
void wswitch_tic(bool freezeActive);
