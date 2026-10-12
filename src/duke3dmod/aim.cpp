#include "aim.h"
#include "target.h"
#include "engine.h"
#include "config.h"
#include "input.h"
#include "tbot.h"   // tbot_ray, triggerbot's last-tic answer
#include "actorstate.h"
#include "project.h"
#include "hooks.h"
#include "wallclock.h"
#include "trigger.h"
#include "log.h"
#include <windows.h>

// One tic's target acquisition (two-clock contract: aim.h). Decisions are pure functions in target.h; this file cannot
// link into test_host (live sprite memory, the engine's cansee).

// Published snapshot; `seq` is the seqlock counter. All fifteen members are written out in publish()/publish_empty():
// aggregate init shifts later values silently when a field is added mid-struct.
static AimSnapshot s_pub = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 0u };

static unsigned seq_read() { return *(volatile unsigned*)&s_pub.seq; }
static void seq_write(unsigned v) { *(volatile unsigned*)&s_pub.seq = v; }

// Selected target's smoothed velocity: one slot; switching or losing the target resets it. Raw per-tic velocity is
// bursty and would flicker the lead point.
static VelSmooth s_vel = { -1, 0, 0, 0, 0, 0 };

// Whether the aimbot's write landed on the last tic, gating aim_shot_diag: acquisition can publish while aim_apply
// refuses, and the instrument would measure the player's own error. The shot fires before this tic's aim_apply.
static bool s_aimWroteLastTic = false;

bool aim_view_held() { return s_aimWroteLastTic; }

static bool s_viewHeldThisTic = false;
void aim_view_hold_note(bool held) { s_viewHeldThisTic = held; }

// Lost-enemy instrument (an enemy once vanished from ESP and assist, unlogged; cause was a health filter, target.h).
// Presence = the tic walk reached the index as a live enemy by actor_state_shown. Three bitsets plus a picnum table
// (~38 KB); the per-tic scan runs only with debuglog on.
static const int LOST_BITS_BYTES = rva::MAX_SPRITES / 8;   // 2048
static unsigned char s_seenPrev[LOST_BITS_BYTES];
static unsigned char s_seenNow[LOST_BITS_BYTES];
static unsigned char s_lostReported[LOST_BITS_BYTES];
static short s_lastPicnum[rva::MAX_SPRITES];
static bool s_lostHavePrev = false;

// World the presence set belongs to, discarded on change (aim_presence_reset, target.h).
static int s_lostPrevPlayerSprite = -1;
static int s_lostPrevNumsectors = -1;
static unsigned s_lostPrevMs = 0;

static bool bit_get(const unsigned char* bits, int i) {
  return (bits[(unsigned)i >> 3] & (unsigned char)(1u << ((unsigned)i & 7u))) != 0;
}
static void bit_set(unsigned char* bits, int i) {
  bits[(unsigned)i >> 3] |= (unsigned char)(1u << ((unsigned)i & 7u));
}

// Own reporting window so the two throttles cannot starve each other.
static bool lost_window_due() {
  static DWORD s_lastMs = 0;
  static bool s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, AIM_LOST_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

static void lost_enemy_scan() {
  const bool windowTurned = lost_window_due();
  if (windowTurned) {
    for (int b = 0; b < LOST_BITS_BYTES; b++) s_lostReported[b] = 0;
  }

  if (s_lostHavePrev) {
    for (int b = 0; b < LOST_BITS_BYTES; b++) {
      const unsigned char lost = (unsigned char)(s_seenPrev[b] & ~s_seenNow[b]);
      if (!lost) continue;
      for (int k = 0; k < 8; k++) {
        if ((lost & (unsigned char)(1u << k)) == 0) continue;
        const int i = b * 8 + k;
        const int picnum = engine_sprite_field_s(i, SPR_PICNUM);
        const int xrepeat = engine_sprite_field_u8(i, SPR_XREPEAT);
        if (!aim_lost_should_log(true, false, picnum == (int)s_lastPicnum[i], xrepeat,
                                 bit_get(s_lostReported, i)))
          continue;
        bit_set(s_lostReported, i);
        LOGI("lost enemy: index=%d picnum=%d extra=%d cstat=0x%04X statnum=%d badguy=%d "
             "sectnum=%d xrepeat=%d pal=%d",
             i, picnum,
             engine_sprite_field_s(i, SPR_EXTRA),
             (unsigned)(engine_sprite_field_s(i, SPR_CSTAT) & 0xFFFF),
             engine_sprite_field_s(i, SPR_STATNUM),
             engine_is_badguy(i) ? 1 : 0,
             engine_sprite_field_s(i, SPR_SECTNUM),
             xrepeat,
             engine_sprite_field_u8(i, SPR_PAL));
      }
    }
  }

  for (int b = 0; b < LOST_BITS_BYTES; b++) s_seenPrev[b] = s_seenNow[b];
  s_lostHavePrev = true;
}

static bool aim_diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, AIM_DIAG_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

static void publish(const AimSnapshot& s) {
  unsigned seq = seq_read();
  snap_begin(&seq);
  seq_write(seq);

  s_pub.targetIndex = s.targetIndex;
  s_pub.targetPicnum = s.targetPicnum;
  s_pub.targetRung = s.targetRung;
  s_pub.aimx = s.aimx;  s_pub.aimy = s.aimy;  s_pub.aimz = s.aimz;
  s_pub.rawx = s.rawx;  s_pub.rawy = s.rawy;  s_pub.rawz = s.rawz;
  s_pub.solvedAng = s.solvedAng;
  s_pub.solvedHoriz = s.solvedHoriz;
  s_pub.leadScale = s.leadScale;
  s_pub.choice = s.choice;
  s_pub.los = s.los;

  snap_end(&seq);
  seq_write(seq);
}

// No target, every other field zeroed. Also resets the velocity smoother: every loss path funnels here and a stale
// average would hand a re-acquisition some other enemy's motion.
static void publish_empty() {
  // All fifteen members in struct order (see s_pub); keep in sync with publish(). targetPicnum 0 beside targetIndex -1
  // is no valid tile.
  AimSnapshot e = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 0u };
  aim_vel_reset(&s_vel);
  s_aimWroteLastTic = false;
  publish(e);
}

void aim_clear() { s_lostHavePrev = false; publish_empty(); }

bool aim_hold_active() {
  if (!g_cfg.aimhold) return true;
  return input_bind_held(g_cfg.aimbind);
}

bool aim_snapshot(AimSnapshot* out) {
  if (!out) return false;
  const unsigned before = seq_read();
  AimSnapshot tmp = s_pub;
  const unsigned after = seq_read();
  if (!snap_read_ok(before, after)) return false;
  tmp.seq = before;
  *out = tmp;
  return true;
}

// The identity refusal ((index, picnum) must match s_vel) is vel_smooth_read's, pinned in target.h.
bool aim_smoothed_velocity(int index, int picnum, int* vx, int* vy, int* vz) {
  return vel_smooth_read(&s_vel, index, picnum, vx, vy, vz);
}

void aim_tic(const PlayerAim& p) {
  // Acquisition's gate, not the aimbot's: aim_should_run's hold-button term would publish empty exactly when silent aim
  // is meant to work (nothing held). Every refusal publishes empty so the marker clears.
  const bool aimbotWants =
      aim_should_run(g_cfg.aimon != 0, g_cfg.aimhold != 0, aim_hold_active(),
                     input_menu_open(), p.dead, p.valid, p.cameraActive);
  if (!aim_acquire_should_run(aimbotWants, g_cfg.silenton != 0,
                              input_menu_open(), p.dead, p.valid, p.cameraActive)) {
    // No walk here; keeping the old set would report every enemy lost when the panel opens.
    s_lostHavePrev = false;
    publish_empty();
    return;
  }

  const WeaponInfo w = target_weapon_info(p.currWeapon);
  if (!aim_weapon_usable(w.cls)) {
    s_lostHavePrev = false;
    publish_empty();
    return;
  }

  const AimPoint ap = aim_point_from_config(g_cfg.aimpoint);
  const TargetMode mode = aim_mode_from_config(g_cfg.aimtarget);

  // The shot leaves from `shoot`'s origin posz + pyoff + 0x400 (VA 0x004f36cc), 1024 below ps[].posz. LOS is tested
  // from here so an enemy visible over a ledge to the eye but not the muzzle is not offered (AIM_SHOT_Z_BIAS).
  const int shotz = aim_shot_origin_z(p.eyez, p.pyoff);

  // One-tic advance computed above the walk because its LOS probes need it: probes cast from the current position
  // cleared door frames a strafing shot then hit (17 of 30 moving misses). See target_lead for why aimleadscale does not scale it.
  Point3 eye;  eye.x = p.eyex;  eye.y = p.eyey;  eye.z = p.eyez;
  const int adx = aim_ahead_step(p.eyex, p.oposx, AIM_AHEAD_MAX_STEP);
  const int ady = aim_ahead_step(p.eyey, p.oposy, AIM_AHEAD_MAX_STEP);
  const int adz = aim_ahead_step(p.eyez, p.oposz, AIM_AHEAD_MAX_STEP_Z);
  const Point3 eyeAhead = aim_ahead_origin(eye, adx, ady, adz);
  const int shotzAhead = clamp_ll_to_int((long long)shotz + (long long)adz);

  TargetCandidate cands[AIM_MAX_CANDIDATES];
  int n = 0;
  int walked = 0, enemies = 0, losPassed = 0;

  const bool lostTracking = (g_cfg.debuglog != 0);

  if (lostTracking) {
    // A level change passes none of the gates above and the tic clock stops during load: the new map's first tic would
    // emit false `lost enemy:` lines. aim_presence_reset decides.
    const unsigned nowMs = (unsigned)GetTickCount();
    const int nsec = engine_numsectors();
    if (s_lostHavePrev &&
        aim_presence_reset(s_lostPrevPlayerSprite, s_lostPrevNumsectors,
                           p.spriteIndex, nsec, nowMs, s_lostPrevMs,
                           AIM_PRESENCE_MAX_GAP_MS)) {
      s_lostHavePrev = false;
    }
    s_lostPrevPlayerSprite = p.spriteIndex;
    s_lostPrevNumsectors = nsec;
    s_lostPrevMs = nowMs;
    for (int b = 0; b < LOST_BITS_BYTES; b++) s_seenNow[b] = 0;
  } else {
    s_lostHavePrev = false;
  }

  // Two sequential walks, never nested: engine.cpp's single-slot per-walk cycle counter is reset by
  // engine_first_sprite_of_stat (see engine.cpp above s_walk).
  const int stats[2] = { STAT_ACTOR, STAT_ZOMBIEACTOR };
  for (int s = 0; s < 2; s++) {
    for (int i = engine_first_sprite_of_stat(stats[s]); i >= 0; i = engine_next_sprite(i)) {
      walked++;

      // `alive` is the state's targetable set (AS_ALIVE or AS_CHOKING), so a dying actor is refused the tic extra goes
      // negative; read through the per-tic cache shared with the ESP, silent aim and the triggerbot.
      const bool badguy = engine_is_badguy(i);
      const ActorState state = actor_state_cached(i);
      const bool alive = actor_state_targetable(state);
      if (badguy) enemies++;

      if (lostTracking) {
        if (alive) bit_set(s_seenNow, i);
        s_lastPicnum[i] = (short)engine_sprite_field_s(i, SPR_PICNUM);
      }

      const int x = engine_sprite_field_l(i, SPR_X);
      const int y = engine_sprite_field_l(i, SPR_Y);
      const int z = engine_sprite_field_l(i, SPR_Z);
      const int dist2d = aim_dist2d(x - p.eyex, y - p.eyey);
      const bool inRange = aim_within_range(dist2d, g_cfg.aimrange);

      const int picnum  = engine_sprite_field_s(i, SPR_PICNUM);
      const int yrepeat = engine_sprite_field_u8(i, SPR_YREPEAT);
      const int sectnum = engine_sprite_field_s(i, SPR_SECTNUM);
      const int extra = engine_sprite_field_s(i, SPR_EXTRA);

      // Up to three body points (aim_probe_zs, lowest-first, deduped): one centre probe fails for an enemy showing only
      // head and shoulders over cover.
      int probeZ[AIM_PROBE_MAX];
      const int probes = aim_probe_zs(z, yrepeat, engine_tilesizy(picnum), ap,
                                      probeZ, AIM_PROBE_MAX);

      // cansee is the only expensive call, asked only of sprites the cheap terms accepted. It starts from the next tic's
      // MUZZLE (testing from the current spot put 17 of 30 moving misses into door frames); the sector stays p.cursectnum
      // (a failed connect drops the candidate, the safe direction). Pick, cone and range use the current position.
      bool visible[AIM_PROBE_MAX] = { false, false, false };
      const bool worthProbing = alive && inRange && probes > 0;
      if (worthProbing) {
        for (int q = 0; q < probes; q++) {
          visible[q] = engine_cansee(eyeAhead.x, eyeAhead.y, shotzAhead, p.cursectnum,
                                     x, y, probeZ[q], sectnum);
          if (visible[q]) break;
        }
      }
      const int firstSeen = aim_first_visible(visible, probes);
      const bool los = firstSeen >= 0;
      if (los) losPassed++;

      const int aimz = los ? probeZ[firstSeen] : 0;

      // Which fixed rung produced `aimz`, so silent.cpp's re-solve can rebuild it. firstSeen indexes the DEDUPED ladder
      // (AP_UPPER makes rungs 0/1 equal), so the first matching fixed rung is searched.
      int aimzRung = 0;
      if (los) {
        for (int rk = 0; rk < AIM_PROBE_MAX; rk++) {
          if (target_probe_z(z, yrepeat, engine_tilesizy(picnum), ap, rk) == aimz) {
            aimzRung = rk;
            break;
          }
        }
      }

      // A solved pitch outside the engine's clamp is no shot from here (a wall turret at jetpack height); a candidate
      // term, so the pick moves on. Solved on the raw point; re-checked after the lead.
      bool reachable = false;
      if (los) {
        Point3 pe;  pe.x = p.eyex;  pe.y = p.eyey;  pe.z = p.eyez;
        Point3 pa;  pa.x = x;       pa.y = y;       pa.z = aimz;
        const AimSolution probeSol = target_solve(pe, pa, p.horizoff, w);
        reachable = probeSol.valid && aim_horiz_reachable(probeSol.horiz);
      }

      // The sixth term collapses into the state (the old alive+hittable and dying rules could disagree). A separate
      // parameter so a widened aim_candidate_ok forces every call site to pass it.
      const bool targetable = alive;

      if (!aim_candidate_ok(badguy, alive, inRange, los, reachable, targetable)) continue;

      TargetCandidate c;
      c.index = i;
      c.picnum = picnum;
      c.x = x; c.y = y; c.z = z;
      c.vx = 0; c.vy = 0; c.vz = 0;
      engine_sprite_velocity(i, &c.vx, &c.vy, &c.vz);

      // Vertical velocity is dropped for a grounded actor: z - bposz is not vertical speed (moveactors records bpos
      // before the think), so a standing actor measures its gravity cycle (+272..+320/tic: rockets hit the floor).
      c.floorz = 0;
      c.grounded = false;
      int actorFloorz = 0;
      if (engine_sprite_floorz(i, &actorFloorz)) {
        c.floorz = actorFloorz;
        c.grounded = aim_actor_grounded(z, actorFloorz, AIM_GROUND_MARGIN);
        c.vz = aim_grounded_vz(z, actorFloorz, c.vz, AIM_GROUND_MARGIN);
      }

      c.aimz = aimz;
      c.aimzRung = aimzRung;
      c.sectnum = sectnum;
      c.dist2d = dist2d;
      c.extra = extra;

      const int slot = aim_keep_index(cands, n, AIM_MAX_CANDIDATES, c.dist2d);
      if (slot < 0) continue;
      cands[slot] = c;
      if (slot == n) n++;
    }
  }

  if (lostTracking) lost_enemy_scan();

  const int coneUnits = target_degrees_to_units(aim_cone_clamped(g_cfg.aimcone));
  const int best = target_pick(cands, n, p.ang, p.eyex, p.eyey, mode, coneUnits);
  const TargetCandidate* sel = nullptr;
  for (int k = 0; k < n; k++) {
    if (cands[k].index == best) { sel = &cands[k]; break; }
  }
  if (best < 0 || !sel) {
    if (g_cfg.debuglog != 0 && aim_diag_due())
      LOGI("aim diag: no target, walked=%d enemies=%d los=%d cands=%d weapon=%d cls=%d cone=%d",
           walked, enemies, losPassed, n, p.currWeapon, (int)w.cls, coneUnits);
    publish_empty();
    return;
  }

  Point3 raw;  raw.x = sel->x;  raw.y = sel->y;  raw.z = sel->aimz;

  // The lead solver gets a smoothed velocity (a six-tic average, teleports dropped), not this tic's burst; a failed
  // update leaves zeroes, making the lead a no-op.
  int svx = 0, svy = 0, svz = 0;
  aim_vel_update(&s_vel, sel->index, sel->picnum, sel->vx, sel->vy, sel->vz,
                 &svx, &svy, &svz);

  // Both ends of the solve move one tic forward: the shot fires next tic, after clipmove (VA 0x004fcf4d) and
  // moveactors, so solving from `eye`/`raw` aims from old positions (degrees of yaw). Not scaled by aimleadscale: that
  // dial backs off the flight-time guess for burst-and-stop actors, this is a latency the engine's order guarantees.
  const Point3 rawAhead = aim_ahead_target(raw, svx, svy, svz);

  const int leadVel = aim_lead_vel(w.vel, g_cfg.aimlead);
  const Point3 full = target_lead(eyeAhead, rawAhead, svx, svy, svz, leadVel);

  // A dial, not a switch: the full time-of-flight solve overshoots RPG shots at burst-and-stop actors; aimleadscale
  // moves the aim point that percentage from the body point toward the led one (0 returns rawAhead).
  const int leadScale = aim_leadscale_clamped(g_cfg.aimleadscale);
  const Point3 led = target_lead_scaled(rawAhead, full, leadScale);
  const AimSolution sol = target_solve(eyeAhead, led, p.horizoff, w);

  // invalid = aim point and eye coincide, unreachable = a pitch the engine would clamp. The walk proved the raw point
  // reachable, so an out-of-envelope led point falls back to it (aim_choose_solution); the fallback equals aimlead=0.
  AimSolution rawSol;
  rawSol.ang = 0; rawSol.horiz = 0; rawSol.valid = false;
  const bool needRaw = !(sol.valid && aim_horiz_reachable(sol.horiz));
  if (needRaw) rawSol = target_solve(eyeAhead, rawAhead, p.horizoff, w);

  const AimSolutionChoice choice =
      aim_choose_solution(sol.valid, sol.horiz, rawSol.valid, rawSol.horiz);
  if (choice == ASC_NONE) {
    publish_empty();
    return;
  }
  const bool useLed = (choice == ASC_LED);
  const AimSolution& use = useLed ? sol : rawSol;
  // The point actually aimed at; rawx/y/z below keep the unadvanced point (what the marker draws).
  const Point3& aimPt = useLed ? led : rawAhead;

  AimSnapshot s;
  s.targetIndex = sel->index;
  s.targetPicnum = sel->picnum;
  s.targetRung = sel->aimzRung;
  s.rawx = raw.x;  s.rawy = raw.y;  s.rawz = raw.z;
  s.aimx = aimPt.x;  s.aimy = aimPt.y;  s.aimz = aimPt.z;
  s.solvedAng = use.ang;
  s.solvedHoriz = use.horiz;
  // The solved fields are the led solve, substituted verbatim by silent.cpp; the dial and choice let the redirect log both.
  s.leadScale = leadScale;
  s.choice = (int)choice;
  s.los = true;
  s.seq = 0u;
  publish(s);

  // Angle cross-check: target_solve derives yaw from atan2 in doubles, the engine from getangle's integer table; logged
  // side by side so a convention error (quadrant, sign, off-by-512) shows. Expected within a unit.
  if (g_cfg.debuglog != 0 && aim_diag_due()) {
    // From the advanced origin (what use.ang was solved from); catches convention errors, not the one-tic correction.
    const int dx = aimPt.x - eyeAhead.x;
    const int dy = aimPt.y - eyeAhead.y;
    const int engAng = engine_getangle(dx, dy);
    const int angdiff = ((use.ang - engAng + 1024) & 2047) - 1024;
    const int noAheadAng = target_solve(eye, led, p.horizoff, w).ang;
    // vraw/vsmooth show the flicker the smoother removes; full vs led the dial's effect; held= is
    // aim_hold_view_rule's answer, so persistent misses can be read as "hold was off".
    LOGI("aim diag: target=%d pic=%d walked=%d enemies=%d los=%d cands=%d weapon=%d cls=%d "
         "vel=%d leadvel=%d leadscale=%d choice=%d raw=(%d,%d,%d) full=(%d,%d,%d) "
         "led=(%d,%d,%d) ahead=(%d,%d,%d) noaheadang=%d vraw=(%d,%d,%d) "
         "vsmooth=(%d,%d,%d) n=%d dist=%d floorz=%d grounded=%d held=%d "
         "ang=%d engine_getangle=%d angdiff=%d horiz=%d horizoff=%d playerang=%d",
         sel->index, sel->picnum, walked, enemies, losPassed, n, p.currWeapon, (int)w.cls,
         w.vel, leadVel, leadScale, (int)choice, raw.x, raw.y, raw.z,
         full.x, full.y, full.z, led.x, led.y, led.z,
         adx, ady, adz, noAheadAng,
         sel->vx, sel->vy, sel->vz, svx, svy, svz, s_vel.samples, sel->dist2d,
         sel->floorz, sel->grounded ? 1 : 0, s_viewHeldThisTic ? 1 : 0,
         use.ang, engAng, angdiff, use.horiz, p.horizoff, p.ang);
  }
}

// The first ten view writes are logged before/after: write #n's `after` and #n+1's `before` are the same field one tic
// apart, so a dragged view shows as a mismatch.
static volatile unsigned s_aimWriteLogCount = 0;

bool aim_apply(const PlayerAim& p) {
  if (!p.valid) { s_aimWroteLastTic = false; return false; }

  // The aimbot's own gate: acquisition also runs for silent aim, so a target is published on tics where the aimbot must
  // not move the view (aimhold=1, nothing held, silenton=1).
  if (!aim_should_run(g_cfg.aimon != 0, g_cfg.aimhold != 0, aim_hold_active(),
                      input_menu_open(), p.dead, p.valid, p.cameraActive)) {
    s_aimWroteLastTic = false;
    return false;
  }

  if (s_pub.targetIndex < 0) { s_aimWroteLastTic = false; return false; }

  const int pct = aim_smooth_clamped(g_cfg.aimsmooth);
  int a = 0, h = 0;
  if (!aim_next_view(p.ang, p.horiz, s_pub.solvedAng, s_pub.solvedHoriz, pct, &a, &h)) {
    s_aimWroteLastTic = false;
    return false;
  }

  // The non-collapsing writer leaves oang/ohoriz/ohorizoff at last tic's value so the renderer interpolates this tic's
  // step (collapsing them made strafing tracking one visible step per tic). The freeze keeps the collapsing writer.
  const bool wrote = engine_write_player_aim(a, h);
  s_aimWroteLastTic = wrote;

  if (g_cfg.debuglog != 0 && s_aimWriteLogCount < 10) {
    s_aimWriteLogCount++;
    LOGI("aim: view write #%u target=%d ang %d->%d horiz %d->%d "
         "(solved %d/%d, smooth %d) wrote=%d",
         s_aimWriteLogCount, s_pub.targetIndex, p.ang, a, p.horiz, h,
         s_pub.solvedAng, s_pub.solvedHoriz, pct, wrote ? 1 : 0);
  }
  return wrote;
}

// Shot-time measurement (aim.h). Bounded and debuglog-gated; 300 lines rather than 20: twenty catches a convention
// error but not a miss pattern over a level.
const int AIM_SHOT_DIAG_MAX_LINES = 300;
static volatile unsigned s_aimShotLogCount = 0;

static unsigned s_lastShotNo = 0;
static unsigned s_lastShotMs = 0;
static int      s_lastShotTarget = -1;

void aim_last_shot(unsigned* no, unsigned* ms, int* target) {
  if (no) *no = s_lastShotNo;
  if (ms) *ms = s_lastShotMs;
  if (target) *target = s_lastShotTarget;
}

void aim_shot_budget_reset() {
  s_aimShotLogCount = 0;
}

void aim_shot_diag(const PlayerAim& p, int atwith, bool redirect) {
  if (g_cfg.debuglog == 0) return;
  if (s_aimShotLogCount >= (unsigned)AIM_SHOT_DIAG_MAX_LINES) return;
  // A crosshair-mode triggerbot shot is aimed by the player's own view with possibly no acquisition, so the line reports
  // -1 and zero geometry instead of refusing on targetIndex < 0.
  if (!p.valid) return;
  // Gated on the aimbot's last write, not a published target: with aimon off and silenton on, acquisition publishes while
  // aim_apply refuses, and the lines would blame the player's own error. The shot runs before this tic's aim_apply, so
  // last tic's write is right; also admits the triggerbot and silent redirect. Never average angerr across both views.
  const bool viaAim = s_aimWroteLastTic;
  const bool viaTbot = tbot_fired_last_tic();
  const bool viaSilent = redirect;
  if (!viaAim && !viaTbot && !viaSilent) return;

  // s_pub read directly: hooked_shoot runs before aim_tic publishes, so no writer can be mid-write.
  const int target = s_pub.targetIndex;
  const bool haveTarget = target >= 0 && target < rva::MAX_SPRITES;

  // Widened (hostile live coordinates). Meaningful only with a published aim point; otherwise both fields print -1.
  int exact = -1, angerr = 0;
  if (haveTarget) {
    const int dx = clamp_ll_to_int((long long)s_pub.aimx - (long long)p.eyex);
    const int dy = clamp_ll_to_int((long long)s_pub.aimy - (long long)p.eyey);
    exact = engine_getangle(dx, dy);
    angerr = aim_shot_angerr(p.ang, exact);
  }

  // Raw step, saturated rather than capped: a step the cap refused should read as the large number it was.
  const int stepx = clamp_ll_to_int((long long)p.eyex - (long long)p.oposx);
  const int stepy = clamp_ll_to_int((long long)p.eyey - (long long)p.oposy);
  const int stepz = clamp_ll_to_int((long long)p.eyez - (long long)p.oposz);

  // The ray `shoot` is about to cast with the accuracy patches on (zero spread, tbot_ray's ray), needing no published
  // target, so it answers "did this shot hit something".
  const short* sintable = (const short*)(g_base + rva::SINTABLE);
  int vx = 0, vy = 0, vz = 0;
  tbot_ray(p.ang, p.horiz, p.horizoff, sintable, &vx, &vy, &vz);
  const int sz = aim_shot_origin_z(p.eyez, p.pyoff);

  int hitsect = -1, hitwall = -1, hitsprite = -1, hitx = 0, hity = 0, hitz = 0;
  const bool hitOk = engine_hitscan(p.eyex, p.eyey, sz, p.cursectnum, vx, vy, vz,
                                    &hitsect, &hitwall, &hitsprite, &hitx, &hity, &hitz);
  const int hitDist = hitOk ? trigger_world_dist2d(p.eyex, p.eyey, hitx, hity) : -1;

  // Live target: every field read from the sprite at trigger-down, not the snapshot (last tic's), since this
  // instrument measures the gap.
  int lx = 0, ly = 0, lz = 0, picnum = -1, extra = 0, cstat = 0;
  int fwd = 0, lat = 0, vert = 0, halfw = 0, hitw = 0, hitz0 = 0, hitz1 = 0;
  if (haveTarget && engine_verified()) {
    lx = engine_sprite_field_l(target, SPR_X);
    ly = engine_sprite_field_l(target, SPR_Y);
    lz = engine_sprite_field_l(target, SPR_Z);
    picnum = engine_sprite_field_s(target, SPR_PICNUM);
    extra = engine_sprite_field_s(target, SPR_EXTRA);
    cstat = engine_sprite_field_s(target, SPR_CSTAT) & 0xFFFF;
    const int xrepeat = engine_sprite_field_u8(target, SPR_XREPEAT);
    const int yrepeat = engine_sprite_field_u8(target, SPR_YREPEAT);
    const int tilesizx = engine_tilesizx(picnum);
    const int tilesizy = engine_tilesizy(picnum);

    // `state=` is from actor_state_cached (once per sprite per tic; a shotgun's pellets share a query). Extents are the
    // ESP box's (project.h), so `halfw` holds `lat` to the same on-screen answer; `hitw` is the engine's hitscan
    // half-width, hitz0/hitz1 the vertical window it tests `vert`'s ray against.
    const SpriteExtents ext = proj_sprite_extents(lz, cstat, tilesizx, tilesizy, xrepeat, yrepeat);
    halfw = ext.halfWidth;
    hitw  = shot_hitscan_halfw(tilesizx, xrepeat);
    hitz0 = ext.bottomZ;
    hitz1 = ext.topZ;

    fwd = shot_forward(p.eyex, p.eyey, p.ang, lx, ly, sintable);
    lat = shot_lateral(p.eyex, p.eyey, p.ang, lx, ly, sintable);
    // Sprite centre: z is the feet for a floor-anchored face sprite, so measuring against it reads every level shot as high.
    const int centreZ = clamp_ll_to_int((long long)ext.topZ +
                                        ((long long)ext.bottomZ - (long long)ext.topZ) / 2);
    vert = shot_vertical(sz, vz, fwd, centreZ);
  }

  s_aimShotLogCount++;
  s_lastShotNo = s_aimShotLogCount;
  s_lastShotMs = wallclock_ms();
  s_lastShotTarget = target;
  // `via` priority: silent, aim, tbot (aim and tbot can both hold; aim is the stronger claim).
  const char* via = viaSilent ? "silent" : (viaAim ? "aim" : "tbot");
  // `tic=` pairs a shotgun pull's pellets with dmgnum.cpp's hit instrument; hs/hw/hsect/hd are the hitscan answer for
  // the ray above, so the line answers hit or miss itself.
  LOGI("aim shot: #%u via=%s aa=%d target=%d pic=%d atwith=0x%X ang=%d exact=%d angerr=%d "
       "horiz=%d step=(%d,%d,%d) live=(%d,%d,%d) fwd=%d lat=%d vert=%d halfw=%d hitw=%d "
       "hitz0=%d hitz1=%d hs=%d hw=%d hsect=%d hd=%d "
       "extra=%d cstat=0x%04X state=%s ms=%u tic=%u",
       s_aimShotLogCount, via, p.autoAim, target, picnum,
       (unsigned)(unsigned short)atwith,
       p.ang, exact, angerr, p.horiz, stepx, stepy, stepz,
       lx, ly, lz, fwd, lat, vert, halfw, hitw,
       hitz0, hitz1, hitsprite, hitwall, hitsect, hitDist,
       extra, (unsigned)cstat, actor_state_name(actor_state_cached(target)),
       s_lastShotMs, input_tic_count());
}
