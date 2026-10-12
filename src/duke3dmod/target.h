#pragma once
// Targeting core. Pure: links into test_host. Every aimbot and silent-aim decision is a function here.

// Liveness is not `extra > 0` (live actors showed extra down to -9 with cstat 0x0101). The rule is the engine's aim()
// (VA 0x004f3010) minus its health term: status lists {1, 2}, badguy(), `xrepeat != 0 && (cstat & 0x8101) == 0x101`.

// cstat bits 0 blocking, 8 hittable, 15 invisible, from aim()'s own compare so the two rules cannot drift.
const int SPR_CSTAT_SHOOT_MASK = 0x8101;
const int SPR_CSTAT_SHOOT_WANT = 0x0101;

// Live-actor status lists (engine.h), restated: this header cannot include the layout.
const int TARGET_STAT_ACTOR       = 1;
const int TARGET_STAT_ZOMBIEACTOR = 2;

bool target_hittable_rule(int xrepeat, int cstat);

bool target_alive_rule(int statnum, bool badguy, int cstat);

// Display only; a kept-alive actor can sit at -7.
int target_hp_for_display(int extra);

bool target_velocity_from(int x, int y, int z, int bx, int by, int bz, int* vx, int* vy, int* vz);


// Weapon classes (speeds and pitch formulas: docs/re/targeting.md).
enum WeaponClass { WC_NONE = 0, WC_HITSCAN = 1, WC_PROJECTILE = 2, WC_MELEE = 3 };
struct WeaponInfo { int weaponIndex; WeaponClass cls; int vel; int horizMul; };
// curr_weapon: 0 knee, 1 pistol, 2 shotgun, 3 chaingun, 4 rpg, 5 pipebomb, 6 shrinker, 7 devastator, 8 tripbomb,
// 9 freezer, 10 handremote, 11 expander.
WeaponInfo target_weapon_info(int currWeapon);

enum AimPoint { AP_CENTRE = 0, AP_UPPER = 1 };
int target_aim_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap);

// A sprite slot is recycled when its actor dies, so identity is (index, picnum).
struct TargetCandidate {
  int index; int picnum; int x, y, z; int vx, vy, vz; int aimz; int sectnum; int dist2d;
  int aimzRung;       // which target_probe_z rung produced `aimz`
  int floorz; bool grounded;
  int extra;          // health; not liveness, only target_pick's tiebreak
};

// Health as a preference tier, never a filter: a dying actor keeps cstat 0x0101 until its CON clears it (hit handler VA
// 0x004e2ea0), and dying looks like low health.
bool target_candidate_healthy(int extra);

enum TargetMode { TM_CROSSHAIR = 0, TM_CLOSEST = 1 };
int target_angular_offset(int viewAng, int dx, int dy);
// Two tiers (healthy first); within one the smallest cone offset (ties by distance), or nearest for TM_CLOSEST.
int target_pick(const TargetCandidate* cands, int count, int viewAng, int eyex, int eyey, TargetMode mode, int crosshairConeUnits);

// Two fixed iterations of t = dist(P + V*t, S) / vel; vel <= 0 returns P.
struct Point3 { int x, y, z; };
Point3 target_lead(Point3 shooter, Point3 p, int vx, int vy, int vz, int vel);

// A full lead misses (the CON restarts actors that alterang eases toward a `move`); scalePct 0 none .. 100 full, 70 default.
Point3 target_lead_scaled(Point3 raw, Point3 led, int scalePct);

const int AIM_LEADSCALE_MIN     = 0;
const int AIM_LEADSCALE_MAX     = 100;
const int AIM_LEADSCALE_DEFAULT = 70;
int aim_leadscale_clamped(int pct);

// The solve runs one tic ahead of the shot: processinput saves opos* (VA 0x004fb020), clipmoves (0x004fcf4d) and calls
// shoot (0x004fe462) before the aimbot's write, so player and enemy are one tic stale. Silent aim re-solves at shot time.

// Per-tic step cap, Build XY; `aim shot:` saw ~230 at run speed. At or above 1024 the step is dropped.
const int AIM_AHEAD_MAX_STEP = 1024;

// `pos - opos` (VA 0x004fb020) in long long; 0 above `cap`. z needs AIM_AHEAD_MAX_STEP_Z.
int aim_ahead_step(int pos, int opos, int cap);

Point3 aim_ahead_origin(Point3 eye, int dx, int dy, int dz);

// Raw aim point plus one tic of smoothed velocity: the read is after moveactors(T-2), the shot meets T-1.
Point3 aim_ahead_target(Point3 raw, int svx, int svy, int svz);

// Wrapped yaw error in [-1024, +1023] (5 vs 2040 is +13); long long, immune to hostile `ang`.
int aim_shot_angerr(int ang, int exact);

// shot_* decompose the shot ray (tbot_ray, tbot.h) for the shot instrument; sintable is 16384-scaled.

// Perpendicular distance from the ray, positive = left (ang 0 faces +x, y points down); null sintable yields 0.
int shot_lateral(int ox, int oy, int ang, int sx, int sy, const short* sintable);

int shot_forward(int ox, int oy, int ang, int sx, int sy, const short* sintable);

// targetZ - (sz + fwd*vz/16384), Build z (16x finer than xy). Positive = the shot went over.
int shot_vertical(int sz, int vz, int fwd, int targetZ);

// Build's hitscan half-width: squared perpendicular distance <= (tilesizx*xrepeat)^2 >> 7; 1000/11314 ~ 1/sqrt(128).
int shot_hitscan_halfw(int tilesizx, int xrepeat);

// A separate pair from ESP_DRAWDIST_* so the two cannot be re-coupled; defaults to the maximum.
const int AIM_RANGE_MIN     = 512;
const int AIM_RANGE_MAX     = 32768;
const int AIM_RANGE_DEFAULT = AIM_RANGE_MAX;
int aim_range_clamped(int units);

inline bool aim_within_range(int dist2d, int aimrange) { return dist2d <= aimrange; }

// Pitch: hitscan horiz = 100 - horizoff - 8*dz/dist; projectile ... - (dz*vel/dist)/horizMul; zero distance is invalid.
struct AimSolution { int ang; int horiz; bool valid; };
AimSolution target_solve(Point3 eye, Point3 aim, int horizoff, const WeaponInfo& w);

int target_smooth_ang(int current, int desired, int smoothPct);      // short way round
int target_smooth_horiz(int current, int desired, int smoothPct);
bool target_within_redirect(int currentAng, int desiredAng, int maxUnits);
int target_degrees_to_units(int degrees);    // 360 degrees = 2048 units

// aim.cpp cannot link into test_host, so its decisions live here, pure and pinned.

// aimon, hold, and four absolute refusals: menuOpen, dead, !valid, cameraOn (newowner >= 0: processinput overwrites
// pos/ang from the camera sprite, VA 0x004fb20d-0x004fb216).
bool aim_should_run(bool on, bool hold, bool buttonDown, bool menuOpen, bool dead, bool valid,
                    bool cameraOn);

// With aimhold=1 and the button up aim_should_run refuses, yet silent aim wants a target; the refusals repeat.
bool aim_acquire_should_run(bool aimbotWants, bool silentOn, bool menuOpen,
                            bool dead, bool valid, bool cameraOn);

// Hold view: the engine adds the mouse turn after the aimbot's write and before `shoot` (10 of 30 moving misses in a
// 183-tic run), so it is zeroed earlier (engine_input_clear_turn).

// wroteLastTic (aim_view_held) is the safety property.
inline bool aim_hold_view_rule(bool wroteLastTic, bool menuOpen, bool lockout) {
  return wroteLastTic && !menuOpen && !lockout;
}

// All six required; `alive`/`targetable` stay separate parameters so a widened signature breaks every call site.
bool aim_candidate_ok(bool badguy, bool alive, bool inRange, bool los, bool reachable,
                      bool targetable);

// The eye is not the muzzle: `shoot` builds sz = posz + pyoff + 0x400 (VA 0x004f36c0-0x004f36d3; posz ps+0x21C, pyoff
// ps+0x244). Line of sight from the eye failed on wall turrets and heads over cover.

// The solve still uses the eye: a muzzle pitch would aim above the enemy up close.
const int AIM_SHOT_Z_BIAS = 0x400;   // shoot's own, VA 0x004f36cc
int aim_shot_origin_z(int posz, int pyoff);

// Rung 0 the chosen point, 1 upper body z - 3h, 2 head z - 4h + h/4. Silent aim's re-solve needs the rung acquisition
// chose, else it aims at a point cansee rejected. Out of range, incl. -1, answers 0.
int target_probe_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int rung);

// Body points for line of sight, in order, at most `cap`; the head's quarter-h margin keeps the probe off the edge where
// cansee is a coin toss.

// Order is the decision: lowest visible point first, the head only when the body is covered, else it aims at heads.
const int AIM_PROBE_MAX = 3;
int aim_probe_zs(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int* out, int cap);

// First, not best: lowest-first aims at what an exposed head shows; -1 if none.
int aim_first_visible(const bool* visible, int count);

// processinput clamps horiz to AIM_HORIZ_MIN..MAX; clamping an out-of-range solve pinned the assist aiming at the wall,
// so such a target is dropped.
bool aim_horiz_reachable(int horiz);

// The lead may push a near-edge target out of reach; fall back to the raw point rather than re-pick, which would reset the
// smoother.
enum AimSolutionChoice { ASC_NONE = 0, ASC_LED = 1, ASC_RAW = 2 };
AimSolutionChoice aim_choose_solution(bool ledValid, int ledHoriz, bool rawValid, int rawHoriz);

int aim_dist2d(int dx, int dy);

// Default 30; config.cpp's load calls the same clamp.
const int AIM_CONE_MIN     = 1;
const int AIM_CONE_MAX     = 90;
const int AIM_CONE_DEFAULT = 30;
int aim_cone_clamped(int degrees);

const int AIM_SMOOTH_MIN = 0;
const int AIM_SMOOTH_MAX = 100;
const int AIM_SMOOTH_DEFAULT = AIM_SMOOTH_MIN;
int aim_smooth_clamped(int pct);

// Seqlock from the tic phase (aim_tic) to the render phase: a half-updated AimSnapshot is detectable. Writer: begin
// (odd), stores, end (even); reader samples around its copy.
void snap_begin(unsigned* seq);
void snap_end(unsigned* seq);
bool snap_read_ok(unsigned before, unsigned after);

// 16384 entries would be 400 KB of stack in a hook body; the second guard (after engine_next_sprite's cycle counter)
// against an unbounded loop in the game tic.
const int AIM_MAX_CANDIDATES = 64;

// Slot or -1. Full: replace the FARTHEST only if the newcomer is nearer, so overflow keeps the nearest `cap`.
int aim_keep_index(const TargetCandidate* cands, int count, int cap, int dist2d);

// WC_NONE covers ordnance/placement weapons and the half-identified shrinker; WC_MELEE (knee) has no aim point.
bool aim_weapon_usable(WeaponClass cls);

// Unknown falls back to the default, not the alternative; config.cpp already clamps to 0/1.
TargetMode aim_mode_from_config(int aimtarget);
AimPoint   aim_point_from_config(int aimpoint);


// The engine clamps horiz to -99..299 (VA 0x004fd559-0x004fd583, ps+0x220) before projecting; the aimbot writes after, so
// it needs its own. Not modelled: with mouse aiming off (ps+0x280, VA 0x004fd4da) the engine snaps horiz 96..104 to 100
// each tic (VA 0x004fd50a-0x004fd522).
const int AIM_HORIZ_MIN = -99;
const int AIM_HORIZ_MAX = 299;
int aim_horiz_clamped(int horiz);

// Smoothing plus the pitch envelope; both pitch endpoints clamp before any subtraction (target.cpp says why).
bool aim_next_view(int curAng, int curHoriz, int wantAng, int wantHoriz, int smoothPct,
                   int* outAng, int* outHoriz);

// Lead-velocity smoother: per-tic velocity swings (actors move in bursts), so an EMA over AIM_VEL_EMA_TICS, no ring buffer.

// Horizontal per-tic step no real sprite exceeds: ssp (VA 0x00498b80) is bounded by xvel, max 840 (SPIT/FIRELASER, VA
// 0x004f4d58). A larger step is a teleport, respawn or bad read.
const int AIM_VEL_MAX_STEP = 840;

// z cap: makeitfall clamps zvel to 0x1800 = 6144 (VA 0x004dd179); the xy cap would reject every falling enemy.
const int AIM_VEL_MAX_FALL = 6144;

// Vertical counterpart to AIM_AHEAD_MAX_STEP, same mover.
const int AIM_AHEAD_MAX_STEP_Z = AIM_VEL_MAX_FALL;

// EMA window, alpha = 2/(N+1). 6 not 10: a 10-tic average still describes a run that ended a third of a second ago.
const int AIM_VEL_EMA_TICS = 6;
const int AIM_VEL_EMA_NUM  = 2;
const int AIM_VEL_EMA_DEN  = AIM_VEL_EMA_TICS + 1;

// Scaled so a small step cannot truncate to zero; * and /, not shifts (negative operand UB before C++20).
const int AIM_VEL_SCALE = 256;

// Absolute, not relative: bursty samples are several times the mean, so a relative test rejects the signal.
bool aim_vel_spike(int vx, int vy, int vz);

// Keyed by (index, picnum): a recycled slot would leak a dead trooper's velocity. `samples` saturates.
struct VelSmooth { int index; int picnum; int samples; int qx, qy, qz; };

void aim_vel_reset(VelSmooth* s);

// A different (index, picnum) resets and seeds; a spike is dropped; else EMA. Truncation settles a steady -200 at -199,
// deliberately.
bool aim_vel_update(VelSmooth* s, int index, int picnum, int vx, int vy, int vz,
                    int* outvx, int* outvy, int* outvz);

// Pure half of aim_smoothed_velocity: a read, not a fold; false, untouched, for null or an identity mismatch.
bool vel_smooth_read(const VelSmooth* s, int index, int picnum,
                     int* outvx, int* outvy, int* outvz);

// 0 when aimlead is off (the hitscan path).
int aim_lead_vel(int weaponVel, int aimlead);

// Silent aim's pure decisions; silent.cpp patches live memory, so its body is only read, decide, substitute, call, restore.

// Half-angle of the redirect cone, degrees, default 45; config.cpp calls the same clamp.
const int SILENT_MAXANGLE_MIN     = 1;
const int SILENT_MAXANGLE_MAX     = 180;
const int SILENT_MAXANGLE_DEFAULT = 45;
int silent_maxangle_clamped(int degrees);

// Player aimed-shot atwith picnums from processinput's shoot calls (VA 0x004fe458-0x004fecf8): 0xa23 pistol, 0xa35
// shotgun x7, 0x9e8 chaingun, 0x990 expander, 0x9fc shrinker, 0xa2d rpg/devastator, 0x669 freezer. Not tripbomb 0xa03,
// knee 0x9d9, enemy weapons, or the 0x8f8..0x8fb hit decals (checkhitsprite re-enters shoot).
bool silent_atwith_is_player_shot(int atwith);

// Safe to substitute FROM into live memory: targetIndex carries every aim_tic refusal, solvedAng a real Build angle,
// solvedHoriz inside the envelope (else `(100 - horiz - horizoff) * 0x62` overflows).
bool silent_snapshot_usable(int targetIndex, int solvedAng, int solvedHoriz);

// Six terms, each refusing alone; los because a redirect through a wall is a shot into it.
bool silent_decide(bool on, bool isLocalPlayerSprite, bool atwithIsPlayerShot,
                   bool haveTarget, bool los, int curAng, int wantAng, int maxUnits);

// Silent aim re-solves from the target's live position: hooked_shoot runs before aim_tic publishes, so the published
// pair was one tic (33 ms) stale.

// `coneRejected` is diagnostic only.
struct SilentResolve { int ang; int horiz; bool valid; bool live; bool coneRejected; };

// Same lead and solve as the marker, so shot and marker cannot disagree.

// Falls back to the published pair for a degenerate solve, unreachable pitch or out-of-cone angle; `live` only when
// substituted.
SilentResolve silent_resolve(Point3 eye, int horizoff, const WeaponInfo& w,
                             int snapAng, int snapHoriz,
                             Point3 liveRaw, int vx, int vy, int vz,
                             int leadVel, int leadScalePct,
                             int curAng, int maxRedirectUnits);


// Grounded actors must get vz = 0: moveactors stores bposx/y/z at the top of each think (VA 0x004a08e4-0x004a090a), so
// z - bposz is the gravity-and-snap cycle (standing troopers read +272..+320/tic), and the lead would aim ~2400
// z-units low over an RPG's flight.

// Where the engine parks a landed actor: makeitfall tests z against hittype[i].floorz - 0x100 (VA 0x004dd12d-0x004dd13f)
// and snaps to it with zvel = 0 (VA 0x004dd1da).
const int AIM_FALL_FLOOR_OFFSET = 256;

// One tic of gravity: `gc` at VA 0x005ceb38 (init 176), loaded at VA 0x004dd05e, added to zvel (0x004dd187), zvel to z
// (0x004dd1a9).
const int AIM_FALL_GRAVITY = 176;

// Grounded margin: the engine offset plus two gravity steps, covering floorz/z one think behind, sloping floors and a
// measured artefact up to 320. A flyer 608 z-units up is only 38 xy-units off the ground.
const int AIM_GROUND_MARGIN = AIM_FALL_FLOOR_OFFSET + 2 * AIM_FALL_GRAVITY;

bool aim_actor_grounded(int z, int floorz, int margin);

// vz for the lead solver: 0 for a grounded actor, applied BEFORE the smoother so the artefact never poisons the average.
int aim_grounded_vz(int z, int floorz, int vz, int margin);

// Lost-enemy diagnostic: logs every field that could explain an index vanishing while its sprite looks the same; it
// exposed the health filter.

// All three windows share aim_throttle_due.
const unsigned AIM_DIAG_WINDOW_MS = 3000;

const unsigned AIM_LOST_WINDOW_MS = 3000;

// Unsigned subtraction makes a GetTickCount wrap a small difference.
bool aim_throttle_due(unsigned nowMs, unsigned lastMs, bool first, unsigned periodMs);

// Whether THIS index deserves a line this window: presentLast, !presentNow ("vanished"), picnumSame (not a recycled
// slot), xrepeat non-zero (zero is the engine deleting it), not already reported.
bool aim_lost_should_log(bool presentLast, bool presentNow, bool picnumSame, int xrepeat,
                         bool alreadyLoggedThisWindow);

// Presence-set reset: a level change skips the walk and indices are allocated in deterministic map order, so stale
// collisions would fake the signal. Signals: (playerSprite, numsectors), and a time gap (a level load outlasts a tic).
const unsigned AIM_PRESENCE_MAX_GAP_MS = 1000;
bool aim_presence_reset(int prevPlayerSprite, int prevNumsectors,
                        int playerSprite, int numsectors,
                        unsigned nowMs, unsigned lastMs, unsigned maxGapMs);
