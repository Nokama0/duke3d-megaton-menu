#include "freecam.h"
#include "common.h"
#include "config.h"
#include "engine.h"
#include "hooks.h"
#include "input.h"
#include "light.h"
#include "log.h"
#include "target.h"   // snap_begin/snap_end/snap_read_ok, the seqlock aim.cpp uses
#include "canvas.h"
#include "world.h"
#include <windows.h>

// The free camera's DLL half: one detour, one tic step, one snapshot. Decisions live in freecam.h and common.h;
// review-only: step order in hooked_displayrooms, the arm guards, and that the original is called once on every path.

typedef void (__cdecl* DisplayRoomsFn)(short, long);
static DisplayRoomsFn s_origDisplayRooms = 0;

// Published snapshot: camera_tic writes on the TIC clock, displayrooms and esp.cpp read on the RENDER clock; the
// seqlock guarantees "never draw half of two tics". cursectnum is what updatesectorz reported, which displayrooms clips
// to (freecam-accuracy.md E1).
struct CameraMark { int ok; int x, y, z; int sect; };   // ok is an int: MSVC /O2 bool fold (docs/re/msvc-bool-fold.md)

struct CameraPub {
  FreecamState s;
  int          cursectnum;
  CameraMark   m;
  unsigned     seq;
};

static CameraPub s_pub = { { 0, 0, 0, 0, 0, false }, -1, { 0, 0, 0, 0, -1 }, 0u };

static FreecamState s_state = { 0, 0, 0, 0, 0, false };
static CameraMark s_mark = { 0, 0, 0, 0, -1 };
static int s_sect = -1;
// What the publish hands displayrooms; separate from s_sect so a seed sector the camera isn't in can't poison it.
static int s_renderSect = -1;
static int s_pitchCap = FREECAM_PITCH_MAX;   // last freecam_pitch_cap, for the readout
static FreecamPose s_pendingPose;
static int s_pendingPoseSet = 0;
void camera_request_pose(const FreecamPose& pose) { s_pendingPose = pose; s_pendingPoseSet = 1; }
int camera_pitch_cap_last() { return s_pitchCap; }
static bool s_loggedSectorLost = false;
// Tics left before a transport may carry the camera again (the engine's transporter_hold).
static int s_transportHold = 0;

static unsigned seq_read() { return *(volatile unsigned*)&s_pub.seq; }
static void seq_write(unsigned v) { *(volatile unsigned*)&s_pub.seq = v; }

static void publish(const FreecamState& s, int sect) {
  unsigned seq = seq_read();
  snap_begin(&seq);
  seq_write(seq);          // odd: readers refuse

  // Field by field so a new FreecamState field not listed here is visible to a reader, not silently dropped.
  s_pub.s.x = s.x;
  s_pub.s.y = s.y;
  s_pub.s.z = s.z;
  s_pub.s.ang = s.ang;
  s_pub.s.pitch = s.pitch;
  s_pub.s.active = s.active;
  s_pub.s.px = s.px;
  s_pub.s.py = s.py;
  s_pub.s.pz = s.pz;
  s_pub.s.pang = s.pang;
  s_pub.s.ppitch = s.ppitch;
  s_pub.cursectnum = sect;
  s_pub.m.ok = s_mark.ok;
  s_pub.m.x = s_mark.x;
  s_pub.m.y = s_mark.y;
  s_pub.m.z = s_mark.z;
  s_pub.m.sect = s_mark.sect;

  snap_end(&seq);
  seq_write(seq);
}

static bool pub_read(CameraPub* out) {
  if (!out) return false;
  const unsigned before = seq_read();
  CameraPub tmp = s_pub;
  const unsigned after = seq_read();
  if (!snap_read_ok(before, after)) return false;
  tmp.seq = before;
  *out = tmp;
  return true;
}

bool camera_snapshot(FreecamState* out) {
  if (!out) return false;
  CameraPub p;
  if (!pub_read(&p)) return false;
  *out = p.s;
  return true;
}

bool camera_active() { return s_state.active; }

// "Flying" differs from "the render bracket substituted this frame": hooked_displayrooms can refuse to arm and the
// player's own view is drawn. Render-thread only.
static bool s_renderArmed = false;
bool camera_render_armed() { return s_renderArmed; }

// The smoothratio displayrooms was called with, for mouseaim.cpp's per-frame view override (16.16). Not cleared on the
// way out: that would make the mirror pass look like a tic start.
static long s_lastSmoothratio = 0;
long camera_last_smoothratio() { return s_lastSmoothratio; }

static bool diag_throttle_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!s_first && (now - s_lastMs) < 3000) return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

// Balance pair: arm count vs restores that put something back. A nonzero difference means a frame was left with the
// camera's position in the player struct.
static unsigned s_armCount = 0;
static unsigned s_releaseCount = 0;
static unsigned s_clampArmCount = 0;
static unsigned s_clampReleaseCount = 0;
static bool s_loggedClampMismatch = false;
// Frames the camera wanted but could not have, by reason.
static unsigned s_refusedUnverified = 0;
static unsigned s_refusedSnapshot = 0;
static unsigned s_refusedNotUs = 0;
static unsigned s_refusedIndex = 0;
static unsigned s_refusedPrecondition = 0;

struct CountedCameraBracket {
  CameraBracket b;
  ~CountedCameraBracket() {
    if (camera_bracket_release(&b)) s_releaseCount++;
    s_renderArmed = false;
  }
};

struct CountedClampBracket {
  CodePatchBracket b;
  ~CountedClampBracket() {
    if (code_patch_bracket_release(&b)) s_clampReleaseCount++;
  }
};

// Light bracket: two data-byte levers plus the getshadefactor patch, counted apart (fullbright works with the camera down).
static unsigned s_lightArmCount = 0, s_lightReleaseCount = 0;
static unsigned s_fogArmCount = 0, s_fogReleaseCount = 0;
static bool s_loggedLightMismatch = false;
static bool s_loggedLeversRefused = false;
static bool s_loggedBglDisableNull = false;
struct CountedLightBracket {
  LightLevers levers;
  CodePatchBracket patch;
  CountedLightBracket() { levers.base = 0; levers.savedNofog = 0; levers.savedTileShades = 0; levers.armed = false; }
  ~CountedLightBracket() {
    const bool hadPatch = patch.armed;
    code_patch_bracket_release(&patch);
    const bool hadLevers = light_levers_release(&levers);
    if (hadPatch || hadLevers) s_lightReleaseCount++;
  }
};

// One player field (visibility), saved, zeroed, restored to the saved value (it varies per level).
struct CountedFogBracket {
  FogBracket b;
  CountedFogBracket() { b.base = 0; b.psRva = 0; b.saved = 0; b.armed = false; }
  ~CountedFogBracket() { if (fog_bracket_release(&b)) s_fogReleaseCount++; }
};

// The original is called exactly once, with the original arguments, on every path; `guard`'s destructor is the only
// restore. getinput CAN run inside the bracket: faketimerhandler (VA 0x004b8184) <- loadtile.
static void __cdecl hooked_displayrooms(short snum, long smoothratio) {
  s_lastSmoothratio = smoothratio;
  // Benign: processinput is reached only from domovethings (xref 0x004d18dc); the rendmode == 0 faketimerhandler calls
  // never fire under Polymost; no SDL pump; the nested write_back_view_fields writes the already-frozen values.
  CountedCameraBracket guard;
  CountedClampBracket clampGuard;
  // Declared last so guards release in reverse of arm order (light, fog, clamp, camera).
  CountedFogBracket fogGuard;
  CountedLightBracket lightGuard;

  FreecamState cam;
  cam.x = 0; cam.y = 0; cam.z = 0; cam.ang = 0; cam.pitch = 0; cam.active = false;
  CameraPub pub;
  const bool haveSnap = pub_read(&pub);
  if (haveSnap) cam = pub.s;

  if (cam.active) {
    if (!engine_verified()) {
      s_refusedUnverified++;
    } else if (!haveSnap) {
      s_refusedSnapshot++;
    } else {
      const int peek = engine_screenpeek();
      const PlayerAim pa = engine_player_aim();
      // The frame has to be ours both ways: screenpeek is the snum the game passed, myconnectindex the local player.
      if (peek < 0 || (int)snum != peek || !pa.valid || pa.index != peek) {
        s_refusedNotUs++;
      } else {
        unsigned psRva = 0;
        if (!camera_ps_rva(peek, &psRva)) {
          s_refusedIndex++;
        } else {
          const long newowner =
              read_field_bytes(g_base, psRva + rva::PS_NEWOWNER, 2);
          const long overShoulder =
              read_field_bytes(g_base, psRva + rva::PS_OVER_SHOULDER_ON, 2);
          if (!camera_preconditions_ok((int)newowner, (int)overShoulder)) {
            s_refusedPrecondition++;
          } else {
            // The engine's horiz is derived from the camera's pitch here only.
            const short* sintable = (const short*)(g_base + rva::SINTABLE);
            CameraFields f;
            f.posx = cam.x;
            f.posy = cam.y;
            f.posz = cam.z;
            f.ang = cam.ang;
            f.horiz = freecam_pitch_to_horiz(cam.pitch, sintable);
            f.oposx = cam.px;
            f.oposy = cam.py;
            f.oposz = cam.pz;
            f.oang = cam.pang;
            f.ohoriz = freecam_pitch_to_horiz(cam.ppitch, sintable);
            f.cursectnum = pub.cursectnum;
            if (camera_bracket_arm(&guard.b, g_base, psRva, f)) {
              s_armCount++;
              s_renderArmed = true;
              // The four clamp sites, armed for the same original call; a mismatch leaves the camera bracket alone, retried every armed frame.
              if (code_patch_bracket_arm(&clampGuard.b, g_base,
                                         DISPLAYROOMS_CLAMP_SITES, DISPLAYROOMS_CLAMP_COUNT)) {
                s_clampArmCount++;
              } else if (!s_loggedClampMismatch) {
                s_loggedClampMismatch = true;
                LOGE("camera: displayrooms clamp sites did not fingerprint-match; "
                     "unrestrained movement stays off for this session and the "
                     "camera keeps clamping to floors, ceilings and pitch as before");
              }
              // Fog removal needs a psRva known to be the local player's.
              if (fog_substitute(s_renderArmed, g_cfg.freecamfog != 0) &&
                  fog_bracket_arm(&fogGuard.b, g_base, psRva)) {
                s_fogArmCount++;
              }
            }
          }
        }
      }
    }
  }

  // Fullbright sits outside the camera branch. All or nothing: data levers without the code patch give a wrong scene
  // (getshadefactor still shades). A failed patch latches the feature off (light.h).
  if (engine_verified() && light_available() &&
      light_wants(g_cfg.fullbright != 0, s_renderArmed, g_cfg.freecamfullbright != 0)) {
    if (light_levers_arm(&lightGuard.levers, g_base)) {
      // nofog=1 only gates future GL_FOG toggles and the HUD leaves GL_FOG on, so call the engine's bglDisable (read
      // fresh: refilled on context recreate) once the patch has armed. __stdcall: an OpenGL APIENTRY.
      typedef void (__stdcall* BglFn)(unsigned);
      const BglFn bglDisable = *(BglFn*)(g_base + rva::BGLDISABLE_PTR_RVA);
      if (!bglDisable) {
        light_levers_release(&lightGuard.levers);
        if (!s_loggedBglDisableNull) {
          s_loggedBglDisableNull = true;
          LOGE("light: bglDisable pointer is null; fullbright skipped this frame");
        }
      } else {
        if (code_patch_bracket_arm(&lightGuard.patch, g_base, GETSHADEFACTOR_SITES, GETSHADEFACTOR_COUNT)) {
          bglDisable(GL_FOG_ENUM);
          s_lightArmCount++;
        } else {
          light_levers_release(&lightGuard.levers);
          light_mark_unavailable();
          if (!s_loggedLightMismatch) {
            s_loggedLightMismatch = true;
            LOGE("light: getshadefactor did not fingerprint-match; fullbright stays off for this session");
          }
        }
      }
    } else if (!s_loggedLeversRefused) {
      s_loggedLeversRefused = true;
      LOGE("light: light_levers_arm refused (null base or already armed); fullbright skipped this frame only");
    }
  }

  if (s_origDisplayRooms) s_origDisplayRooms(snum, smoothratio);

  if (g_cfg.debuglog != 0 && diag_throttle_due()) {
    const int lightArmedNow = lightGuard.levers.armed ? 1 : 0;
    int fcs[FCS_COUNT] = { 0 };
    input_freecam_stats(fcs);
    LOGI("camera: active=%d pos=(%d,%d,%d) ang=%d pitch=%d sect=%d "
         "arms=%u releases=%u diff=%d clamparms=%u clampreleases=%u clampdiff=%d "
         "lightarms=%u lightreleases=%u armedNow=%d fogarms=%u fogreleases=%u; "
         "refused unverified=%u snapshot=%u notus=%u index=%u precondition=%u; "
         "mouse ev=%d echo=%d negx=%d posx=%d echoneg=%d echopos=%d warps=%d lastx=%d recentres=%d; "
         "negy=%d posy=%d lasty=%d pitchcap=%d sens=%d/%d",
         cam.active ? 1 : 0, cam.x, cam.y, cam.z, cam.ang, cam.pitch,
         haveSnap ? pub.cursectnum : -1,
         s_armCount, s_releaseCount, (int)s_armCount - (int)s_releaseCount,
         s_clampArmCount, s_clampReleaseCount, (int)s_clampArmCount - (int)s_clampReleaseCount,
         s_lightArmCount, s_lightReleaseCount, lightArmedNow, s_fogArmCount, s_fogReleaseCount,
         s_refusedUnverified, s_refusedSnapshot, s_refusedNotUs,
         s_refusedIndex, s_refusedPrecondition,
         fcs[FCS_EVENTS], fcs[FCS_ECHOES], fcs[FCS_NEGX], fcs[FCS_POSX],
         fcs[FCS_ECHONEGX], fcs[FCS_ECHOPOSX], fcs[FCS_WARPS], fcs[FCS_LASTX],
         fcs[FCS_RECENTRES], fcs[FCS_NEGY], fcs[FCS_POSY], fcs[FCS_LASTY], s_pitchCap,
         freecam_sens_from_mouseaim(g_cfg.mouseaimon != 0, g_cfg.mouseaimsens,
                                    g_cfg.mouseaimvscale).yawMilli,
         freecam_sens_from_mouseaim(g_cfg.mouseaimon != 0, g_cfg.mouseaimsens,
                                    g_cfg.mouseaimvscale).pitchMilli);
  }

}


static bool s_loggedEnterUnreadable = false;
static unsigned s_toggleLogCount = 0;
static unsigned s_releaseLogCount = 0;

// Coming down is a two-step: the toggle runs before the engine's per-tic update and may write no player state, but
// switching off ends in write_back_view_fields; so it only requests, and camera_release_tic() applies it after the
// original processinput.
static bool s_pendingOff = false;
static const char* s_offReason = "";

static void request_off(const char* why) {
  if (s_pendingOff) return;
  s_pendingOff = true;
  s_offReason = why;
}

void camera_release_tic() {
  if (!s_pendingOff) return;
  s_pendingOff = false;

  s_state.active = false;
  s_mark.ok = 0;
  s_sect = -1;
  s_renderSect = -1;
  // camera_tic is the only drain of accumulated mouse motion and doesn't run on the exit tic.
  input_freecam_discard();
  publish(s_state, -1);

  input_lockout_sync();

  if (s_releaseLogCount < 10) {
    s_releaseLogCount++;
    LOGI("camera: released #%u (%s); the input lockout is handed back",
         s_releaseLogCount, s_offReason);
  }
}

void camera_toggle_tic(int player) {
  const PlayerAim pa = engine_player_aim();

  // Dead-man's switch, ahead of the local-player check: an unreadable player is a state it exists to escape. Also closes
  // the panel path (switching "Free camera" off while flying).
  if (freecam_force_off_rule(s_state.active, g_cfg.freecamon != 0,
                             pa.valid && pa.dead, pa.valid)) {
    request_off(g_cfg.freecamon == 0 ? "the feature was switched off"
                : !pa.valid          ? "the local player could not be read"
                                     : "the player died");
    // Spends the press edge, else the entry rule would bring the camera straight back up.
    (void)input_freecam_key_edge();
    return;
  }

  // The engine calls getinput/processinput per player.
  if (pa.index < 0 || player != pa.index) return;

  // Consumed below the index check so a foreign index can't eat it, acted on or not.
  const bool edge = input_freecam_key_edge();

  if (s_state.active) {
    if (!freecam_exit_rule(true, edge, input_menu_open())) return;
    request_off("the toggle key");
    if (s_toggleLogCount < 10) {
      s_toggleLogCount++;
      LOGI("camera: toggle #%u -> off requested; the release runs after this tic's "
           "processinput", s_toggleLogCount);
    }
    return;
  }

  if (!freecam_enter_rule(g_cfg.freecamon != 0, edge, input_menu_open(),
                          pa.valid && pa.dead, pa.valid))
    return;

  const PlayerPos p = engine_player_pos();
  if (!p.valid) {
    if (!s_loggedEnterUnreadable) {
      s_loggedEnterUnreadable = true;
      LOGE("camera: cannot start, the local player's position could not be read; "
           "the free camera stays off");
    }
    return;
  }
  // Safe: p.valid implies engine_verified().
  const short* sintable = (const short*)(g_base + rva::SINTABLE);
  s_state = freecam_enter(p, pa.ang, pa.horiz, sintable);
  s_sect = p.cursectnum;
  s_renderSect = s_sect;
  s_loggedSectorLost = false;   // re-arm for this flight
  s_transportHold = 0;          // a hold left by the last flight is not this one's
  publish(s_state, s_sect);

  // Safe here (unlike the release): freeze_engage writes only engine globals, and makes the lockout active before this
  // tic's `loc` is read.
  input_lockout_sync();

  if (s_toggleLogCount < 10) {
    s_toggleLogCount++;
    LOGI("camera: toggle #%u -> on at (%d,%d,%d) ang=%d pitch=%d sect=%d",
         s_toggleLogCount, s_state.x, s_state.y, s_state.z,
         s_state.ang, s_state.pitch, s_sect);
  }
}

// The teleport: destination freecam_teleport_plan, permission freecam_teleport_rule, write engine_teleport_player.

// The teleport's one line of feedback: a literal, never owned storage. Volatile: tic clock writes, render clock reads.
static const char* volatile s_notice = 0;
static volatile unsigned    s_noticeMs = 0;
static unsigned    s_teleportLogCount = 0;

static void say(const char* what) {
  if (what == 0 || what[0] == '\0') { s_notice = 0; return; }
  s_notice = what;
  s_noticeMs = GetTickCount();
}

const char* camera_notice() {
  if (!freecam_notice_visible(s_notice != 0, s_noticeMs, GetTickCount())) return 0;
  return s_notice;
}

// Gathers what the teleport needs in call order: ray cast, landing sector walk, slope heights (freecam-accuracy.md F).
// The sector is looked up fresh from the stepped-back point, not s_sect nor hitsect. An unanswered engine call is
// NO_WRITE, not NO_SECTOR.
static TeleportPlan plan_teleport(const FreecamState& cam, int guessSect, const short* sintable) {
  const int horiz = freecam_pitch_to_horiz(cam.pitch, sintable);
  const HitscanDir dir = freecam_hitscan_direction(cam.ang, horiz, sintable);
  int hitsect = -1, hitwall = -1, hitsprite = -1, hitx = 0, hity = 0, hitz = 0;
  const bool haveHit = engine_hitscan(cam.x, cam.y, cam.z, guessSect,
                                      dir.vx, dir.vy, dir.vz,
                                      &hitsect, &hitwall, &hitsprite, &hitx, &hity, &hitz);
  if (!haveHit) {
    TeleportPlan p;
    p.ok = false; p.x = cam.x; p.y = cam.y; p.z = cam.z; p.sect = -1;
    p.reason = TELEPORT_REASON_NO_WRITE;
    return p;
  }

  const TeleportAim aim = freecam_teleport_aim(hitsect, hitwall, hitsprite,
                                               hitx, hity, hitz, dir.vx, dir.vy);
  if (!aim.ok) {
    TeleportPlan p;
    p.ok = false; p.x = aim.x; p.y = aim.y; p.z = aim.z; p.sect = -1;
    p.reason = aim.reason;
    return p;
  }

  int sect = hitsect;
  if (!engine_updatesectorz(aim.x, aim.y, aim.z, &sect)) {
    TeleportPlan p;
    p.ok = false; p.x = aim.x; p.y = aim.y; p.z = aim.z; p.sect = -1;
    p.reason = TELEPORT_REASON_NO_WRITE;
    return p;
  }

  int florz = 0, ceilz = 0;
  if (sect >= 0) {
    if (!engine_florz(sect, aim.x, aim.y, &florz)) {
      TeleportPlan p;
      p.ok = false; p.x = aim.x; p.y = aim.y; p.z = aim.z; p.sect = -1;
      p.reason = TELEPORT_REASON_NO_WRITE;
      return p;
    }
    if (!engine_ceilz(sect, aim.x, aim.y, &ceilz)) {
      TeleportPlan p;
      p.ok = false; p.x = aim.x; p.y = aim.y; p.z = aim.z; p.sect = -1;
      p.reason = TELEPORT_REASON_NO_WRITE;
      return p;
    }
  }
  return freecam_teleport_plan(aim.x, aim.y, sect, florz, ceilz, PHEIGHT);
}

void camera_tic() {
  FreecamInput in;
  input_freecam_feed(&in);
  const bool teleportEdge = input_freecam_teleport_edge();

  if (!s_state.active) return;

  // The panel pauses the camera: its freeze owns the view, so this tic's input is dropped. Republished so a reader that
  // missed the last publish gets a clean copy.
  if (input_menu_open() || s_pendingOff) {
    s_state.px = s_state.x;
    s_state.py = s_state.y;
    s_state.pz = s_state.z;
    s_state.pang = s_state.ang;
    s_state.ppitch = s_state.pitch;
    // s_renderSect, not s_sect: with "Nearest sector" on and out of bounds they diverge.
    publish(s_state, s_renderSect);
    return;
  }

  const short* sintable =
      engine_verified() ? (const short*)(g_base + rva::SINTABLE) : 0;

  // Turn rate derives from the modern aim's, read live. The pitch watch logs a tic whose asked pitch differs from its result.
  const int pitchBefore = s_state.pitch;
  const FreecamSens watchSens = freecam_sens_from_mouseaim(g_cfg.mouseaimon != 0,
                                                           g_cfg.mouseaimsens,
                                                           g_cfg.mouseaimvscale);
  s_state = freecam_step(s_state, in, g_cfg.freecamspeed,
                         freecam_sens_from_mouseaim(g_cfg.mouseaimon != 0,
                                                    g_cfg.mouseaimsens,
                                                    g_cfg.mouseaimvscale),
                         sintable);
  if (sintable) {
    const int cap = freecam_pitch_cap(*(const double*)(g_base + rva::GHALFX),
                                      *(const int*)(g_base + rva::YDIMEN),
                                      *(const int*)(g_base + rva::XDIMENSCALE), sintable);
    s_state.pitch = clamp_int(s_state.pitch, -cap, cap);
    s_pitchCap = cap;
  }
  if (s_pendingPoseSet && s_state.active) {
    s_pendingPoseSet = 0;
    s_state.x = s_pendingPose.x; s_state.y = s_pendingPose.y; s_state.z = s_pendingPose.z;
    s_state.ang = s_pendingPose.ang & (FREECAM_ANG_UNITS - 1);
    s_state.pitch = clamp_int(s_pendingPose.pitch, -s_pitchCap, s_pitchCap);
    s_state.px = s_state.x; s_state.py = s_state.y; s_state.pz = s_state.z;
    s_state.pang = s_state.ang; s_state.ppitch = s_state.pitch;
    s_state.yawRem = 0; s_state.pitchRem = 0;
    LOGI("camera: pose set from the clipboard (%d,%d,%d) ang=%d pitch=%d",
         s_state.x, s_state.y, s_state.z, s_state.ang, s_state.pitch);
  }

  if (g_cfg.freecamdebug != 0 && s_state.active) {
    static int wTics = 0, wWant = 0, wGot = 0, wDy = 0;
    static DWORD wLast = 0;
    wWant += -(int)((long long)in.dy * watchSens.pitchMilli / FREECAM_SENS_MILLI_UNIT);
    wGot  += s_state.pitch - pitchBefore;
    wDy   += in.dy;
    if (++wTics >= 26) {
      const int want = wWant < 0 ? -wWant : wWant;
      const int got  = wGot  < 0 ? -wGot  : wGot;
      const DWORD now = GetTickCount();
      if (want > 40 && got * 4 < want && (now - wLast) >= 5000) {
        wLast = now;
        LOGI("camera: pitch watch -- asked %d got %d over %d tics (dy=%d sens=%d/%d "
             "pitch=%d cap=%d rem=%d)",
             wWant, wGot, wTics, wDy, watchSens.yawMilli, watchSens.pitchMilli,
             s_state.pitch, s_pitchCap, s_state.pitchRem);
      }
      wTics = 0; wWant = 0; wGot = 0; wDy = 0;
    }
  }

  // Sector tracking is mandatory: displayrooms clamps camera z into the reported sector's span (freecam-accuracy.md E1).
  // A failed track keeps the last good sector: updatesectorz reports -1 in solid rock and displayrooms returns early on
  // -1 (black screen).
  int sect = s_sect;
  bool tracked = engine_updatesectorz(s_state.x, s_state.y, s_state.z, &sect) && sect >= 0;
  if (!tracked) {
    // After a level change s_sect can exceed numsectors and be refused; retry once from -1 (a full scan).
    int rescan = -1;
    if (engine_updatesectorz(s_state.x, s_state.y, s_state.z, &rescan) && rescan >= 0) {
      sect = rescan;
      tracked = true;
    }
  }
  // s_renderSect is what the publish hands displayrooms: s_sect seeds the next updatesectorz.
  if (tracked) {
    s_sect = sect;
    s_renderSect = sect;
  } else {
    // Only when updatesectorz finds none: the O(numwalls) search stays off the normal path. Not named `near` (macro).
    const int nearest = (g_cfg.freecamnearest != 0)
                        ? engine_nearest_sector(s_state.x, s_state.y, s_state.z) : -1;
    s_renderSect = (nearest >= 0) ? nearest : s_sect;
    if (!s_loggedSectorLost) {
      s_loggedSectorLost = true;
      LOGI("camera: updatesectorz found no sector at (%d,%d,%d), even after a full "
           "rescan; drawing from sector %d (nearest=%d, held=%d). This is what flying "
           "into solid space looks like and is not an error.",
           s_state.x, s_state.y, s_state.z, s_renderSect, nearest, s_sect);
    }
  }

  // No internal z clamp by design: the four code-patched clamp sites (freecam-accuracy.md G) unrestrain the render.
  // Transports: a paired sector effector 7 carries the player across a teleporter or water surface (movetransports);
  // the camera follows. After the sector track, before the teleport block so a key teleport wins the tic.
  if (s_transportHold > 0) s_transportHold--;
  if (g_cfg.freecamtransport != 0 && s_transportHold == 0 && engine_verified()) {
    for (int i = engine_first_sprite_of_stat(FREECAM_TRANSPORT_STAT); i >= 0;
         i = engine_next_sprite(i)) {
      const int owner = engine_sprite_field_s(i, SPR_OWNER);
      if (owner < 0 || owner == i) continue;
      const int sect = engine_sprite_field_s(i, SPR_SECTNUM);
      if (sect < 0 || sect != s_sect) continue;
      // An unreadable temp_data[4] is not a zero: a failure read as zero would invent a floor teleporter.
      int td4 = 0;
      if (!engine_actor_temp_data(i, 4, &td4)) continue;
      const int ownerSect = engine_sprite_field_s(owner, SPR_SECTNUM);
      FreecamTransport t;
      t.lotag = engine_sector_lotag(sect);   // -1 when unreadable: no case matches
      t.seX = engine_sprite_field_l(i, SPR_X);
      t.seY = engine_sprite_field_l(i, SPR_Y);
      t.seZ = engine_sprite_field_l(i, SPR_Z);
      t.ownerX = engine_sprite_field_l(owner, SPR_X);
      t.ownerY = engine_sprite_field_l(owner, SPR_Y);
      t.ownerZ = engine_sprite_field_l(owner, SPR_Z);
      t.ownerCeilZ = engine_sector_field_l(ownerSect, SEC_CEILINGZ);
      t.ownerFloorZ = engine_sector_field_l(ownerSect, SEC_FLOORZ);
      t.secFloorZ = engine_sector_field_l(sect, SEC_FLOORZ);
      t.secCeilZ = engine_sector_field_l(sect, SEC_CEILINGZ);
      t.tempData4 = td4;
      const FreecamJump jump =
          freecam_transport_jump(t, s_state.x, s_state.y, s_state.z);
      if (!jump.hit) continue;
      s_state.x = jump.x;
      s_state.y = jump.y;
      s_state.z = jump.z;
      // Collapse the previous-tic position onto the landing so no frame interpolates across the jump.
      s_state.px = s_state.x;
      s_state.py = s_state.y;
      s_state.pz = s_state.z;
      // Re-track from the landing; a refusal falls back to the pair's own sector.
      int land = s_sect;
      if (engine_updatesectorz(s_state.x, s_state.y, s_state.z, &land) && land >= 0) {
        s_sect = land;
        s_renderSect = land;
      } else if (ownerSect >= 0) {
        s_sect = ownerSect;
        s_renderSect = ownerSect;
      }
      s_transportHold = FREECAM_TRANSPORT_HOLD_TICS;
      LOGI("camera: transport se=%d lotag=%d -> (%d,%d,%d)", i, t.lotag,
           s_state.x, s_state.y, s_state.z);
      break;
    }
  }

  // Cast ONCE per tic and read by marker and key, so one can't draw a spot and drop the player elsewhere.
  const bool markWanted = g_cfg.freecamteleportmarker != 0;
  const bool keyPressed = freecam_teleport_rule(s_state.active, teleportEdge, input_menu_open());
  if (markWanted || keyPressed) {
    const TeleportPlan plan = plan_teleport(s_state, s_sect, sintable);
    s_mark.ok = (markWanted && plan.ok) ? 1 : 0;
    if (s_mark.ok) {
      s_mark.x = plan.x;
      s_mark.y = plan.y;
      s_mark.z = plan.z;
      s_mark.sect = plan.sect;
    }
    if (keyPressed) {
      const bool wrote = plan.ok && engine_teleport_player(plan);
      if (!plan.ok) {
        say(plan.reason);
      } else if (!wrote) {
        say(TELEPORT_REASON_NO_WRITE);
      } else {
        say(plan.reason);   // "" for a clean landing
      }
      if (s_teleportLogCount < 10) {
        s_teleportLogCount++;
        LOGI("camera: teleport #%u %s at (%d,%d,%d) -> (%d,%d,%d) sect=%d, camera stays up%s%s",
             s_teleportLogCount,
             !plan.ok ? "refused" : wrote ? "done" : "could not write",
             s_state.x, s_state.y, s_state.z, plan.x, plan.y, plan.z, plan.sect,
             freecam_teleport_has_notice(plan) ? "; " : "",
             freecam_teleport_has_notice(plan) ? plan.reason : "");
      }
    }
  } else {
    s_mark.ok = 0;
  }

  publish(s_state, s_renderSect);
}

// The landing marker: this tic's published plan as a translucent column, so the destination shows before the key.
// Render clock, from world_draw_frame's producer slot after the secret volume (last puts it over the panels).
void camera_draw_teleport_marker() {
  if (g_cfg.freecamteleportmarker == 0) return;
  if (!engine_verified() || !world_frame_valid()) return;
  CameraPub p;
  if (!pub_read(&p)) return;
  if (!p.s.active || p.m.ok == 0) return;

  const CamState cam = engine_camera();
  if (!cam.valid) return;
  const GProj g = engine_gproj();
  const short* sintable = (const short*)(g_base + rva::SINTABLE);

  // The plan's z is the EYE height at the landing (florz - PHEIGHT), so the floor is PHEIGHT below it (z grows down).
  // Widened: it arrives through memory this DLL doesn't own.
  const int floorz = clamp_ll_to_int((long long)p.m.z + (long long)PHEIGHT);
  const FreecamMarkBox box = freecam_teleport_box(p.m.x, p.m.y, floorz);

  // The four sides, then the cap. All four corners or nothing per quad (a corner behind the camera skips it).
  for (int i = 0; i < 4; i++) {
    const FreecamMarkCorner& a = box.c[i];
    const FreecamMarkCorner& b = box.c[(i + 1) & 3];
    WorldQuad q;
    if (!world_view_point(cam, g, sintable, a.x, a.y, box.zTop, &q.p[0])) continue;
    if (!world_view_point(cam, g, sintable, b.x, b.y, box.zTop, &q.p[1])) continue;
    if (!world_view_point(cam, g, sintable, b.x, b.y, box.zBottom, &q.p[2])) continue;
    if (!world_view_point(cam, g, sintable, a.x, a.y, box.zBottom, &q.p[3])) continue;
    q.rgba = C_TELEPORT_MARK;
    world_push_quad(q);
  }
  WorldQuad top;
  bool capped = true;
  for (int i = 0; i < 4 && capped; i++)
    capped = world_view_point(cam, g, sintable, box.c[i].x, box.c[i].y, box.zTop, &top.p[i]);
  if (capped) {
    top.rgba = C_TELEPORT_MARK;
    world_push_quad(top);
  }
}

bool camera_install() {
  void* origTarget = 0;
  const bool ok = hook_jmp_thunk(rva::THUNK_DISPLAYROOMS_RVA, rva::THUNK_DISPLAYROOMS_BYTES,
                                 (void*)&hooked_displayrooms, &origTarget);
  // One of twelve thunk fingerprint checks feeding the same aggregate (engine.h): differing bytes mean the binary doesn't
  // match and every other hardcoded RVA is as suspect.
  engine_mark_hook_result(ok);
  if (ok) {
    s_origDisplayRooms = (DisplayRoomsFn)origTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origTarget - g_base);
    LOGI("camera: displayrooms thunk hook installed; recovered original target rva=%08X "
         "(expect %08X)%s",
         origRva, rva::DISPLAYROOMS_BODY_RVA,
         origRva == rva::DISPLAYROOMS_BODY_RVA ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("camera: displayrooms thunk hook failed, the free camera is off for this session; "
         "everything else keeps running and the view stays the player's own");
  }
  return ok;
}
