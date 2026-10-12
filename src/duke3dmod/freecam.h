#pragma once
// Free camera: pure state and integration (freecam.cpp) plus the DLL wiring (camera.cpp). Pitch is a free angle.
#include "project.h"
#include <stdio.h>
#include <string.h>

// The camera never moves the player: camera.cpp substitutes this state into ps[] for one displayrooms call.

// Percent of FREECAM_BASE_SPEED; 500% because big expansion maps take too long to cross.
const int FREECAM_SPEED_MIN     = 1;
const int FREECAM_SPEED_MAX     = 500;
const int FREECAM_SPEED_DEFAULT = 100;

const int FREECAM_BASE_SPEED = BUILD_UNITS_PER_METRE / 4;


// 0.23 units/count (the aim's rate) doesn't survive an integer, so freecam_step carries the remainder in milli units.
const int FREECAM_SENS_MILLI_UNIT = 1000;
const int FREECAM_SENS_MILLI_MIN  = 1;       // 0 would freeze it
const int FREECAM_SENS_MILLI_MAX  = 10000;
struct FreecamSens { int yawMilli, pitchMilli; };
inline int freecam_sens_milli_clamped(int m) { return clamp_int(m, FREECAM_SENS_MILLI_MIN, FREECAM_SENS_MILLI_MAX); }
inline FreecamSens freecam_sens_default() { FreecamSens s = { FREECAM_SENS_MILLI_UNIT, FREECAM_SENS_MILLI_UNIT }; return s; }
// Mouse-aim sensitivity x1000 when modern mouse aim is on; off, the historic default.
FreecamSens freecam_sens_from_mouseaim(bool mouseaimOn, int sens, int vscale);

// Build's z axis is 16x x/y; vertical travel is scaled to match. PHEIGHT = 0x2800 = 1.25 m.
const int FREECAM_Z_PER_XY = 16;

const int FREECAM_ANG_UNITS = 2048;

const int FREECAM_SIN_SCALE = 16384;

// Bounded short of vertical (500 =~ 87.9 degrees) so the horiz derivation never divides by zero.
const int FREECAM_PITCH_MAX = 500;

// The engine's `horiz` from a pitch: 100 + 128*sin/cos, integer division as displayrooms draws it.
inline int freecam_pitch_to_horiz(int pitch, const short* sintable) {
  if (!sintable) return 100;
  const int sinP = sintable[pitch & (FREECAM_ANG_UNITS - 1)];
  const int cosP = sintable[(pitch + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  if (cosP == 0) return sinP >= 0 ? 32767 : -32768;
  return 100 + (128 * sinP) / cosP;
}

// Inverse, used once on entry; not a binary search (truncation flattens several pitches near 0 to one horiz).
int freecam_horiz_to_pitch(int horiz, const short* sintable);

// The steepest pitch Polymost can draw: past a tilt of ghalfx^2/(ydimen/2) pixels a corner falls behind the camera.
// Held to 90%, never below the cap minimum (a collapse to ~6 units locked the view).
const int FREECAM_PITCH_CAP_MIN = 256;
int freecam_pitch_cap(double ghalfx, int ydimen, int xdimenscale, const short* sintable);

// `active` is its own switch, not g_cfg.freecamon. p* is the PREVIOUS tic's pose for render interpolation; the
// remainders are last so initialisers zero them.
struct FreecamState { int x, y, z; int ang; int pitch; bool active;
                      int px, py, pz, pang, ppitch;
                      int yawRem, pitchRem; };

struct FreecamInput { int dx, dy; bool fwd, back, left, right, up, down, fast, slow; };

int freecam_speed_clamped(int speedPct);

int freecam_speed(int speedPct, bool fast, bool slow);

// Yaw takes dx (wraps); pitch takes -dy, clamped; travel follows the 3-D look. A null sintable refuses. `sens` is
// widened: 32 bits overflow at FREECAM_MOTION_LIMIT.
FreecamState freecam_step(const FreecamState& s, const FreecamInput& in,
                          int speedPct, const FreecamSens& sens, const short* sintable);

// Three rules: one shared symmetric rule let the camera stick on.

bool freecam_enter_rule(bool on, bool keyEdge, bool menuOpen, bool dead, bool valid);

// `on`, `dead` and `valid` must never hold it engaged, or switching the feature off (or dying) would lock the player in.
bool freecam_exit_rule(bool active, bool keyEdge, bool menuOpen);

// Death, an unreadable player, or freecamon switched off while flying.
bool freecam_force_off_rule(bool active, bool on, bool dead, bool valid);

// Starts at the player's eye (posz is already the eye); `horiz` becomes pitch so the first frame doesn't jump.
FreecamState freecam_enter(const PlayerPos& p, int ang, int horiz, const short* sintable);

// The teleport casts the engine's hitscan from the camera pose and lands the player there; the camera stays flying.
// `on` is absent as in freecam_exit_rule: gating a way out of the camera on the feature could only strand somebody.
bool freecam_teleport_rule(bool active, bool keyEdge, bool menuOpen);

// Look direction in the hitscan vx/vy/vz convention: vx/vy from yaw scaled to +-16384; vz from pitch alone,
// ((100 - horiz) << 5) << 6, no horizoff term.
struct HitscanDir { int vx, vy, vz; };

inline HitscanDir freecam_hitscan_direction(int ang, int horiz, const short* sintable) {
  HitscanDir d = { 0, 0, 0 };
  if (!sintable) return d;
  d.vx = sintable[(ang + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  d.vy = sintable[ang & (FREECAM_ANG_UNITS - 1)];
  d.vz = ((100 - horiz) << 5) << 6;
  return d;
}

const int TELEPORT_CLEARANCE = 256;

inline void freecam_teleport_step_back(int hitx, int hity, int vx, int vy,
                                       bool stepBack, int* outX, int* outY) {
  if (!outX || !outY) return;
  if (!stepBack) { *outX = hitx; *outY = hity; return; }
  *outX = clamp_ll_to_int((long long)hitx - (long long)vx * TELEPORT_CLEARANCE / FREECAM_SIN_SCALE);
  *outY = clamp_ll_to_int((long long)hity - (long long)vy * TELEPORT_CLEARANCE / FREECAM_SIN_SCALE);
}

// Stage one: a raw hitscan result into the point the engine resolves a sector for. hitsect < 0 is the one refusal.
struct TeleportAim { bool ok; int x, y, z; const char* reason; };

TeleportAim freecam_teleport_aim(int hitsect, int hitwall, int hitsprite,
                                 int hitx, int hity, int hitz,
                                 int vx, int vy);

// Stage two: a tight space is a refusal, not a clamp, so a picked destination never silently moves.
struct TeleportPlan { bool ok; int x, y, z; int sect; const char* reason; };

const char* const TELEPORT_REASON_NOTHING   = "Nothing to teleport to";
const char* const TELEPORT_REASON_NO_SECTOR = "Camera is outside the map";
const char* const TELEPORT_REASON_TIGHT     = "Not enough room to stand";
const char* const TELEPORT_REASON_NONE      = "";
// Destination fine but the write failed: an engine fact, not a map fact.
const char* const TELEPORT_REASON_NO_WRITE  = "The player could not be moved";

TeleportPlan freecam_teleport_plan(int x, int y, int sectFromEngine,
                                   int florz, int ceilz, int eyeHeight);

inline bool freecam_teleport_has_notice(const TeleportPlan& p) {
  return p.reason != 0 && p.reason[0] != '\0';
}

const unsigned FREECAM_NOTICE_MS = 2000;

// GetTickCount wraps every 49.7 days; `now - set` is unsigned, correct across it.
inline bool freecam_notice_visible(bool have, unsigned setMs, unsigned nowMs) {
  return have && (unsigned)(nowMs - setMs) < FREECAM_NOTICE_MS;
}

// Landing marker: a column 0.44 m across and 1.5 m tall (a full 2 m footprint read too big); xy unit is 1/512 m, z
// unit 1/8192.
const int FREECAM_TPMARK_HALF   = 224;
const int FREECAM_TPMARK_HEIGHT = 0x3000;
struct FreecamMarkCorner { int x, y; };
struct FreecamMarkBox { FreecamMarkCorner c[4]; int zTop, zBottom; };
// Saturated: a near-limit engine coordinate would wrap a corner across the map.
inline FreecamMarkBox freecam_teleport_box(int x, int y, int z) {
  const long long h = FREECAM_TPMARK_HALF;
  const int lx = clamp_ll_to_int((long long)x - h), hx = clamp_ll_to_int((long long)x + h);
  const int ly = clamp_ll_to_int((long long)y - h), hy = clamp_ll_to_int((long long)y + h);
  FreecamMarkBox b;
  b.c[0].x = lx; b.c[0].y = ly;
  b.c[1].x = hx; b.c[1].y = ly;
  b.c[2].x = hx; b.c[2].y = hy;
  b.c[3].x = lx; b.c[3].y = hy;
  b.zTop    = clamp_ll_to_int((long long)z - (long long)FREECAM_TPMARK_HEIGHT);
  b.zBottom = z;
  return b;
}

// DLL only: pushes the column's quads on the render clock, after the secret volume.
void camera_draw_teleport_marker();

// movetransports (VA 0x0049F260) carries the player across a paired sector effector 7; a camera flying through one came
// out in the wrong half, so it gets the game's rule.

const int FREECAM_TRANSPORT_STAT = 9;           // SE 7 sprites
const int FREECAM_TRANSPORT_ZBAND = 0x1800;     // lotag 0's z window and landing offset
const int FREECAM_TRANSPORT_SURFACE = 0x1000;   // lotag 1's band below the floor
const int FREECAM_TRANSPORT_CEILBAND = 0x600;   // lotag 2's band below the ceiling
const int FREECAM_TRANSPORT_LAND = 0x700;       // lotag 1/2 landing offset
const int FREECAM_TRANSPORT_HOLD_TICS = 13;

// lotag is the EFFECTOR'S SECTOR's (0 teleporter, 1 water surface, 2 underwater), not the sprite's.
struct FreecamTransport { int lotag, seX, seY, seZ, ownerX, ownerY, ownerZ,
                          ownerCeilZ, ownerFloorZ, secFloorZ, secCeilZ, tempData4; };
struct FreecamJump { int hit, x, y, z; };   // int flags only (MSVC fold defect)

// Mirrors movetransports' three cases; lotag 1 and 2 use their z bands alone (no ground/crouch/fall terms).
inline FreecamJump freecam_transport_jump(const FreecamTransport& t, int x, int y, int z) {
  FreecamJump j = { 0, x, y, z };
  // The engine skips an unpaired effector (owner == self).
  if (t.seX == t.ownerX && t.seY == t.ownerY && t.seZ == t.ownerZ) return j;
  int hit = 0, zTo = z;
  if (t.lotag == 0) {
    // temp_data[4] == 0 is a floor teleporter; non-zero is half of a water pair.
    const long long dz = (long long)t.seZ - z;
    hit = (t.tempData4 == 0 && (dz < 0 ? -dz : dz) < FREECAM_TRANSPORT_ZBAND) ? 1 : 0;
    zTo = t.ownerZ + FREECAM_TRANSPORT_ZBAND;
  } else if (t.lotag == 1) {
    hit = (t.tempData4 != 0 && z > t.secFloorZ - FREECAM_TRANSPORT_SURFACE) ? 1 : 0;
    zTo = t.ownerCeilZ + FREECAM_TRANSPORT_LAND;
  } else if (t.lotag == 2) {
    hit = (t.tempData4 != 0 && z < t.secCeilZ + FREECAM_TRANSPORT_CEILBAND) ? 1 : 0;
    zTo = t.ownerFloorZ - FREECAM_TRANSPORT_LAND;
  }
  if (!hit) return j;
  j.hit = 1;
  j.x = clamp_ll_to_int((long long)x + t.ownerX - t.seX);
  j.y = clamp_ll_to_int((long long)y + t.ownerY - t.seY);
  j.z = zTo;
  return j;
}


bool camera_install();

// Once per tic from hooked_getinput after getinput fills `loc`, before the lockout clears it. Writes no player state;
// the write-back is camera_release_tic()'s.
void camera_toggle_tic(int player);

// Applies a deferred deactivation (releases the lockout, writes view fields back); after the original processinput
// since it writes the player struct.
void camera_release_tic();

// From hooked_processinput after the original, only while active: drains input (always, so a key released while paused
// isn't missed), steps, publishes a snapshot.
void camera_tic();

bool camera_active();

// Whether THIS frame is substituted; not camera_active(), as hooked_displayrooms can refuse to arm.
bool camera_render_armed();
int camera_pitch_cap_last();

struct FreecamPose { int x, y, z, ang, pitch; };
inline int freecam_parse_readout(const char* text, FreecamPose* out) {
  if (!text || !out) return 0;
  const char* p = text;
  while ((p = strstr(p, "cam x=")) != 0) {
    FreecamPose r;
    if (sscanf_s(p, "cam x=%d y=%d z=%d ang=%d pitch=%d", &r.x, &r.y, &r.z, &r.ang, &r.pitch) == 5) {
      *out = r;
      return 1;
    }
    p += 6;
  }
  return 0;
}
void camera_request_pose(const FreecamPose& pose);

// smoothratio of the last hooked_displayrooms call (16.16), for mouseaim.cpp's per-frame view override; 0 before the first frame.
long camera_last_smoothratio();

bool camera_snapshot(FreecamState* out);

// The teleport's one feedback line, or null; not gated on the camera, so a refusal survives it switching off.
const char* camera_notice();
