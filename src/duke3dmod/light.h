#pragma once
#include "common.h"

// Render-side light levers, pure. camera.cpp arms them around the original displayrooms (docs/re/fog-shade-hud.md A-C).
namespace rva {
  const unsigned NOFOG_RVA           = 0x001E87EE;  // VA 0x005E87EE, uchar; 1 skips every GL_FOG toggle
  const unsigned R_USETILESHADES_RVA = 0x001B1AA4;  // VA 0x005B1AA4, int; 0 = unshaded texture variant
  const unsigned GETSHADEFACTOR_RVA  = 0x000786F0;  // VA 0x004786F0; cdecl float(int)
  const unsigned PS_VISIBILITY       = 0x268;       // player_struct +616, long; 0 = no distance fog, shade intact
  // nofog=1 freezes the previous frame's GL_FOG state; calling bglDisable sets it disabled explicitly (B).
  const unsigned BGLDISABLE_PTR_RVA  = 0x017FF4F8;  // VA 0x01bff4f8; the engine's bglDisable loader pointer
}
const unsigned GL_FOG_ENUM = 0x0B60;

inline bool light_wants(bool fullbright, bool cameraActive, bool cameraFullbright) {
  return fullbright || (cameraActive && cameraFullbright);
}

inline bool fog_substitute(bool cameraActive, bool fogSwitch) {
  return cameraActive && fogSwitch;
}

// getshadefactor's prologue becomes `fld1; ret` (colour factor 1.0). 8 bytes: `55 8B EC` alone is a common prologue.
const int GETSHADEFACTOR_COUNT = 1;
const CodePatch GETSHADEFACTOR_SITES[GETSHADEFACTOR_COUNT] = {
  { rva::GETSHADEFACTOR_RVA, 8,
    { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0x0F, 0xBF },
    { 0xD9, 0xE8, 0xC3, 0x83, 0xEC, 0x1C, 0x0F, 0xBF } },
};

// One player field, saved then zeroed, restored to the SAVED value.
struct FogBracket { unsigned char* base; unsigned psRva; long saved; bool armed; };

inline bool fog_bracket_arm(FogBracket* b, unsigned char* base, unsigned psRva) {
  if (!b || !base || b->armed) return false;
  b->base = base; b->psRva = psRva;
  b->saved = read_field_bytes(base, psRva + rva::PS_VISIBILITY, 4);
  write_field_bytes(base, psRva + rva::PS_VISIBILITY, 4, 0);
  b->armed = true;
  return true;
}
inline bool fog_bracket_release(FogBracket* b) {
  if (!b || !b->armed) return false;
  write_field_bytes(b->base, b->psRva + rva::PS_VISIBILITY, 4, b->saved);
  b->armed = false;
  return true;
}

// Arm nofog then tileshades, release in reverse; both restore what was READ.
struct LightLevers { unsigned char* base; long savedNofog; long savedTileShades; bool armed; };

inline bool light_levers_arm(LightLevers* l, unsigned char* base) {
  if (!l || !base || l->armed) return false;
  l->base = base;
  l->savedNofog      = read_field_bytes(base, rva::NOFOG_RVA, 1);
  l->savedTileShades = read_field_bytes(base, rva::R_USETILESHADES_RVA, 4);
  write_field_bytes(base, rva::NOFOG_RVA, 1, 1);
  write_field_bytes(base, rva::R_USETILESHADES_RVA, 4, 0);
  l->armed = true;
  return true;
}
inline bool light_levers_release(LightLevers* l) {
  if (!l || !l->armed) return false;
  write_field_bytes(l->base, rva::R_USETILESHADES_RVA, 4, l->savedTileShades);
  write_field_bytes(l->base, rva::NOFOG_RVA, 1, l->savedNofog);
  l->armed = false;
  return true;
}

// Session latch (nocull pattern): a failed arm marks light unavailable until restart.
inline bool& light_available_slot() { static bool avail = true; return avail; }
inline bool light_available() { return light_available_slot(); }
inline void light_mark_unavailable() { light_available_slot() = false; }
inline void light_reset_available() { light_available_slot() = true; }

// The "(Unavailable)" suffix for the Fullbright rows; the switches stay live.
inline bool light_row_unavailable() { return !light_available(); }
