#include "target.h"
#include "common.h"
#include <math.h>

static bool cstat_shootable(int cstat) {
  return (cstat & SPR_CSTAT_SHOOT_MASK) == SPR_CSTAT_SHOOT_WANT;
}

bool target_hittable_rule(int xrepeat, int cstat) {
  return xrepeat != 0 && cstat_shootable(cstat);
}

bool target_alive_rule(int statnum, bool badguy, int cstat) {
  const bool onActorList = (statnum == TARGET_STAT_ACTOR ||
                            statnum == TARGET_STAT_ZOMBIEACTOR);
  return onActorList && badguy && cstat_shootable(cstat);
}

int target_hp_for_display(int extra) { return extra > 0 ? extra : 0; }

// x/y/z is the live position, bx/by/bz hittype[i]'s previous-tic position; the difference is the per-tic velocity from
// the engine's own mover. Null-safe.
bool target_velocity_from(int x, int y, int z, int bx, int by, int bz, int* vx, int* vy, int* vz) {
  if (!vx || !vy || !vz) return false;
  *vx = x - bx;
  *vy = y - by;
  *vz = z - bz;
  return true;
}

static const int    kUnitsFullCircle = 2048;
static const double kPi              = 3.14159265358979323846;

// atan2(dy, dx) as a Build angle 0..2047 (0 = +x, 512 = +y). `& 2047` on a negative operand is the engine's own wrap
// (exactly mod 2048); one point of truth for target_angular_offset and target_solve.
static int build_angle_from(long long dx, long long dy) {
  double angRad = atan2((double)dy, (double)dx);
  long long araw = (long long)llround(angRad * (kUnitsFullCircle / 2) / kPi);
  return clamp_ll_to_int(araw & (long long)(kUnitsFullCircle - 1));
}

WeaponInfo target_weapon_info(int currWeapon) {
  static const WeaponInfo table[] = {
    { 0,  WC_MELEE,      0,   0 },
    { 1,  WC_HITSCAN,    0,  32 },   // pistol
    { 2,  WC_HITSCAN,    0,  32 },   // shotgun
    { 3,  WC_HITSCAN,    0,  32 },   // chaingun
    { 4,  WC_PROJECTILE, 644, 81 },  // rpg (vel confirmed at VA 0x004f5105)
    { 5,  WC_NONE,       0,   0 },   // pipebomb: lobbed, no aim assist
    // shrinker: fixed vel = 768, pitch multiplier unread; a half-known projectile is worse than none. WC_NONE also keeps
    // aim_tic from publishing a target, so silent aim's 0x9fc row is unreachable until the multiplier is read.
    { 6,  WC_NONE,       0,   0 },
    { 7,  WC_PROJECTILE, 644, 81 },  // devastator
    { 8,  WC_NONE,       0,   0 },   // tripbomb
    { 9,  WC_PROJECTILE, 644, 81 },  // freezer (shares RPG's handler)
    { 10, WC_NONE,       0,   0 },   // handremote
    // expander (Atomic): hitscan, atwith GROWSPARK 0x990 (VA 0x004fe956). The seven shoot(i, 0xa35) calls at VA
    // 0x004fe58c-0x004fe602 are the SHOTGUN's pellets.
    { 11, WC_HITSCAN,    0,  32 },
  };
  const int n = (int)(sizeof(table) / sizeof(table[0]));
  if (currWeapon < 0 || currWeapon >= n) {
    WeaponInfo unknown = { currWeapon, WC_NONE, 0, 0 };
    return unknown;
  }
  return table[currWeapon];
}

// h = yrepeat*tilesizy; sprite height is 4h, so centre (k=2) is mid-body and upper (k=3) three-quarters up. long long
// so a hostile pair cannot overflow int.
int target_aim_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap) {
  long long h = (long long)yrepeat * (long long)tilesizy;
  long long k = (ap == AP_UPPER) ? 3 : 2;
  return clamp_ll_to_int((long long)spriteZ - k * h);
}

int target_angular_offset(int viewAng, int dx, int dy) {
  int a = build_angle_from(dx, dy);
  int d = (a - viewAng) & (kUnitsFullCircle - 1);
  return d > kUnitsFullCircle / 2 ? kUnitsFullCircle - d : d;
}

bool target_candidate_healthy(int extra) { return extra > 0; }

// One tier of the scan: smallest cone offset, ties by distance, or smallest distance for TM_CLOSEST. The cone test is
// INSIDE the tier so a healthy candidate outside the cone cannot block the fallback from an unhealthy one inside it.
static int pick_in_tier(const TargetCandidate* cands, int count, int viewAng,
                        int eyex, int eyey, TargetMode mode, int crosshairConeUnits,
                        bool healthyOnly) {
  int best = -1;
  int bestOffset = 0;
  int bestDist = 0;
  for (int i = 0; i < count; ++i) {
    const TargetCandidate& c = cands[i];
    if (healthyOnly && !target_candidate_healthy(c.extra)) continue;
    if (mode == TM_CROSSHAIR) {
      int off = target_angular_offset(viewAng, c.x - eyex, c.y - eyey);
      if (off > crosshairConeUnits) continue;
      if (best == -1 || off < bestOffset || (off == bestOffset && c.dist2d < bestDist)) {
        best = c.index;
        bestOffset = off;
        bestDist = c.dist2d;
      }
    } else {
      if (best == -1 || c.dist2d < bestDist) {
        best = c.index;
        bestDist = c.dist2d;
      }
    }
  }
  return best;
}

int target_pick(const TargetCandidate* cands, int count, int viewAng, int eyex, int eyey, TargetMode mode, int crosshairConeUnits) {
  if (!cands || count <= 0) return -1;
  const int healthy = pick_in_tier(cands, count, viewAng, eyex, eyey, mode,
                                   crosshairConeUnits, true);
  if (healthy >= 0) return healthy;
  return pick_in_tier(cands, count, viewAng, eyex, eyey, mode,
                      crosshairConeUnits, false);
}

// Two fixed iterations of t = dist2d(P + V*t, S) / vel (t0 from the straight-line distance, t1 from the t0 lead
// point), bounded by construction. vel <= 0 (hitscan) returns P unchanged, guarding the only division. The distance is
// 2D, never 3D: vel is xy-units per tic and shoot picks zvel as dz*vel/dist2d so the vertical arrives in the SAME t.
Point3 target_lead(Point3 shooter, Point3 p, int vx, int vy, int vz, int vel) {
  if (vel <= 0) return p;

  double sx = (double)shooter.x, sy = (double)shooter.y;
  double px = (double)p.x, py = (double)p.y, pz = (double)p.z;
  double v = (double)vel;

  double dx0 = px - sx, dy0 = py - sy;
  double t0 = sqrt(dx0 * dx0 + dy0 * dy0) / v;

  double lx0 = px + vx * t0, ly0 = py + vy * t0;
  double dx1 = lx0 - sx, dy1 = ly0 - sy;
  double t1 = sqrt(dx1 * dx1 + dy1 * dy1) / v;

  Point3 out;
  out.x = clamp_double_to_int(px + vx * t1);
  out.y = clamp_double_to_int(py + vy * t1);
  out.z = clamp_double_to_int(pz + vz * t1);
  return out;
}

// The dial between the raw body point and the led one. Clamped here too: an out-of-range value would extrapolate past
// either endpoint. long long throughout since `led - raw` and `* scalePct` can overflow int. Truncates toward zero.
Point3 target_lead_scaled(Point3 raw, Point3 led, int scalePct) {
  const long long pct = (long long)aim_leadscale_clamped(scalePct);
  Point3 out;
  out.x = clamp_ll_to_int((long long)raw.x + ((long long)led.x - raw.x) * pct / 100);
  out.y = clamp_ll_to_int((long long)raw.y + ((long long)led.y - raw.y) * pct / 100);
  out.z = clamp_ll_to_int((long long)raw.z + ((long long)led.z - raw.z) * pct / 100);
  return out;
}

// The one-tic advance (target.h): the shot fired inside processinput leaves from a position the aimbot's solve has not
// seen. The player's step is taken in long long: pos and opos are independent live reads, and INT_MAX - INT_MIN wraps to
// -1 in int. The cap is inclusive: AIM_AHEAD_MAX_STEP for x/y, AIM_AHEAD_MAX_STEP_Z for z (16x finer).
int aim_ahead_step(int pos, int opos, int cap) {
  const long long step = (long long)pos - (long long)opos;
  if (step > (long long)cap) return 0;
  if (step < -(long long)cap) return 0;
  return (int)step;
}

Point3 aim_ahead_origin(Point3 eye, int dx, int dy, int dz) {
  Point3 out;
  out.x = clamp_ll_to_int((long long)eye.x + (long long)dx);
  out.y = clamp_ll_to_int((long long)eye.y + (long long)dy);
  out.z = clamp_ll_to_int((long long)eye.z + (long long)dz);
  return out;
}

Point3 aim_ahead_target(Point3 raw, int svx, int svy, int svz) {
  Point3 out;
  out.x = clamp_ll_to_int((long long)raw.x + (long long)svx);
  out.y = clamp_ll_to_int((long long)raw.y + (long long)svy);
  out.z = clamp_ll_to_int((long long)raw.z + (long long)svz);
  return out;
}

// Wrapped signed yaw error in [-1024, +1023]. A modulo corrected into range, not a `& 2047` mask: `ang` comes from live
// memory and `%` keeps the left operand's sign.
int aim_shot_angerr(int ang, int exact) {
  long long m = ((long long)ang - (long long)exact + 1024) % 2048;
  if (m < 0) m += 2048;
  return (int)(m - 1024);
}

static int ray_sin(int ang, const short* sintable) { return sintable[ang & 2047]; }
static int ray_cos(int ang, const short* sintable) { return sintable[(ang + 512) & 2047]; }

// Widened before the subtraction and the scaling: the difference alone can overflow int, and the 16384 multiply adds 14 bits.
int shot_lateral(int ox, int oy, int ang, int sx, int sy, const short* sintable) {
  if (!sintable) return 0;
  const long long dx = (long long)sx - (long long)ox;
  const long long dy = (long long)sy - (long long)oy;
  return clamp_ll_to_int((dx * ray_sin(ang, sintable) - dy * ray_cos(ang, sintable)) / 16384);
}

int shot_forward(int ox, int oy, int ang, int sx, int sy, const short* sintable) {
  if (!sintable) return 0;
  const long long dx = (long long)sx - (long long)ox;
  const long long dy = (long long)sy - (long long)oy;
  return clamp_ll_to_int((dx * ray_cos(ang, sintable) + dy * ray_sin(ang, sintable)) / 16384);
}

// fwd*vz can overflow on its own (vz ~3.3 million looking straight down), so it is built in long long and narrowed once.
int shot_vertical(int sz, int vz, int fwd, int targetZ) {
  const long long rayZ = (long long)sz + (long long)fwd * (long long)vz / 16384;
  return clamp_ll_to_int((long long)targetZ - rayZ);
}

// Derivation in target.h: the engine's hitscan accepts a face sprite within tilesizx*xrepeat/sqrt(128) of the ray (1000/
// 11314 is the integer 1/sqrt(128)). The product is clamped before the *1000 so a hostile pair saturates; real tiles are
// unaffected.
int shot_hitscan_halfw(int tilesizx, int xrepeat) {
  long long prod = (long long)tilesizx * (long long)xrepeat;
  const long long safeMax = 9223372036854775807LL / 1000;
  const long long safeMin = (-9223372036854775807LL - 1LL) / 1000;
  if (prod > safeMax) prod = safeMax;
  if (prod < safeMin) prod = safeMin;
  return clamp_ll_to_int(prod * 1000 / 11314);
}

// Integer arithmetic (VA 0x004f424f hitscan, 0x004f520f projectile), not floating point: the RPG fixture pins 644/81
// truncating to 7 (7.95 in double), expecting 93. `dist` is the EXACT 2D distance (sqrt); shoot uses `ldist` (Build's
// octagonal approximation, ~3.9% short on-axis), so this is not byte-exact. Guarded against dist==0.
AimSolution target_solve(Point3 eye, Point3 aim, int horizoff, const WeaponInfo& w) {
  AimSolution r;
  r.ang = 0;
  r.horiz = 0;
  r.valid = false;

  long long dx = (long long)aim.x - (long long)eye.x;
  long long dy = (long long)aim.y - (long long)eye.y;
  long long dz = (long long)aim.z - (long long)eye.z;

  long long dist = (long long)clamp_double_to_int(sqrt((double)(dx * dx + dy * dy)));
  if (dist < 1) return r;

  r.ang = build_angle_from(dx, dy);

  long long zvel, horiz;
  if (w.cls == WC_PROJECTILE) {
    if (w.horizMul == 0) return r;
    zvel = (dz * (long long)w.vel) / dist;
    horiz = 100 - horizoff - zvel / w.horizMul;
  } else {
    // Hitscan (and the default for melee/none): horiz = 100 - horizoff - 8*dz/dist as the engine computes it. `dz * 256`
    // not `dz << 8`: shifting a negative signed operand is UB pre-C++20.
    zvel = (dz * 256) / dist;
    horiz = 100 - horizoff - zvel / 32;
  }
  r.horiz = clamp_ll_to_int(horiz);
  r.valid = true;
  return r;
}

// delta wraps the short way round; step is the fraction of delta NOT damped by smoothPct (0 snaps, 100 applies none),
// folded into 0..2047. A truncated step could stall short forever, so a zero step becomes sign(delta); not at pct == 100,
// which means "never moves".
int target_smooth_ang(int current, int desired, int smoothPct) {
  int pct = clamp_int(smoothPct, 0, 100);
  int delta = ((desired - current + kUnitsFullCircle / 2) & (kUnitsFullCircle - 1)) - kUnitsFullCircle / 2;
  int step = delta * (100 - pct) / 100;
  if (step == 0 && delta != 0 && pct < 100) step = (delta > 0) ? 1 : -1;
  return (current + step) & (kUnitsFullCircle - 1);
}

int target_smooth_horiz(int current, int desired, int smoothPct) {
  int pct = clamp_int(smoothPct, 0, 100);
  int delta = desired - current;
  int step = delta * (100 - pct) / 100;
  if (step == 0 && delta != 0 && pct < 100) step = (delta > 0) ? 1 : -1;
  return current + step;
}

bool target_within_redirect(int currentAng, int desiredAng, int maxUnits) {
  int delta = ((desiredAng - currentAng + kUnitsFullCircle / 2) & (kUnitsFullCircle - 1)) - kUnitsFullCircle / 2;
  int absDelta = delta < 0 ? -delta : delta;
  return absDelta <= maxUnits;
}

int target_degrees_to_units(int degrees) {
  long long v = ((long long)degrees * kUnitsFullCircle + 180) / 360;
  return clamp_ll_to_int(v);
}

// The four absolute refusals come first (panel owning input, dead player, untrusted read, viewscreen camera), each
// enough alone; only then does hold mode get a say.
bool aim_should_run(bool on, bool hold, bool buttonDown, bool menuOpen, bool dead, bool valid,
                    bool cameraOn) {
  if (!on || menuOpen || dead || !valid || cameraOn) return false;
  if (hold) return buttonDown;
  return true;
}

// A plain conjunction, so the suite can prove which six terms are consulted. `los` keeps an enemy behind a wall from
// getting the marker; `targetable` keeps a body mid-death-animation from stealing the aim point from a live enemy.
bool aim_candidate_ok(bool badguy, bool alive, bool inRange, bool los, bool reachable,
                      bool targetable) {
  return badguy && alive && inRange && los && reachable && targetable;
}

// Widened to double before the squares: dx*dx in int overflows well inside a legal map. Saturates to INT_MAX rather than
// wrapping negative through the draw-distance cull.
int aim_dist2d(int dx, int dy) {
  const double x = (double)dx, y = (double)dy;
  return clamp_double_to_int(sqrt(x * x + y * y));
}

int aim_cone_clamped(int degrees) { return clamp_int(degrees, AIM_CONE_MIN, AIM_CONE_MAX); }
int aim_smooth_clamped(int pct)   { return clamp_int(pct, AIM_SMOOTH_MIN, AIM_SMOOTH_MAX); }
int aim_leadscale_clamped(int pct) { return clamp_int(pct, AIM_LEADSCALE_MIN, AIM_LEADSCALE_MAX); }
int aim_range_clamped(int units)  { return clamp_int(units, AIM_RANGE_MIN, AIM_RANGE_MAX); }

// The seqlock. Both writers RESTORE parity rather than flip it: `(v + 1) | 1` lands on the next odd value from an even
// one and stays odd from an odd one, so a counter left odd by a writer that never finished is repaired by the next
// cycle (a bare `++` would invert it). Unsigned wrap holds across 0xFFFFFFFF -> 0; null-safe.
void snap_begin(unsigned* seq) { if (seq) *seq = (*seq + 1u) | 1u; }
void snap_end(unsigned* seq)   { if (seq) *seq = (*seq + 1u) & ~1u; }

bool snap_read_ok(unsigned before, unsigned after) {
  return before == after && (before & 1u) == 0u;
}

// Room left -> next free slot. Full -> the slot holding the largest dist2d, only if the newcomer is strictly nearer (a
// tie never churns). `count >= cap`, not `==`, so an over-counted caller still replaces instead of appending past the end.
int aim_keep_index(const TargetCandidate* cands, int count, int cap, int dist2d) {
  if (!cands || cap <= 0 || count < 0) return -1;
  if (count < cap) return count;
  int worst = 0;
  for (int k = 1; k < cap; k++)
    if (cands[k].dist2d > cands[worst].dist2d) worst = k;
  return (dist2d < cands[worst].dist2d) ? worst : -1;
}

bool aim_weapon_usable(WeaponClass cls) {
  return cls == WC_HITSCAN || cls == WC_PROJECTILE;
}

TargetMode aim_mode_from_config(int aimtarget) {
  return (aimtarget == 1) ? TM_CLOSEST : TM_CROSSHAIR;
}
AimPoint aim_point_from_config(int aimpoint) {
  return (aimpoint == 1) ? AP_UPPER : AP_CENTRE;
}

int aim_horiz_clamped(int horiz) { return clamp_int(horiz, AIM_HORIZ_MIN, AIM_HORIZ_MAX); }

// One tic of the view move. Its only decision beyond target_smooth_* is WHERE the envelope applies: both pitch
// endpoints, before any subtraction (an unbounded raw read could overflow inside target_smooth_horiz). No third clamp on
// the result: with both endpoints inside the envelope the smoothing cannot leave the interval, pinned by a sweep.
bool aim_next_view(int curAng, int curHoriz, int wantAng, int wantHoriz, int smoothPct,
                   int* outAng, int* outHoriz) {
  if (!outAng || !outHoriz) return false;
  *outAng = target_smooth_ang(curAng, wantAng, smoothPct);
  *outHoriz = target_smooth_horiz(aim_horiz_clamped(curHoriz), aim_horiz_clamped(wantHoriz), smoothPct);
  return true;
}

bool aim_vel_spike(int vx, int vy, int vz) {
  const double x = (double)vx, y = (double)vy;
  const double step = sqrt(x * x + y * y);
  if (step > (double)AIM_VEL_MAX_STEP) return true;
  const long long z = (long long)vz;
  return (z > (long long)AIM_VEL_MAX_FALL) || (z < -(long long)AIM_VEL_MAX_FALL);
}

void aim_vel_reset(VelSmooth* s) {
  if (!s) return;
  s->index = -1;
  s->picnum = 0;
  s->samples = 0;
  s->qx = 0; s->qy = 0; s->qz = 0;
}

// One axis of the EMA in a scaled domain: q moves alpha (NUM/DEN) of the way to the sample. The scale stops a small
// delta truncating to zero; int suffices since an accepted sample is bounded by AIM_VEL_MAX_FALL (6144).
static int vel_fold(int q, int sample) {
  return q + ((sample * AIM_VEL_SCALE - q) * AIM_VEL_EMA_NUM) / AIM_VEL_EMA_DEN;
}

bool aim_vel_update(VelSmooth* s, int index, int picnum, int vx, int vy, int vz,
                    int* outvx, int* outvy, int* outvz) {
  if (!s || !outvx || !outvy || !outvz) return false;

  const bool spike = aim_vel_spike(vx, vy, vz);

  if (s->index != index || s->picnum != picnum) {
    // A different actor: the old history is not this one's. Seeding with the sample makes the first tic on a new target
    // already lead, except for a spike, which seeds zero rather than a teleport.
    aim_vel_reset(s);
    s->index = index;
    s->picnum = picnum;
    if (!spike) {
      s->qx = vx * AIM_VEL_SCALE;
      s->qy = vy * AIM_VEL_SCALE;
      s->qz = vz * AIM_VEL_SCALE;
      s->samples = 1;
    }
  } else if (!spike) {
    s->qx = vel_fold(s->qx, vx);
    s->qy = vel_fold(s->qy, vy);
    s->qz = vel_fold(s->qz, vz);
    // Saturating at the window: past it the count says only "settled"; a growing counter could overflow in a hook body.
    if (s->samples < AIM_VEL_EMA_TICS) s->samples++;
  }
  // A spike on an established target changes nothing: the average is returned and `samples` does not advance, so a
  // stream of teleports cannot age the state.

  *outvx = s->qx / AIM_VEL_SCALE;
  *outvy = s->qy / AIM_VEL_SCALE;
  *outvz = s->qz / AIM_VEL_SCALE;
  return true;
}

bool vel_smooth_read(const VelSmooth* s, int index, int picnum,
                     int* outvx, int* outvy, int* outvz) {
  if (!s || !outvx || !outvy || !outvz) return false;
  if (s->index != index || s->picnum != picnum) return false;
  *outvx = s->qx / AIM_VEL_SCALE;
  *outvy = s->qy / AIM_VEL_SCALE;
  *outvz = s->qz / AIM_VEL_SCALE;
  return true;
}

int aim_lead_vel(int weaponVel, int aimlead) { return aimlead != 0 ? weaponVel : 0; }

int aim_shot_origin_z(int posz, int pyoff) {
  return clamp_ll_to_int((long long)posz + (long long)pyoff + (long long)AIM_SHOT_Z_BIAS);
}

// One rung of the ladder, lowest first: configured aim point, upper body, head (rationale in target.h). Indexed so
// silent.cpp's live re-solve need not re-walk cansee; aim_probe_zs builds its ladder from three calls here.
int target_probe_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int rung) {
  const long long h = (long long)yrepeat * (long long)tilesizy;
  switch (rung) {
    case 1: return clamp_ll_to_int((long long)spriteZ - 3 * h);
    case 2: return clamp_ll_to_int((long long)spriteZ - 4 * h + h / 4);
    // Rung 0, and the fallback for anything out of range (including the "no probe visible" sentinel -1).
    default: return target_aim_z(spriteZ, yrepeat, tilesizy, ap);
  }
}

int aim_probe_zs(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int* out, int cap) {
  if (!out || cap < 1) return 0;
  const int ladder[AIM_PROBE_MAX] = {
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 0),
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 1),
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 2),
  };
  int n = 0;
  for (int i = 0; i < AIM_PROBE_MAX && n < cap; i++) {
    bool dup = false;
    for (int k = 0; k < n; k++) if (out[k] == ladder[i]) { dup = true; break; }
    if (!dup) out[n++] = ladder[i];
  }
  return n;
}

int aim_first_visible(const bool* visible, int count) {
  if (!visible || count <= 0) return -1;
  for (int i = 0; i < count; i++) if (visible[i]) return i;
  return -1;
}

bool aim_horiz_reachable(int horiz) {
  return horiz >= AIM_HORIZ_MIN && horiz <= AIM_HORIZ_MAX;
}

AimSolutionChoice aim_choose_solution(bool ledValid, int ledHoriz, bool rawValid, int rawHoriz) {
  if (ledValid && aim_horiz_reachable(ledHoriz)) return ASC_LED;
  if (rawValid && aim_horiz_reachable(rawHoriz)) return ASC_RAW;
  return ASC_NONE;
}

bool aim_acquire_should_run(bool aimbotWants, bool silentOn, bool menuOpen,
                            bool dead, bool valid, bool cameraOn) {
  if (menuOpen || dead || !valid || cameraOn) return false;
  return aimbotWants || silentOn;
}

int silent_maxangle_clamped(int degrees) {
  return clamp_int(degrees, SILENT_MAXANGLE_MIN, SILENT_MAXANGLE_MAX);
}

// A set matched exactly, never a range: 0x9d9 (knee) sits just below 0x9e8, so a range would pull melee in with the
// chaingun. One entry per aimed-shot player weapon; VAs in docs/re/targeting.md section D.
bool silent_atwith_is_player_shot(int atwith) {
  switch (atwith) {
    case 0xa23:   // SHOTSPARK1: pistol
    case 0xa35:   // SHOTGUN: x7
    case 0x9e8:   // CHAINGUN
    case 0xa2d:   // RPG: rpg and devastator
    // Inert today: the shrinker is WC_NONE, so aim_tic never publishes a target while it is in hand; live once the pitch
    // multiplier is read.
    case 0x9fc:   // SHRINKSPARK
    case 0x669:   // FREEZEBLAST
    case 0x990:   // GROWSPARK: expander
      return true;
    default:
      return false;
  }
}

bool silent_snapshot_usable(int targetIndex, int solvedAng, int solvedHoriz) {
  if (targetIndex < 0) return false;
  if (solvedAng < 0 || solvedAng >= kUnitsFullCircle) return false;
  return aim_horiz_reachable(solvedHoriz);
}

bool silent_decide(bool on, bool isLocalPlayerSprite, bool atwithIsPlayerShot,
                   bool haveTarget, bool los, int curAng, int wantAng, int maxUnits) {
  return on
      && isLocalPlayerSprite
      && atwithIsPlayerShot
      && haveTarget
      && los
      && target_within_redirect(curAng, wantAng, maxUnits);
}

// The silent-aim live re-solve: the substituted pair was one tic old (hooked_shoot runs before aim_tic publishes this
// tic's solve). Acquisition stays the snapshot's; only the final angle is recomputed from where the target is at the shot.
SilentResolve silent_resolve(Point3 eye, int horizoff, const WeaponInfo& w,
                             int snapAng, int snapHoriz,
                             Point3 liveRaw, int vx, int vy, int vz,
                             int leadVel, int leadScalePct,
                             int curAng, int maxRedirectUnits) {
  SilentResolve r;
  r.ang = snapAng; r.horiz = snapHoriz; r.valid = true; r.live = false;
  r.coneRejected = false;
  // The same calls, order and scale the aimbot makes, so redirect and calibration marker cannot disagree on the lead point.
  const Point3 led = target_lead(eye, liveRaw, vx, vy, vz, leadVel);
  const Point3 aim = target_lead_scaled(liveRaw, led, leadScalePct);
  const AimSolution sol = target_solve(eye, aim, horizoff, w);
  if (!sol.valid || !aim_horiz_reachable(sol.horiz)) return r;
  // Re-apply the redirect cone silent_decide gated on, or a close, fast target's live angle could exceed "Max redirect".
  if (!target_within_redirect(curAng, sol.ang, maxRedirectUnits)) {
    r.coneRejected = true;
    return r;
  }
  r.ang = sol.ang; r.horiz = sol.horiz; r.live = true;
  return r;
}

// The engine's not-falling test (AIM_FALL_FLOOR_OFFSET, target.h). Build z points down, so at-or-below floorz-margin
// means standing on the floor.
bool aim_actor_grounded(int z, int floorz, int margin) {
  return (long long)z >= (long long)floorz - (long long)margin;
}

// A grounded actor contributes no vertical velocity (its measured vz was its gravity-and-snap cycle); anything above
// its floor keeps its measured vz, so a flying RECON loses nothing.
int aim_grounded_vz(int z, int floorz, int vz, int margin) {
  return aim_actor_grounded(z, floorz, margin) ? 0 : vz;
}

bool aim_throttle_due(unsigned nowMs, unsigned lastMs, bool first, unsigned periodMs) {
  if (first) return true;
  return (unsigned)(nowMs - lastMs) >= periodMs;
}

bool aim_lost_should_log(bool presentLast, bool presentNow, bool picnumSame, int xrepeat,
                         bool alreadyLoggedThisWindow) {
  return presentLast && !presentNow && picnumSame && xrepeat != 0 && !alreadyLoggedThisWindow;
}

// The presence set must not outlive its level. OR, not AND: either signal alone suffices, covering different failure
// modes (target.h). Signature first: two integer compares settle the common case without the clock.
bool aim_presence_reset(int prevPlayerSprite, int prevNumsectors,
                        int playerSprite, int numsectors,
                        unsigned nowMs, unsigned lastMs, unsigned maxGapMs) {
  if (prevPlayerSprite != playerSprite) return true;
  if (prevNumsectors != numsectors) return true;

  return aim_throttle_due(nowMs, lastMs, false, maxGapMs);
}
