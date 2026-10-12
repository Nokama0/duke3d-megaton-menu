#include "mouseaim.h"
#include "hooks.h"
#include "engine.h"
#include "input.h"
#include "freecam.h"
#include "shot.h"
#include "target.h"
#include "nocull.h"
#include "config.h"
#include "log.h"
#include <windows.h>

// Modern mouse aim, tic side (pure rules: mouseaim.h). The game truncates raw counts into loc.avel/horz (dead zone <1.0,
// uneven pitch); this file diverts them to mouseaim_tic, which carries the fraction. Hooks call the original first, and
// zero its outputs only while s_active (decided at an apply's end: a deliberate one-tic delay); loc is only ADDED to.

typedef void (__cdecl* DnGetMouseInputFn)(int*, int*, int);
typedef void (__cdecl* ReadMouseXYFn)(long*, long*);
typedef void (__cdecl* InitMostsFn)(double*, double*, long);

static DnGetMouseInputFn s_origDnGet = 0;
static ReadMouseXYFn     s_origRead  = 0;
static InitMostsFn       s_initmosts = 0;   // set only when the thunk's five bytes matched
static bool s_installed = false;
static MouseAimState s_state = { 0.0, 0.0, 0, 0 };
static int  s_countsX = 0, s_countsY = 0;  // raw counts taken since the last apply
static bool s_active = false;              // the gate the last apply decided
// Set when the render override's frame gate refuses; s_active cannot see a pause (no tic runs).
static bool s_frameGateClosed = false;
static bool s_flipLastTic    = false;
static bool s_aimModeLastTic = false;
// ps.newowner (rva::PS_NEWOWNER, +708, short, sign-extended): >= 0 means the drawn view is a viewscreen substitution.
static int s_newownerLastTic = -1;
// The render override's pitchMatch, published once the frame's viewingrange is known. The tic side reads this, not
// engine_viewingrange(), which the 2D HUD pass and setaspect rewrite mid-frame. 1.0 until the override has run.
static double s_pitchMatchLastFrame = 1.0;
static int s_frameUs = 0;
static long long s_lastFrameTick = 0;
static void mouseaim_note_frame_time() {
  LARGE_INTEGER freq, now;
  if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) return;
  if (!QueryPerformanceCounter(&now)) return;
  const long long t = now.QuadPart;
  if (s_lastFrameTick > 0 && t > s_lastFrameTick) {
    const long long us = (t - s_lastFrameTick) * 1000000 / freq.QuadPart;
    // A pause or breakpoint must not raise the cap.
    s_frameUs = (us > 0 && us < 250000) ? (int)us : 0;
  }
  s_lastFrameTick = t;
}
static int    s_diagPendingX = 0, s_diagPendingY = 0;
static double s_diagSmooth   = 0.0;
static double s_diagYawLead  = 0.0;
static double s_diagLeadMax  = 0.0;
static int    s_diagLeadBig  = 0;     // |lead| > 64 units
static int    s_diagLeadCapped = 0;
// Clip polygon point count: 4 normally, 5-8 when corners fall behind the near plane; under 3 refuses every frame.
static int    s_diagFrustumN = 0;

// "Installed" covers both thunks, the scansector redirect AND initmosts: the tic half alone would give the dead-zone fix
// without the smooth per-frame view, which is not an offered mode.
bool mouseaim_installed()  { return s_installed && s_initmosts != 0 && nocull_available(); }
bool mouseaim_active_now() { return s_active; }

// Called once after nocull_install(): logs the case the tic-side log misses (thunks in, culling redirect not).
void mouseaim_note_render_redirect_result() {
  if (s_installed && !nocull_available())
    LOGE("mouseaim: the culling redirect did not install; mouse aim stays off for this session");
}

// The game picks its mouse reader at startup by ManyMouse device count (dnGetMouseInput raw, readmousexy SDL fallback);
// both are hooked. A null original cannot happen while s_installed, but each hook guards it. The only known caller of
// dnGetMouseInput passes keep == 0 (a consuming read); a peek passes through.
static void __cdecl hooked_dngetmouseinput(int* dx, int* dy, int keep) {
  if (!s_origDnGet) { if (dx) *dx = 0; if (dy) *dy = 0; return; }
  s_origDnGet(dx, dy, keep);
  if (s_active && keep == 0 && dx && dy) {
    s_countsX = mouseaim_add_counts(s_countsX, *dx);
    s_countsY = mouseaim_add_counts(s_countsY, *dy);
    *dx = 0; *dy = 0;
  }
}

static void __cdecl hooked_readmousexy(long* x, long* y) {
  if (!s_origRead) { if (x) *x = 0; if (y) *y = 0; return; }
  s_origRead(x, y);
  if (s_active && x && y) {
    s_countsX = mouseaim_add_counts(s_countsX, (int)*x);
    s_countsY = mouseaim_add_counts(s_countsY, (int)*y);
    *x = 0; *y = 0;
  }
}

static bool diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, MOUSEAIM_DIAG_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

// From hooked_getinput right after the original, once per connected player. Local-player guard first: another player's
// same-tic apply would overwrite appliedAvel/appliedHorz with the zeros the render catch-up reads, stuttering the view.
void mouseaim_tic_apply(int player) {
  if (!mouseaim_apply_for_player(player, engine_myconnectindex())) return;

  const int cx = s_countsX, cy = s_countsY;
  s_countsX = 0; s_countsY = 0;

  unsigned psRva = 0;
  const bool havePs   = engine_verified() && camera_ps_rva(engine_myconnectindex(), &psRva);
  const bool menuOpen = havePs && (read_field_bytes(g_base, psRva + rva::PS_GM, 1) & 1) != 0;
  const bool aimMode  = havePs && read_field_bytes(g_base, psRva + rva::PS_AIM_MODE, 4) != 0;
  const int  newowner = havePs ? (int)read_field_bytes(g_base, psRva + rva::PS_NEWOWNER, 2) : -1;
  const bool viewscreen = newowner >= 0;
  const int  psHoriz  = havePs ? (int)read_field_bytes(g_base, psRva + rva::PS_OFFSET_HORIZ, 4) : 100;
  // ps[].horiz updates only in processinput, so it excludes this tic's keyboard look already in loc.horz; horizNow adds
  // half (the game applies horz >> 1). Clamped at the read so it, the write-back and mouseaim_tic agree at +-127.
  unsigned char* loc = g_base + rva::RVA_LOC;
  const int locAvel = engine_verified() ? clamp_int((signed char)loc[0], -127, 127) : 0;
  const int locHorz = engine_verified() ? clamp_int((signed char)loc[1], -127, 127) : 0;
  const int  horizNow = psHoriz + locHorz / 2;
  const bool paused   = engine_paused();
  const bool flip     = engine_verified() && read_field_bytes(g_base, rva::UD + rva::UD_MOUSEFLIP, 4) != 0;
  const double pitchMatch = s_pitchMatchLastFrame;
  s_flipLastTic     = flip;
  s_aimModeLastTic  = aimMode;
  s_newownerLastTic = newowner;

  const bool on   = g_cfg.mouseaimon != 0;
  const bool ok   = engine_verified() && havePs;
  const bool cam  = camera_active();
  const bool lock = input_lockout_now();
  const bool gate = mouseaim_gate(on, ok, mouseaim_installed(), cam, lock, menuOpen, paused, viewscreen);

  // Latch read and cleared once: a pause that never flipped s_active still has to discard the counters' catch.
  const bool frameGateClosedSinceLastTic = s_frameGateClosed;
  s_frameGateClosed = false;
  const bool discard = mouseaim_discard_this_tic(s_active, gate, frameGateClosedSinceLastTic);
  if (gate && !discard) {
    const MouseAimTic t = mouseaim_tic(&s_state, cx, cy, g_cfg.mouseaimsens, g_cfg.mouseaimvscale,
                                       locAvel, locHorz, horizNow, flip, aimMode, pitchMatch);
    loc[0] = (unsigned char)(signed char)(locAvel + t.avelAdd);
    loc[1] = (unsigned char)(signed char)(locHorz + t.horzAdd);
  } else {
    // Gated or reopening: discard everything including the carry; the game had its own mouse on these tics.
    mouseaim_reset(&s_state);
  }

  s_active = gate;

  if (on && g_cfg.debuglog != 0 && diag_due())
    LOGI("mouseaim: gate=%s counts=%d,%d applied=%d,%d carry=%.3f,%.3f aim_mode=%d flip=%d horiz=%d "
         "pending=%d,%d smooth=%.3f lead=%.2f frustum=%d match=%.3f leadmax=%.1f big=%d capped=%d cap=%d",
         mouseaim_gate_reason(on, ok, mouseaim_installed(), cam, lock, menuOpen, paused, viewscreen),
         cx, cy, s_state.appliedAvel, s_state.appliedHorz,
         s_state.yawPending, s_state.horizPending, (int)aimMode, (int)flip, horizNow,
         s_diagPendingX, s_diagPendingY, s_diagSmooth, s_diagYawLead, s_diagFrustumN, pitchMatch,
         s_diagLeadMax, s_diagLeadBig, s_diagLeadCapped, g_cfg.mouseaimleadcap);
  if (on && g_cfg.debuglog != 0 && s_diagLeadBig >= 0) { s_diagLeadMax = 0.0; s_diagLeadBig = 0; s_diagLeadCapped = 0; }
}

// Draws each FRAME from mouseaim_view (interpolated view + unshown remainder, sub-unit carry, counts since the tic), since
// the tic gives the game only whole units. Runs from hooked_polymost_scansector (nocull.cpp) after drawrooms, twice with a
// mirror. Writes gcosang/gsinang/gcosang2/gsinang2/gshang/gchang (recomputed next call) and globalang (the sky pans by it).

// Refuses: gate re-asked on this clock (paused/menu/lockout change mid-pause with no tic); !engine_verified();
// camera_render_armed(); an unusable camera; s_newownerLastTic >= 0 (viewscreen). Not the mirror pass.
// Never writes ghoriz, globalhoriz or code.
void mouseaim_render_override() {
  if (!engine_verified()) return;

  const bool mirrorPass = engine_in_prepare_mirror();
  if (!mirrorPass) mouseaim_note_frame_time();

  unsigned psRva = 0;
  const bool havePs   = camera_ps_rva(engine_myconnectindex(), &psRva);
  const bool menuOpen = havePs && (read_field_bytes(g_base, psRva + rva::PS_GM, 1) & 1) != 0;
  if (!mouseaim_gate(s_active, havePs, mouseaim_installed(), camera_render_armed(),
                     input_lockout_now(), menuOpen, engine_paused(), s_newownerLastTic >= 0)) {
    // Latched for the next tic: s_active alone cannot see these volatile terms change mid-pause. Mirror pass too.
    s_frameGateClosed = true;
    // Else the diag's frustum= would keep the last accepted count. Main pass only.
    if (!mirrorPass) s_diagFrustumN = 0;
    return;
  }

  const CamState cam = engine_camera();
  if (!cam.valid || cam.halfxdimen <= 0 || cam.ydimen <= 0 || cam.xdimenscale <= 0 || cam.viewingrange <= 0) {
    if (!mirrorPass) s_diagFrustumN = 0;
    return;
  }

  // Counts since the last tic, read WITHOUT consuming (the tic drains them; consuming here would erase the frame's lead).
  int px = 0, py = 0;
  if (read_field_bytes(g_base, rva::MM_NUMDEVICES, 4) > 0) {
    px = (int)read_field_bytes(g_base, rva::MM_DX, 4);
    py = (int)read_field_bytes(g_base, rva::MM_DY, 4);
  } else {
    // mousex/mousey can be stale when the mouse isn't acquired or moustat is 0; check readmousexy's two flags.
    const bool acquired = read_field_bytes(g_base, rva::RVA_MOUSEACQUIRED, 1) != 0;
    const bool moustat  = read_field_bytes(g_base, rva::RVA_MOUSTAT, 1) != 0;
    if (acquired && moustat) {
      px = (int)read_field_bytes(g_base, rva::RVA_MOUSEX, 4);
      py = (int)read_field_bytes(g_base, rva::RVA_MOUSEY, 4);
    }
  }

  // CamState::horiz is globalhoriz in viewport pixels; mouseaim_view works in 0..200 units, so this runs
  // mouseaim_horiz_units_to_px backwards (tb_0088). Widened: cam.horiz is unchecked live memory.
  const double horizUnits = 100.0 + ((double)cam.horiz - (double)(cam.ydimen >> 1)) * 65536.0 / (double)cam.xdimenscale;

  double smooth = (double)camera_last_smoothratio() / 65536.0;
  if (smooth < 0.0) smooth = 0.0;
  if (smooth > 1.0) smooth = 1.0;

  const double pitchMatch = mouseaim_pitch_match(cam.viewingrange);
  s_pitchMatchLastFrame = pitchMatch;

  const MouseAimView player = mouseaim_view(s_state, cam.ang, horizUnits, px, py,
                                            g_cfg.mouseaimsens, g_cfg.mouseaimvscale,
                                            s_flipLastTic, s_aimModeLastTic, smooth, pitchMatch);
  // On the mirror pass cam.ang is the reflected angle, so the lead reflects with it; the horizon isn't mirrored. The
  // yaw lead is capped (MOUSEAIM_LEAD_CAP_UNITS, ini overrides): uncapped it drew black screen edges on fast turns.
  MouseAimView capped = player;
  {
    const double raw = player.ang - (double)cam.ang;
    const int cap = g_cfg.mouseaimleadcap > 0
                    ? g_cfg.mouseaimleadcap
                    : mouseaim_lead_cap_for_frame(MOUSEAIM_LEAD_CAP_UNITS, s_frameUs);
    const double lead = mouseaim_lead_capped(raw, cap);
    const double a = raw < 0.0 ? -raw : raw;
    if (!mirrorPass) {
      if (a > s_diagLeadMax) s_diagLeadMax = a;
      if (a > 64.0) s_diagLeadBig++;
    }
    if (lead != mouseaim_lead_capped(raw, 0)) {
      capped.ang = (double)cam.ang + lead;
      if (!mirrorPass) s_diagLeadCapped++;
    }
  }
  const MouseAimView v = mirrorPass ? mouseaim_mirror_view(capped, cam.ang) : capped;

  if (!mirrorPass) {
    s_diagPendingX = px;
    s_diagPendingY = py;
    s_diagSmooth   = smooth;
    s_diagYawLead  = player.ang - (double)cam.ang;
  }

  // Back to pixels for the horizon, then polymost's six formulas (mouseaim_view_doubles, tb_0088).
  const ViewDoubles d = mouseaim_view_doubles(v.ang,
                                              mouseaim_horiz_units_to_px(v.horiz, cam.xdimenscale, cam.ydimen),
                                              cam.viewingrange, cam.halfxdimen, cam.ydimen);

  // The clip polygon: drawrooms built it from its own pitch/tilt before this hook, so writing only the six doubles draws
  // walls against a stale region. Rebuilt BEFORE the writes: under 3 points the engine draws nothing, so leave it alone.
  const double gctang   = *(const double*)(g_base + rva::GCTANG);
  const double gstang   = *(const double*)(g_base + rva::GSTANG);
  const double ghalfx   = *(const double*)(g_base + rva::GHALFX);
  const double ghorizPx = *(const double*)(g_base + rva::GHORIZ);
  const long wx1 = read_field_bytes(g_base, rva::WINDOWX1, 4);
  const long wy1 = read_field_bytes(g_base, rva::WINDOWY1, 4);
  const long wx2 = read_field_bytes(g_base, rva::WINDOWX2, 4);
  const long wy2 = read_field_bytes(g_base, rva::WINDOWY2, 4);
  // Refusals, not clamps: ghalfx is the projection's divisor and the corners are unchecked live memory. Bounded to
  // 0..65535 since mouseaim_frustum computes (windowx2 - windowx1) + 3 in int.
  if (!(ghalfx > 0.0) || wx2 < wx1 || wy2 < wy1 ||
      wx1 < 0 || wy1 < 0 || wx2 > 65535 || wy2 > 65535) {
    if (!mirrorPass) s_diagFrustumN = 0;
    return;
  }

  Frustum f = mouseaim_frustum((int)wx1, (int)wy1, (int)wx2, (int)wy2,
                               ghalfx, ghorizPx, gctang, gstang, d.shang, d.chang);
  if (!mirrorPass) s_diagFrustumN = f.n;
  if (f.n < 3 || !s_initmosts) return;

  *(double*)(g_base + rva::GCOSANG)  = d.cosang;
  *(double*)(g_base + rva::GSINANG)  = d.sinang;
  *(double*)(g_base + rva::GCOSANG2) = d.cosang2;
  *(double*)(g_base + rva::GSINANG2) = d.sinang2;
  *(double*)(g_base + rva::GSHANG)   = d.shang;
  *(double*)(g_base + rva::GCHANG)   = d.chang;

  // The engine's other copy of the view yaw: drawalls/scansector/drawsprite read this pair for wall geometry and texture
  // setup, so a stale pair swims textures. No restore: drawrooms rewrites both.
  long cosGlobalAng = 0, sinGlobalAng = 0;
  mouseaim_int_trig(v.ang, &cosGlobalAng, &sinGlobalAng);
  *(long*)(g_base + rva::COSGLOBALANG) = cosGlobalAng;
  *(long*)(g_base + rva::SINGLOBALANG) = sinGlobalAng;

  // The short `globalang`: the parallax sky pans by this alone (six drawalls read sites), so a stale value slides the sky
  // against the walls. Rounded; no restore. Must stay two bytes: numwalls sits two before and sprite[] two after
  // (common.h, rva::NUMWALLS); pinned by test_esp and tb_0094.
  *(short*)(g_base + rva::GLOBALANG) = (short)mouseaim_globalang_of_view(v.ang);

  s_initmosts(f.px, f.py, (long)f.n);
}

static void log_recovered(const char* name, void* origTarget, unsigned bodyRva) {
  const unsigned origRva = (unsigned)((unsigned char*)origTarget - g_base);
  LOGI("mouseaim: %s thunk hook installed; recovered original target rva=%08X (expect %08X)%s",
       name, origRva, bodyRva, origRva == bodyRva ? "" : " -- MISMATCH, this may not be the right choke point");
}

bool mouseaim_install() {
  void* t = 0;
  // Ninth and tenth of twelve thunk fingerprint checks (engine.h's aggregate). Both are hooked though only one runs;
  // either failing keeps the feature off, as the unused path proves nothing about the live one.
  const bool okG = hook_jmp_thunk(rva::THUNK_DNGETMOUSEINPUT_RVA, rva::THUNK_DNGETMOUSEINPUT_BYTES,
                                  (void*)&hooked_dngetmouseinput, &t);
  engine_mark_hook_result(okG);
  if (okG) { s_origDnGet = (DnGetMouseInputFn)t; log_recovered("dnGetMouseInput", t, rva::DNGETMOUSEINPUT_BODY_RVA); }
  t = 0;
  const bool okR = hook_jmp_thunk(rva::THUNK_READMOUSEXY_RVA, rva::THUNK_READMOUSEXY_BYTES,
                                  (void*)&hooked_readmousexy, &t);
  engine_mark_hook_result(okR);
  if (okR) { s_origRead = (ReadMouseXYFn)t; log_recovered("readmousexy", t, rva::READMOUSEXY_BODY_RVA); }
  // initmosts is called, not hooked: only its five bytes are checked.
  const unsigned char* im = g_base + rva::THUNK_INITMOSTS_RVA;
  bool okI = true;
  for (int i = 0; i < 5; i++) okI = okI && im[i] == rva::THUNK_INITMOSTS_BYTES[i];
  if (okI) s_initmosts = (InitMostsFn)(g_base + rva::THUNK_INITMOSTS_RVA);
  else     LOGE("mouseaim: initmosts did not fingerprint-match; mouse aim stays off for this session");
  s_installed = okG && okR;
  LOGI("mouseaim: hooks dnGetMouseInput=%d readmousexy=%d initmosts=%d", okG, okR, okI);
  if (!s_installed) LOGE("mouseaim: modern mouse aim is off for this session");
  return s_installed;
}
