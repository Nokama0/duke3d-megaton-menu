#include "freecam.h"
#include "mouseaim.h"  // turn rate derives from mouse aim; freecam.h stays free of it to avoid the aim's engine RVAs

// The free camera's arithmetic; nothing here touches the game. camera.cpp keeps the hook, engine reads and publish.

int freecam_pitch_cap(double ghalfx, int ydimen, int xdimenscale, const short* sintable) {
  if (!sintable || ghalfx <= 0.0 || ydimen <= 0 || xdimenscale <= 0) return FREECAM_PITCH_MAX;
  const double tiltMax = 0.9 * ghalfx * ghalfx / ((double)ydimen * 0.5 + 3.0);
  const double dh = tiltMax * 65536.0 / (double)xdimenscale;
  if (dh >= 32000.0) return FREECAM_PITCH_MAX;
  const int cap = freecam_horiz_to_pitch(100 + (int)dh, sintable);
  return clamp_int(cap < 0 ? -cap : cap, FREECAM_PITCH_CAP_MIN, FREECAM_PITCH_MAX);
}

int freecam_speed_clamped(int speedPct) {
  return clamp_int(speedPct, FREECAM_SPEED_MIN, FREECAM_SPEED_MAX);
}

int freecam_speed(int speedPct, bool fast, bool slow) {
  int s = FREECAM_BASE_SPEED * freecam_speed_clamped(speedPct) / 100;
  if (fast) s *= 2;   // both applied means neither wins
  if (slow) s /= 2;
  return s < 1 ? 1 : s;   // never zero
}

// Turn rate derives from mouseaim.h's rate functions so it cannot drift from the aim. Yaw is in angle units, pitch in
// degrees (converted at 2048/360). No FOV pitch match: the camera does not render through the player's view scale.
FreecamSens freecam_sens_from_mouseaim(bool mouseaimOn, int sens, int vscale) {
  if (!mouseaimOn) return freecam_sens_default();
  const double unitsPerDegree = (double)FREECAM_ANG_UNITS / 360.0;
  const double yaw   = mouseaim_yaw_units_per_count(sens);
  const double pitch = mouseaim_pitch_deg_per_count(sens, vscale) * unitsPerDegree;
  FreecamSens out;
  out.yawMilli   = freecam_sens_milli_clamped((int)(yaw   * FREECAM_SENS_MILLI_UNIT + 0.5));
  out.pitchMilli = freecam_sens_milli_clamped((int)(pitch * FREECAM_SENS_MILLI_UNIT + 0.5));
  return out;
}

FreecamState freecam_step(const FreecamState& s, const FreecamInput& in,
                          int speedPct, const FreecamSens& sens, const short* sintable) {
  FreecamState o = s;
  if (!sintable) return o;

  // Scale is milli angle units per count; the sub-unit remainder carries to the next tic. Widened before the multiply
  // (100000 counts at 10000 milli exceeds an int). Truncation keeps left/right symmetric; floor would not.
  const long long yawTotal   = (long long)in.dx * freecam_sens_milli_clamped(sens.yawMilli)   + s.yawRem;
  const long long pitchTotal = (long long)in.dy * freecam_sens_milli_clamped(sens.pitchMilli) + s.pitchRem;
  const int dx = clamp_ll_to_int(yawTotal / FREECAM_SENS_MILLI_UNIT);
  const int dy = clamp_ll_to_int(pitchTotal / FREECAM_SENS_MILLI_UNIT);
  o.yawRem   = (int)(yawTotal   % FREECAM_SENS_MILLI_UNIT);
  o.pitchRem = (int)(pitchTotal % FREECAM_SENS_MILLI_UNIT);

  o.px = s.x; o.py = s.y; o.pz = s.z; o.pang = s.ang; o.ppitch = s.pitch;

  o.ang = (int)(((long long)s.ang + dx) & (long long)(FREECAM_ANG_UNITS - 1));

  // A flick past half a turn makes displayrooms' interpolation go the short way, backwards for a delta this large. When its
  // wrap-resolved delta disagrees with the true one the pair collapses (`pang = ang`) for a hard cut.
  {
    const long long trueDelta = (long long)dx;
    const long long wrapResolved =
        ((o.ang - o.pang + 1024) & (FREECAM_ANG_UNITS - 1)) - 1024;
    if (wrapResolved != trueDelta) o.pang = o.ang;
  }

  o.pitch = clamp_int(clamp_ll_to_int((long long)s.pitch - dy),
                      -FREECAM_PITCH_MAX, FREECAM_PITCH_MAX);

  const int speed = freecam_speed(speedPct, in.fast, in.slow);

  const int fwd = (in.fwd ? 1 : 0) - (in.back ? 1 : 0);
  const int str = (in.right ? 1 : 0) - (in.left ? 1 : 0);
  const int vert = (in.up ? 1 : 0) - (in.down ? 1 : 0);

  // Heading: cos is sintable[(ang+512)&2047], sin is sintable[ang&2047], both x16384. Strafe right is ang+512 (+y is the
  // screen's right, pinned by the projection calibration).
  const int cosA = sintable[(o.ang + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  const int sinA = sintable[o.ang & (FREECAM_ANG_UNITS - 1)];

  // Forward/back tilt with pitch, strafe does not: xy step = v*cos(pitch), z = 16*v*sin(pitch). Reads the POST-step pitch.
  const int cosP = sintable[(o.pitch + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  const int sinP = sintable[o.pitch & (FREECAM_ANG_UNITS - 1)];
  const int fwdXY = clamp_ll_to_int(((long long)speed * cosP) / FREECAM_SIN_SCALE);
  const int fwdZ = clamp_ll_to_int(((long long)16 * speed * sinP) / FREECAM_SIN_SCALE);

  // Widened before the multiply. Division, not a shift: Build's mulscale14 floors and breaks "back is forward's exact
  // negative" off-axis. Strafe keeps unscaled `speed`.
  const long long mx = (long long)fwdXY * (long long)fwd * cosA - (long long)speed * (long long)str * sinA;
  const long long my = (long long)fwdXY * (long long)fwd * sinA + (long long)speed * (long long)str * cosA;
  o.x = clamp_ll_to_int((long long)s.x + mx / FREECAM_SIN_SCALE);
  o.y = clamp_ll_to_int((long long)s.y + my / FREECAM_SIN_SCALE);

  // Up is -z: Build's z grows downward (docs/re/freecam-accuracy.md E2), FREECAM_Z_PER_XY is its 16:1 ratio. Space/C are
  // world up/down, untouched by pitch; `fwd * fwdZ` is the look-direction tilt.
  o.z = clamp_ll_to_int((long long)s.z
                        - (long long)fwd * fwdZ
                        - (long long)vert * speed * FREECAM_Z_PER_XY);
  return o;
}


// Inverse of freecam_pitch_to_horiz: the pitch whose derived horiz is closest. A full outward scan, not a binary search:
// integer division flattens pitches around 0 to one horiz and a binary search lands on the plateau's edge. Ties go to the
// plateau's centre (`+mag` first).
int freecam_horiz_to_pitch(int horiz, const short* sintable) {
  if (!sintable) return 0;
  int bestPitch = 0;
  int bestDist = -1;   // no candidate yet
  for (int mag = 0; mag <= FREECAM_PITCH_MAX; mag++) {
    const int signs = (mag == 0) ? 1 : 2;
    for (int s = 0; s < signs; s++) {
      const int pitch = (s == 0) ? mag : -mag;
      const int h = freecam_pitch_to_horiz(pitch, sintable);
      const int dist = h >= horiz ? h - horiz : horiz - h;
      if (bestDist < 0 || dist < bestDist) {
        bestDist = dist;
        bestPitch = pitch;
      }
    }
  }
  return bestPitch;
}

bool freecam_enter_rule(bool on, bool keyEdge, bool menuOpen, bool dead, bool valid) {
  return on && keyEdge && !menuOpen && !dead && valid;
}

// Not the negation of the entry rule: `on`, `dead` and `valid` are reasons not to start; applying them here made the camera
// impossible to switch off.
bool freecam_exit_rule(bool active, bool keyEdge, bool menuOpen) {
  return active && keyEdge && !menuOpen;
}

bool freecam_force_off_rule(bool active, bool on, bool dead, bool valid) {
  if (!active) return false;
  return !on || dead || !valid;
}

FreecamState freecam_enter(const PlayerPos& p, int ang, int horiz, const short* sintable) {
  FreecamState s;
  s.x = p.posx;
  s.y = p.posy;
  s.z = p.posz;
  s.ang = (int)(((long long)ang) & (long long)(FREECAM_ANG_UNITS - 1));
  // Through the inverse of freecam_pitch_to_horiz, so the view does not jump on the first armed frame.
  s.pitch = clamp_int(freecam_horiz_to_pitch(horiz, sintable), -FREECAM_PITCH_MAX, FREECAM_PITCH_MAX);
  s.active = true;
  s.px = s.x; s.py = s.y; s.pz = s.z; s.pang = s.ang; s.ppitch = s.pitch;
  s.yawRem = 0; s.pitchRem = 0;
  return s;
}


// Deliberately separate from freecam_exit_rule: same terms today, different keys and questions.
bool freecam_teleport_rule(bool active, bool keyEdge, bool menuOpen) {
  return active && keyEdge && !menuOpen;
}

TeleportAim freecam_teleport_aim(int hitsect, int hitwall, int hitsprite,
                                 int hitx, int hity, int hitz,
                                 int vx, int vy) {
  TeleportAim a;
  a.ok = false;
  a.x = hitx;
  a.y = hity;
  a.z = hitz;
  a.reason = TELEPORT_REASON_NONE;

  // hitsect < 0 is hitscan's "ray hit nothing"; closed Build levels do not allow it, but an unverified read must be refused.
  if (hitsect < 0) {
    a.reason = TELEPORT_REASON_NOTHING;
    return a;
  }

  // A wall or sprite hit steps the landing point back so the clip radius does not spawn the player inside the surface.
  const bool stepBack = hitwall >= 0 || hitsprite >= 0;
  int x = hitx, y = hity;
  freecam_teleport_step_back(hitx, hity, vx, vy, stepBack, &x, &y);

  a.ok = true;
  a.x = x;
  a.y = y;
  a.z = hitz;
  return a;
}

TeleportPlan freecam_teleport_plan(int x, int y, int sectFromEngine,
                                   int florz, int ceilz, int eyeHeight) {
  TeleportPlan p;
  p.ok = false;
  p.x = x;
  p.y = y;
  p.z = 0;
  p.sect = sectFromEngine;
  p.reason = TELEPORT_REASON_NONE;

  // updatesectorz answers -1 for a point in solid geometry or off the map; the stepped-back point can drift outside.
  if (sectFromEngine < 0) {
    p.reason = TELEPORT_REASON_NO_SECTOR;
    return p;
  }

  // A refusal, not a clamp: when the room is shorter than eyeHeight + PHEIGHT_MARGIN nothing fits a standing player, and
  // silently relocating the player is wrong for a crosshair-aimed destination.
  if (florz - ceilz < eyeHeight + PHEIGHT_MARGIN) {
    p.reason = TELEPORT_REASON_TIGHT;
    return p;
  }

  p.ok = true;
  p.z = florz - eyeHeight;
  return p;
}
