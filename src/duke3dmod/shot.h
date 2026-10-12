#pragma once
#include "common.h"

// Screenshot mode. Pure rules; shot.cpp owns the hooks. Facts: docs/re/fog-shade-hud.md, section D.
namespace rva {
  const unsigned THUNK_DISPLAYWEAPON_RVA = 0x000035B7;
  const unsigned char THUNK_DISPLAYWEAPON_BYTES[5] = { 0xE9, 0xC4, 0x3B, 0x0F, 0x00 };
  const unsigned DISPLAYWEAPON_BODY_RVA  = 0x000F7180;
  const unsigned THUNK_DISPLAYMASKS_RVA  = 0x00001852;
  const unsigned char THUNK_DISPLAYMASKS_BYTES[5]  = { 0xE9, 0x59, 0x52, 0x0F, 0x00 };
  const unsigned DISPLAYMASKS_BODY_RVA   = 0x000F6AB0;
  const unsigned THUNK_DISPLAYREST_RVA   = 0x000036BB;
  const unsigned char THUNK_DISPLAYREST_BYTES[5]   = { 0xE9, 0x80, 0xB2, 0x0B, 0x00 };
  const unsigned DISPLAYREST_BODY_RVA    = 0x000BE940;
  const unsigned THUNK_VSCRN_RVA         = 0x000032A1;   // called, never hooked
  const unsigned char THUNK_VSCRN_BYTES[5]         = { 0xE9, 0x8A, 0x32, 0x10, 0x00 };
  const unsigned VSCRN_BODY_RVA          = 0x00106530;
  const unsigned UD_SCREEN_SIZE = 1508;  // user_defs, long; 0 = full view, no status bar
  const unsigned UD_CROSSHAIR   = 1516;  // user_defs, long
  const unsigned PS_GM          = 844;   // player_struct, char; bit 0 = the game's menu is open
}

inline bool shot_active(bool cameraActive, bool shotSwitch, bool menuOpen) {
  return cameraActive && shotSwitch && !menuOpen;
}
enum ShotEdge { SHOT_EDGE_NONE = 0, SHOT_EDGE_ON = 1, SHOT_EDGE_OFF = 2 };
inline int shot_edge(bool wasActive, bool nowActive) {
  if (wasActive == nowActive) return SHOT_EDGE_NONE;
  return nowActive ? SHOT_EDGE_ON : SHOT_EDGE_OFF;
}
// Saved size is written on the ON edge only and restored on the OFF edge only; OFF with nothing held is a no-op.
struct ShotView { bool held; long savedSize; };
enum ShotAction { SHOT_ACT_NONE = 0, SHOT_ACT_WRITE_ZERO_AND_VSCRN = 1, SHOT_ACT_RESTORE_AND_VSCRN = 2 };
inline int shot_view_step(ShotView* v, int edge, long currentSize, long* writeOut) {
  if (!v || !writeOut) return SHOT_ACT_NONE;
  if (edge == SHOT_EDGE_ON && !v->held) {
    v->held = true; v->savedSize = currentSize; *writeOut = 0;
    return SHOT_ACT_WRITE_ZERO_AND_VSCRN;
  }
  if (edge == SHOT_EDGE_OFF && v->held) {
    v->held = false; *writeOut = v->savedSize;
    return SHOT_ACT_RESTORE_AND_VSCRN;
  }
  return SHOT_ACT_NONE;
}
inline bool shot_hides_overlays(bool active) { return active; }
inline bool shot_call_original(bool active) { return !active; }
// Gates the blind `ud` reads in hooked_displayrest: a verified thunk does not verify `ud`'s own address.
inline bool shot_frame_reads_allowed(bool verified) { return verified; }

// DLL only: dllmain.cpp installs, overlay.cpp asks if this frame hides markers.
bool shot_install();      // three thunk hooks + the vscrn fingerprint; feeds engine_mark_hook_result once per hook
bool shot_active_now();   // the value hooked_displayrest computed this frame
