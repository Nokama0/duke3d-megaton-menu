#pragma once
#include "common.h"
#include <math.h>

// Modern mouse aim. Pure; mouseaim.cpp owns the hooks. Facts: docs/re/mouse.md.
namespace rva {
  const unsigned THUNK_DNGETMOUSEINPUT_RVA = 0x00003C10;
  const unsigned char THUNK_DNGETMOUSEINPUT_BYTES[5] = { 0xE9, 0x9B, 0x41, 0x00, 0x00 };
  const unsigned DNGETMOUSEINPUT_BODY_RVA  = 0x00007DB0;
  const unsigned THUNK_READMOUSEXY_RVA     = 0x0000364D;
  const unsigned char THUNK_READMOUSEXY_BYTES[5]     = { 0xE9, 0xEE, 0x05, 0x09, 0x00 };
  const unsigned READMOUSEXY_BODY_RVA      = 0x00093C40;
  const unsigned MM_NUMDEVICES = 0x001D3054;  // ManyMouse devices; > 0 means s_dx/s_dy are the source
  const unsigned MM_DX         = 0x001D3048;  // pending raw counts since the game last read them
  const unsigned MM_DY         = 0x001D3044;
  const unsigned PS_AIM_MODE   = 0x280;  // player_struct +640 (long): 0 = the game's mouse aiming is off
  const unsigned UD_MOUSEFLIP  = 2384;
  const unsigned GCTANG   = 0x01773298;
  const unsigned GSTANG   = 0x01774320;
  const unsigned WINDOWX2 = 0x018911E0;
  const unsigned WINDOWY2 = 0x01893F50;
  // The engine's OTHER copy of the view yaw (16384-scaled sin/cos of the integer globalang), read by polymost_drawalls/
  // scansector/drawsprite; left stale, walls swim against the fractional projection. No restore: drawrooms rewrites it.
  const unsigned COSGLOBALANG = 0x018D1688;
  const unsigned SINGLOBALANG = 0x01893F24;
  // initmosts(double* px, double* py, long n), __cdecl: re-seeds the vsp list the 3D pass clips against. CALLED, never
  // hooked; five bytes fingerprinted at install, a mismatch keeps the feature off.
  const unsigned THUNK_INITMOSTS_RVA = 0x00002CC5;
  const unsigned char THUNK_INITMOSTS_BYTES[5] = { 0xE9, 0x76, 0x60, 0x07, 0x00 };
  const unsigned INITMOSTS_BODY_RVA  = 0x00078D40;
}

const int MOUSEAIM_SENS_MIN = 5, MOUSEAIM_SENS_MAX = 200, MOUSEAIM_SENS_DEFAULT = 40;
const int MOUSEAIM_VSCALE_MIN = 50, MOUSEAIM_VSCALE_MAX = 150, MOUSEAIM_VSCALE_DEFAULT = 100;
const double MOUSEAIM_PI = 3.14159265358979323846;
const double MOUSEAIM_PITCH_LIMIT_DEG = 57.0;
// Ceilings on the two carry accumulators: a stuck mouse, or a clamp that keeps refusing (locAvel/locHorz pinned by held
// keyboard turning), would grow the pending value unbounded and make the (int) casts UB.
const double MOUSEAIM_YAW_PENDING_MAX   = 4096.0;
// Per-frame yaw lead cap, angle units (2048 per turn). An uncapped lead on a fast turn draws black at the screen's
// edges (Polymost's bunch scan does not reach walls outside the view cone); 64 units (11 degrees) removed it. The ini's
// mouseaimleadcap overrides it.
const int MOUSEAIM_LEAD_CAP_UNITS = 64;
// 64 was measured at ~175 fps, where a frame is a small slice of a 26 Hz tic; at 40 fps a frame covers most of a tic and
// clipping made the view lag and step. So the cap scales with frame duration against the ~5.7 ms it was tuned at, up to
// 8x (512 units) so a stall cannot uncap it.
const int MOUSEAIM_LEAD_CAP_REF_US = 5700;
const int MOUSEAIM_LEAD_CAP_MAX_SCALE = 8;
inline int mouseaim_lead_cap_for_frame(int capBase, int frameUs) {
  if (capBase <= 0) return capBase;
  if (frameUs <= MOUSEAIM_LEAD_CAP_REF_US) return capBase;
  const long long scaled = (long long)capBase * frameUs / MOUSEAIM_LEAD_CAP_REF_US;
  const long long ceiling = (long long)capBase * MOUSEAIM_LEAD_CAP_MAX_SCALE;
  return (int)(scaled < ceiling ? scaled : ceiling);
}
// `lead` is the view minus the engine's interpolated angle, any magnitude; returns it wrapped to [-1024, 1024) and
// clipped to +/- cap (cap <= 0: no clip).
inline double mouseaim_lead_capped(double lead, int cap) {
  while (lead >= 1024.0) lead -= 2048.0;
  while (lead < -1024.0) lead += 2048.0;
  if (cap <= 0) return lead;
  if (lead > (double)cap) return (double)cap;
  if (lead < -(double)cap) return -(double)cap;
  return lead;
}
const double MOUSEAIM_HORIZ_PENDING_MAX = 400.0;

inline int mouseaim_sens_clamped(int v)   { return clamp_int(v, MOUSEAIM_SENS_MIN, MOUSEAIM_SENS_MAX); }
inline int mouseaim_vscale_clamped(int v) { return clamp_int(v, MOUSEAIM_VSCALE_MIN, MOUSEAIM_VSCALE_MAX); }
inline double mouseaim_yaw_units_per_count(int sens) {
  return (mouseaim_sens_clamped(sens) / 1000.0) * (2048.0 / 360.0);
}
inline double mouseaim_pitch_deg_per_count(int sens, int vscale, double match = 1.0) {
  return (mouseaim_sens_clamped(sens) / 1000.0) * (mouseaim_vscale_clamped(vscale) / 100.0) * match;
}
inline double mouseaim_pitch_deg_of_horiz(double horizUnits) {
  return atan((horizUnits - 100.0) / 128.0) * 180.0 / MOUSEAIM_PI;
}
inline double mouseaim_horiz_of_pitch_deg(double deg) {
  if (deg >  MOUSEAIM_PITCH_LIMIT_DEG) deg =  MOUSEAIM_PITCH_LIMIT_DEG;
  if (deg < -MOUSEAIM_PITCH_LIMIT_DEG) deg = -MOUSEAIM_PITCH_LIMIT_DEG;
  return 100.0 + 128.0 * tan(deg * MOUSEAIM_PI / 180.0);
}

// Yaw's lateral scale follows the FOV but Build's shear pitch always projects at halfxdimen per radian, so off FOV 90 the
// axes differ (FOV 120: pitch moved 1.7x as many pixels). match restores yaw's pixel scale; clamped against a degenerate
// viewingrange.
inline double mouseaim_pitch_match(int viewingrange) {
  if (viewingrange <= 0) return 4.0;
  double m = 65536.0 / (double)viewingrange;
  if (m > 4.0)  m = 4.0;
  if (m < 0.25) m = 0.25;
  return m;
}

inline double mouseaim_clamp_pending(double v, double maxAbs) {
  if (v >  maxAbs) return  maxAbs;
  if (v < -maxAbs) return -maxAbs;
  return v;
}

struct MouseAimState { double yawPending; double horizPending; int appliedAvel; int appliedHorz; };
inline void mouseaim_reset(MouseAimState* s) { if (s) { s->yawPending = 0; s->horizPending = 0; s->appliedAvel = 0; s->appliedHorz = 0; } }

struct MouseAimTic { int avelAdd; int horzAdd; };

// Pitch delta in horizon units for a mouse delta, taken around the CURRENT pitch so angular sensitivity is the same at
// every pitch (the horizon is tan-shaped).
inline double mouseaim_pitch_units_delta(int countsY, int sens, int vscale, bool flip, double horizNowUnits, double match = 1.0) {
  const double dir = flip ? 1.0 : -1.0;
  const double ddeg = dir * countsY * mouseaim_pitch_deg_per_count(sens, vscale, match);
  const double p0 = mouseaim_pitch_deg_of_horiz(horizNowUnits);
  return mouseaim_horiz_of_pitch_deg(p0 + ddeg) - mouseaim_horiz_of_pitch_deg(p0);
}

inline MouseAimTic mouseaim_tic(MouseAimState* s, int countsX, int countsY, int sens, int vscale,
                                int locAvel, int locHorz, int horizNow, bool flip, bool aimMode,
                                double pitchMatch = 1.0) {
  MouseAimTic t = { 0, 0 };
  if (!s) return t;
  // locAvel/locHorz are a blind read of live engine memory, clamped first: -128 would make `room` compute +1, a spurious
  // avel with nothing pending.
  locAvel = clamp_int(locAvel, -127, 127);
  locHorz = clamp_int(locHorz, -127, 127);
  // Yaw: accumulate in angle units, hand the game whole avel units (two angle units each, truncated toward zero), keep
  // the rest. The clamp is on the SUM with what the game already put in loc; what it refuses stays pending.
  s->yawPending += countsX * mouseaim_yaw_units_per_count(sens);
  int avel = clamp_double_to_int(s->yawPending / 2.0);
  const int room = clamp_int(locAvel + avel, -127, 127) - locAvel;
  avel = room;
  s->yawPending -= 2.0 * avel;
  s->yawPending = mouseaim_clamp_pending(s->yawPending, MOUSEAIM_YAW_PENDING_MAX);
  t.avelAdd = avel;
  s->appliedAvel = avel;
  if (!aimMode) { s->horizPending = 0; s->appliedHorz = 0; return t; }
  s->horizPending += mouseaim_pitch_units_delta(countsY, sens, vscale, flip, (double)horizNow, pitchMatch);
  int units = clamp_double_to_int(s->horizPending);
  int horz = 2 * units;                                    // the game applies horz >> 1, so only even values survive
  const int roomH = clamp_int(locHorz + horz, -127, 127) - locHorz;
  horz = roomH - (roomH % 2);
  s->horizPending -= horz / 2.0;
  s->horizPending = mouseaim_clamp_pending(s->horizPending, MOUSEAIM_HORIZ_PENDING_MAX);
  t.horzAdd = horz;
  s->appliedHorz = horz;
  return t;
}

// The view: the engine's interpolated angle/horizon plus the unshown part of the last tic's step, the carry, and counts
// that arrived since the tic.
struct MouseAimView { double ang; double horiz; };
inline MouseAimView mouseaim_view(const MouseAimState& s, int angInterp, double horizInterpUnits,
                                  int pendingX, int pendingY, int sens, int vscale, bool flip, bool aimMode, double smooth,
                                  double pitchMatch = 1.0) {
  MouseAimView v;
  // catchUp assumes the engine applied the tic's full `avel << 1`; one movement state applies 7/8, so the view leads by
  // the remaining 1/8 that tic (a small sawtooth proportional to turn rate; accepted).
  const double catchUp = 1.0 - smooth;
  const double yawPendingCounts = mouseaim_clamp_pending(pendingX * mouseaim_yaw_units_per_count(sens), MOUSEAIM_YAW_PENDING_MAX);
  v.ang = (double)angInterp + 2.0 * s.appliedAvel * catchUp + s.yawPending + yawPendingCounts;
  v.horiz = horizInterpUnits;
  if (aimMode) {
    const double horizPendingCounts = mouseaim_clamp_pending(
        mouseaim_pitch_units_delta(pendingY, sens, vscale, flip, horizInterpUnits, pitchMatch), MOUSEAIM_HORIZ_PENDING_MAX);
    v.horiz += (s.appliedHorz / 2.0) * catchUp + s.horizPending + horizPendingCounts;
  }
  return v;
}

// The same view through a mirror: the engine draws it as a second drawrooms call at the reflected angle `tang`
// (preparemirror, VA 0x00446300 area), so yaw lead changes sign; the horizon passes through. angInterp is already `tang`.
inline MouseAimView mouseaim_mirror_view(const MouseAimView& v, int angInterp) {
  MouseAimView m;
  m.ang   = 2.0 * (double)angInterp - v.ang;
  m.horiz = v.horiz;
  return m;
}

// `viewscreen`: the drawn view is a security-camera substitution; displayrooms overwrites the WHOLE view from the camera
// sprite and forces `smoothratio` to 0x10000, so mouse lead belongs to no one's view.
inline bool mouseaim_gate(bool on, bool verified, bool installed, bool cameraActive, bool lockout, bool menuOpen, bool paused, bool viewscreen) {
  return on && verified && installed && !cameraActive && !lockout && !menuOpen && !paused && !viewscreen;
}

// Whether this tic's apply may touch the accumulator, `loc` and s_active: only for the LOCAL player; another player's
// repeated same-tic apply would zero appliedAvel/appliedHorz, wiping the local catch-up term.
inline bool mouseaim_apply_for_player(int player, int local) { return player == local; }

// Whether this tic must THROW ITS COUNTS AWAY and reset the accumulator: the gate reopens this tic (counts made while
// off would lurch the view), or the render gate closed since the last tic (Duke's pause stops the tic clock).
inline bool mouseaim_discard_this_tic(bool wasActive, bool nowActive, bool frameGateClosedSinceLastTic) {
  return (nowActive && !wasActive) || frameGateClosedSinceLastTic;
}

struct ViewDoubles { double cosang, sinang, cosang2, sinang2, shang, chang; };
inline ViewDoubles mouseaim_view_doubles(double angUnits, double horizPx, int viewingrange, int halfxdimen, int ydimen) {
  ViewDoubles d;
  const double a = angUnits * MOUSEAIM_PI / 1024.0;
  d.cosang  = cos(a) / 16.0;
  d.sinang  = sin(a) / 16.0;
  d.cosang2 = (double)viewingrange * d.cosang / 65536.0;
  d.sinang2 = (double)viewingrange * d.sinang / 65536.0;
  const double halfx  = (double)halfxdimen;
  const double dh     = (double)(ydimen >> 1) - horizPx;
  const double radius = sqrt(halfx * halfx + dh * dh);
  d.shang = radius > 0.0 ? dh / radius : 0.0;
  d.chang = radius > 0.0 ? halfx / radius : 1.0;
  return d;
}
inline double mouseaim_horiz_units_to_px(double horizUnits, int xdimenscale, int ydimen) {
  return (horizUnits - 100.0) * (double)xdimenscale / 65536.0 + (double)(ydimen >> 1);
}

inline void mouseaim_int_trig(double angUnits, long* cosOut, long* sinOut) {
  const double a = angUnits * MOUSEAIM_PI / 1024.0;
  const double c = 16384.0 * cos(a);
  const double s = 16384.0 * sin(a);
  if (cosOut) *cosOut = (long)(c >= 0.0 ? c + 0.5 : c - 0.5);
  if (sinOut) *sinOut = (long)(s >= 0.0 ? s + 0.5 : s - 0.5);
}

// The engine's THIRD copy of the view yaw, the short `globalang` (rva::GLOBALANG), which the parallax sky pans by.
// Rounded to the NEAREST unit so the sky stays within half a unit of the walls; `& 2047` is the engine's `& 0x7ff`.
inline int mouseaim_globalang_of_view(double angUnits) {
  return clamp_double_to_int(floor(angUnits + 0.5)) & 2047;
}

// polymost_drawrooms' SCREEN CLIP POLYGON (body VA 0x0047a2d0): viewport corners (-1 and span + 3) rotated by tilt and
// pitch, clipped against z > 1, handed to initmosts before scansector. Moving only the pitch leaves geometry drawn
// against the old region (black void, morphing textures), so the override rebuilds this.
struct Frustum { double px[8], py[8]; int n; };
inline Frustum mouseaim_frustum(int windowx1, int windowy1, int windowx2, int windowy2,
                                double ghalfx, double ghoriz, double gctang, double gstang,
                                double gshang, double gchang) {
  double sx[4] = { -1.0, (double)((windowx2 - windowx1) + 3), (double)((windowx2 - windowx1) + 3), -1.0 };
  double sy[4] = { -1.0, -1.0, (double)((windowy2 - windowy1) + 3), (double)((windowy2 - windowy1) + 3) };
  double vx[4], vy[4], vz[4];
  for (int i = 0; i < 4; i++) {
    const double ox = sx[i] - ghalfx, oy = sy[i] - ghoriz;
    const double tx = oy * gstang + ox * gctang;
    const double ty = oy * gctang - ox * gstang;
    vx[i] = tx;
    vy[i] = ghalfx * gshang + ty * gchang;
    vz[i] = ghalfx * gchang - ty * gshang;
  }
  Frustum f; f.n = 0;
  double cx[8], cy[8], cz[8];
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) & 3;
    if (vz[i] > 1.0) { cx[f.n] = vx[i]; cy[f.n] = vy[i]; cz[f.n] = vz[i]; f.n++; }
    if ((vz[i] > 1.0) != (vz[j] > 1.0)) {
      const double t = (1.0 - vz[i]) / (vz[j] - vz[i]);
      cx[f.n] = (vx[j] - vx[i]) * t + vx[i]; cy[f.n] = (vy[j] - vy[i]) * t + vy[i]; cz[f.n] = 1.0; f.n++;
    }
  }
  for (int i = 0; i < f.n; i++) { const double r = ghalfx / cz[i]; f.px[i] = cx[i] * r + ghalfx; f.py[i] = cy[i] * r + ghoriz; }
  return f;
}

inline const char* mouseaim_gate_reason(bool on, bool verified, bool installed, bool cameraActive,
                                        bool lockout, bool menuOpen, bool paused, bool viewscreen) {
  if (!on)           return "switch off";
  if (!installed)    return "hooks not installed";
  if (!verified)     return "engine or player unavailable";
  if (cameraActive)  return "free camera";
  if (lockout)       return "panel lockout";
  if (menuOpen)      return "game menu";
  if (paused)        return "paused";
  if (viewscreen)    return "viewscreen";
  return "on";
}

// Saturating accumulate for the raw counts the reader hooks sum between tics: readmousexy has out-of-band callers
// (getmousevalues, the editor/OSD cursor) and a stuck device could overflow a plain int. Added in double before the clamp.
const int MOUSEAIM_COUNTS_MAX = 1 << 24;
inline int mouseaim_add_counts(int acc, int delta) {
  const double v = (double)acc + (double)delta;
  if (v >  (double)MOUSEAIM_COUNTS_MAX) return  MOUSEAIM_COUNTS_MAX;
  if (v < -(double)MOUSEAIM_COUNTS_MAX) return -MOUSEAIM_COUNTS_MAX;
  return (int)v;
}

const unsigned MOUSEAIM_DIAG_WINDOW_MS = 10000;

// Defined in mouseaim.cpp, DLL only (patches live thunks): declared here for dllmain.cpp, input.cpp's getinput hook and
// the render override.
bool mouseaim_install();
bool mouseaim_installed();
void mouseaim_note_render_redirect_result();
void mouseaim_tic_apply(int player);
void mouseaim_render_override();
bool mouseaim_active_now();

