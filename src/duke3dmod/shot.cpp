#include "shot.h"
#include "hooks.h"
#include "engine.h"
#include "freecam.h"   // camera_active
#include "config.h"
#include "log.h"
#include "world.h"     // world_draw_frame

// Screenshot mode's DLL half: three thunk hooks and one call the game makes itself. Decides nothing; every rule lives in shot.h.

// Review-only: hooked_displayrest decides the frame BEFORE the original runs (displayweapon/displaymasks read this frame's
// value); the crosshair write and restore pair around that call; a missing vscrn fingerprint keeps the mode off rather than
// leave the screen size written with no way back.

typedef void (__cdecl* DisplayWeaponFn)(short);
typedef void (__cdecl* DisplayMasksFn)(short);
typedef void (__cdecl* DisplayRestFn)(long);
typedef void (__cdecl* VscrnFn)(void);

static DisplayWeaponFn s_origWeapon = 0;
static DisplayMasksFn  s_origMasks = 0;
static DisplayRestFn   s_origRest = 0;
static VscrnFn         s_vscrn = 0;        // set only when the thunk's five bytes matched
static bool s_active = false;              // this frame's decision
static ShotView s_view = { false, 0 };

bool shot_active_now() { return s_active; }

static void __cdecl hooked_displayweapon(short snum) {
  if (shot_call_original(s_active) && s_origWeapon) s_origWeapon(snum);
}
static void __cdecl hooked_displaymasks(short snum) {
  // Drawn AFTER the original and only on frames it ran: depth holds the scene, and the HUD weapon is not yet drawn so it
  // still draws over the panels. Screenshot mode skips both: it exists to take overlays off the shot.
  if (shot_call_original(s_active) && s_origMasks) {
    s_origMasks(snum);
    world_draw_frame();
  } else {
    world_skip_frame();
  }
}

static void __cdecl hooked_displayrest(long smoothratio) {
  const bool verified = engine_verified();
  bool menuOpen = false;
  unsigned psRva = 0;
  const bool havePs = verified && camera_ps_rva(engine_screenpeek(), &psRva);
  if (havePs) menuOpen = (read_field_bytes(g_base, psRva + rva::PS_GM, 1) & 1) != 0;
  const bool now = verified && s_vscrn != 0 &&
                   shot_active(camera_active(), g_cfg.freecamshot != 0, menuOpen);
  const int edge = shot_edge(s_active, now);
  s_active = now;

  // Both `ud` reads are blind hardcoded-RVA reads, gated by shot_frame_reads_allowed (the engine.h rule). The OFF-edge restore
  // stays reachable when !readsAllowed on purpose: it writes back v.savedSize, captured on a verified ON-edge.
  const bool readsAllowed = shot_frame_reads_allowed(verified);
  long write = 0;
  const long size = readsAllowed ? read_field_bytes(g_base, rva::UD + rva::UD_SCREEN_SIZE, 4) : 0;
  const int act = shot_view_step(&s_view, edge, size, &write);
  if (act != SHOT_ACT_NONE) {
    write_field_bytes(g_base, rva::UD + rva::UD_SCREEN_SIZE, 4, write);
    s_vscrn();
    LOGI(act == SHOT_ACT_WRITE_ZERO_AND_VSCRN ? "shot: on, screen_size saved %d" : "shot: off, screen_size restored %d",
         act == SHOT_ACT_WRITE_ZERO_AND_VSCRN ? (int)size : (int)write);
  }

  const long crosshair = readsAllowed ? read_field_bytes(g_base, rva::UD + rva::UD_CROSSHAIR, 4) : 0;
  if (readsAllowed && s_active) write_field_bytes(g_base, rva::UD + rva::UD_CROSSHAIR, 4, 0);
  if (s_origRest) s_origRest(smoothratio);
  if (readsAllowed && s_active) write_field_bytes(g_base, rva::UD + rva::UD_CROSSHAIR, 4, crosshair);
}

// rva::*_BODY_RVA is pinned in the suite by decoding each thunk's rel32, so "expect" cannot drift from what the hook recovers.
static void log_recovered(const char* name, void* origTarget, unsigned bodyRva) {
  const unsigned origRva = (unsigned)((unsigned char*)origTarget - g_base);
  LOGI("shot: %s thunk hook installed; recovered original target rva=%08X (expect %08X)%s",
       name, origRva, bodyRva, origRva == bodyRva ? "" : " -- MISMATCH, this may not be the right choke point");
}

bool shot_install() {
  void* t = 0;
  const bool okW = hook_jmp_thunk(rva::THUNK_DISPLAYWEAPON_RVA, rva::THUNK_DISPLAYWEAPON_BYTES, (void*)&hooked_displayweapon, &t);
  engine_mark_hook_result(okW);
  if (okW) { s_origWeapon = (DisplayWeaponFn)t; log_recovered("displayweapon", t, rva::DISPLAYWEAPON_BODY_RVA); }
  t = 0;
  const bool okM = hook_jmp_thunk(rva::THUNK_DISPLAYMASKS_RVA, rva::THUNK_DISPLAYMASKS_BYTES, (void*)&hooked_displaymasks, &t);
  engine_mark_hook_result(okM);
  if (okM) { s_origMasks = (DisplayMasksFn)t; log_recovered("displaymasks", t, rva::DISPLAYMASKS_BODY_RVA); }
  t = 0;
  const bool okR = hook_jmp_thunk(rva::THUNK_DISPLAYREST_RVA, rva::THUNK_DISPLAYREST_BYTES, (void*)&hooked_displayrest, &t);
  engine_mark_hook_result(okR);
  if (okR) { s_origRest = (DisplayRestFn)t; log_recovered("displayrest", t, rva::DISPLAYREST_BODY_RVA); }
  // vscrn is CALLED, not hooked: only its fingerprint is checked, and a mismatch keeps the mode off.
  const unsigned char* v = g_base + rva::THUNK_VSCRN_RVA;
  bool okV = true;
  for (int i = 0; i < 5; i++) okV = okV && v[i] == rva::THUNK_VSCRN_BYTES[i];
  if (okV) s_vscrn = (VscrnFn)(g_base + rva::THUNK_VSCRN_RVA);
  LOGI("shot: hooks weapon=%d masks=%d rest=%d vscrn=%d", okW, okM, okR, okV);
  if (!(okW && okM && okR && okV)) LOGE("shot: screenshot mode is off for this session");
  return okW && okM && okR && okV;
}
